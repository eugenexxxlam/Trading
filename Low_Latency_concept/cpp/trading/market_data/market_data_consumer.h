#pragma once

#include <functional>
#include <map>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/mcast_socket.h"

#include "exchange/market_data/market_update.h"

/*
 * MARKET DATA CONSUMER - TRADING FIRM'S MARKET DATA RECEIVER
 * ===========================================================
 * 
 * PURPOSE:
 * Trading firm's receiver for real-time market data from exchange.
 * Subscribes to multicast feeds, handles packet loss, maintains order book.
 * 
 * RESPONSIBILITIES:
 * 1. Subscribe to incremental multicast stream (real-time updates)
 * 2. Detect packet loss (sequence number gaps)
 * 3. Subscribe to snapshot stream (recovery)
 * 4. Synchronize snapshot with incremental stream
 * 5. Forward market updates to trading engine
 * 
 * TWO-STREAM MODEL:
 * ```
 * Exchange:
 *   Incremental Stream (UDP multicast) -> Market Data Consumer
 *   Snapshot Stream (UDP multicast)    -> (only during recovery)
 *                                           |
 *                                           v
 *                                      [LF Queue] -> Trading Engine
 * ```
 * 
 * NORMAL OPERATION:
 * 1. Subscribe to incremental stream
 * 2. Receive market updates (ADD, CANCEL, TRADE, etc.)
 * 3. Validate sequence numbers (1, 2, 3, ...)
 * 4. Forward to trading engine (via lock-free queue)
 * 
 * RECOVERY MODE (PACKET LOSS):
 * 1. Detect gap in sequence numbers (e.g., received seq 100, then 102)
 * 2. Enter recovery mode (in_recovery_ = true)
 * 3. Subscribe to snapshot stream
 * 4. Buffer incoming incremental updates
 * 5. Receive complete snapshot (SNAPSHOT_START ... SNAPSHOT_END)
 * 6. Apply buffered incremental updates (after snapshot seq#)
 * 7. Exit recovery mode (in_recovery_ = false)
 * 8. Unsubscribe from snapshot stream
 * 9. Resume normal operation
 * 
 * RECOVERY EXAMPLE:
 * ```
 * Normal: Receive seq 98, 99, 100
 * Gap: Receive seq 102 (missing 101)
 * Action: Enter recovery
 * Subscribe: Snapshot stream
 * Buffer: Incremental seq 102, 103, 104, ...
 * Receive: Snapshot (built up to seq 100)
 * Apply: Incremental seq 101, 102, 103, 104, ...
 * Result: Recovered, no data loss
 * ```
 * 
 * MESSAGE QUEUEING:
 * - snapshot_queued_msgs_: Buffered snapshot messages (ordered)
 * - incremental_queued_msgs_: Buffered incremental messages (ordered)
 * - std::map<seq_num, update>: Automatic ordering by sequence number
 * 
 * PERFORMANCE:
 * - Normal: 1-5 μs per update (receive + forward)
 * - Recovery: 10-100 ms (snapshot receive + apply)
 * - Frequency: Recovery rare (<0.01% packets lost)
 * - Not latency-critical: Market data is slightly delayed (acceptable)
 * 
 * THREADING:
 * - Single thread (dedicated to market data)
 * - Non-blocking UDP I/O
 * - Lock-free queue (to trading engine)
 * - CPU affinity (can be pinned)
 */

namespace Trading {
  class MarketDataConsumer {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Initializes market data consumer with connection parameters.
     * 
     * Parameters:
     * - client_id: Trading firm's client identifier (for logging)
     * - market_updates: Lock-free queue to trading engine
     * - iface: Network interface ("eth0", "lo", etc.)
     * - snapshot_ip: Snapshot multicast group
     * - snapshot_port: Snapshot port
     * - incremental_ip: Incremental multicast group
     * - incremental_port: Incremental port
     * 
     * Initializes:
     * - Incremental socket: Subscribe immediately
     * - Snapshot socket: Not subscribed (only during recovery)
     * - Queues: Empty (no buffered messages)
     * - State: Normal operation (not in recovery)
     */
    MarketDataConsumer(Common::ClientId client_id, Exchange::MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                       const std::string &snapshot_ip, int snapshot_port,
                       const std::string &incremental_ip, int incremental_port);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleans up market data consumer resources.
     * 
     * ALGORITHM:
     * 1. Stop thread (set run_ = false)
     * 2. Wait 5 seconds (drain queues, close sockets)
     */
    ~MarketDataConsumer() {
      stop();  // Stop thread

      // Wait for pending operations
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);
    }

    /*
     * START - BEGIN MARKET DATA CONSUMER THREAD
     * ==========================================
     * 
     * Creates and starts market data consumer thread.
     * 
     * ALGORITHM:
     * 1. Set run_ = true (enable main loop)
     * 2. Create and start thread (run() method)
     * 3. Assert thread creation succeeded
     * 
     * THREAD:
     * - Name: "Trading/MarketDataConsumer"
     * - CPU affinity: -1 (any core)
     * - Production: Pin to dedicated core
     */
    auto start() {
      run_ = true;  // Enable main loop
      
      // Create and start thread
      ASSERT(Common::createAndStartThread(-1, "Trading/MarketDataConsumer", [this]() { run(); }) != nullptr, 
             "Failed to start MarketData thread.");
    }

    /*
     * STOP - SHUTDOWN MARKET DATA CONSUMER
     * =====================================
     * 
     * Stops market data consumer thread gracefully.
     * 
     * ALGORITHM:
     * - Set run_ = false
     * - Main loop exits naturally
     * - Thread terminates
     */
    auto stop() -> void {
      run_ = false;  // Disable main loop
    }

    // Deleted constructors (prevent accidental copies)
    MarketDataConsumer() = delete;
    MarketDataConsumer(const MarketDataConsumer &) = delete;
    MarketDataConsumer(const MarketDataConsumer &&) = delete;
    MarketDataConsumer &operator=(const MarketDataConsumer &) = delete;
    MarketDataConsumer &operator=(const MarketDataConsumer &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Next expected sequence number on incremental stream
    // Starts at 1, increments for each update (1, 2, 3, ...)
    // Used to detect gaps (packet loss)
    size_t next_exp_inc_seq_num_ = 1;

    // Lock-free queue: Market updates to trading engine (output)
    // Consumer produces, trading engine consumes
    Exchange::MEMarketUpdateLFQueue *incoming_md_updates_ = nullptr;

    // Thread control flag
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (per-client log file)
    // File: "trading_market_data_consumer_<client_id>.log"
    Logger logger_;

    // Multicast sockets (UDP)
    // incremental_mcast_socket_: Always subscribed (real-time updates)
    // snapshot_mcast_socket_: Only subscribed during recovery
    Common::McastSocket incremental_mcast_socket_, snapshot_mcast_socket_;

    // Recovery state flag
    // false: Normal operation (processing incremental stream)
    // true: Recovery mode (buffering, waiting for snapshot)
    bool in_recovery_ = false;

    // Snapshot connection parameters (saved for recovery)
    // Used by startSnapshotSync() to subscribe during recovery
    const std::string iface_, snapshot_ip_;
    const int snapshot_port_;

    // Buffered messages during recovery (ordered by sequence number)
    // std::map: Automatic ordering (sorted by key)
    // Key: Sequence number (size_t)
    // Value: Market update (MEMarketUpdate)
    typedef std::map<size_t, Exchange::MEMarketUpdate> QueuedMarketUpdates;
    
    // snapshot_queued_msgs_: Messages from snapshot stream
    // - SNAPSHOT_START, CLEAR, ADD, ADD, ..., SNAPSHOT_END
    // - Used to rebuild order book during recovery
    QueuedMarketUpdates snapshot_queued_msgs_;
    
    // incremental_queued_msgs_: Messages from incremental stream
    // - Buffered during recovery (may arrive while waiting for snapshot)
    // - Applied after snapshot (to catch up to real-time)
    QueuedMarketUpdates incremental_queued_msgs_;

  private:
    /*
     * PRIVATE METHODS
     * ===============
     */
    
    /*
     * RUN - MAIN MARKET DATA LOOP
     * ============================
     * 
     * Main loop for market data consumer thread.
     * Polls multicast sockets for market updates.
     * 
     * ALGORITHM:
     * 1. Log thread start
     * 2. While run_ is true:
     *    a. Poll incremental socket (sendAndRecv)
     *       - Receives market updates
     *       - Triggers recvCallback when data received
     *    b. Poll snapshot socket (sendAndRecv)
     *       - Only active during recovery
     *       - Receives snapshot messages
     *       - Triggers recvCallback when data received
     * 3. Repeat until stopped
     * 
     * MULTICAST POLLING:
     * - sendAndRecv(): Non-blocking UDP receive
     * - Calls recv_callback_ when data received
     * - Both sockets use same callback (recvCallback)
     * - Callback determines source (incremental vs snapshot)
     * 
     * BUSY-WAIT:
     * - Continuously polls (even if no data)
     * - Low latency: Immediate response
     * - High CPU: Acceptable for trading
     * 
     * NOEXCEPT:
     * - No exception handling overhead
     * 
     * Implemented in market_data_consumer.cpp (see for details).
     */
    auto run() noexcept -> void;

    /*
     * RECV CALLBACK - PROCESS MARKET DATA UPDATE
     * ===========================================
     * 
     * Called by multicast socket when market update received.
     * Handles both incremental and snapshot messages.
     * 
     * ALGORITHM:
     * 1. Determine source: incremental or snapshot (socket->socket_fd_)
     * 2. If snapshot but not in recovery: Discard (unexpected)
     * 3. Parse buffer (may contain multiple updates)
     * 4. For each MDPMarketUpdate:
     *    a. Check if gap detected (seq# != expected)
     *    b. If gap: Enter recovery (startSnapshotSync)
     *    c. If in recovery: Buffer message (queueMessage)
     *    d. If normal operation: Forward to trading engine
     * 5. Shift remaining data in buffer
     * 
     * GAP DETECTION:
     * - Check: request->seq_num_ == next_exp_inc_seq_num_
     * - Gap: Packet loss (UDP is unreliable)
     * - Action: Enter recovery mode
     * 
     * RECOVERY TRIGGERING:
     * - First gap: Subscribe to snapshot stream (startSnapshotSync)
     * - Buffer: All subsequent messages (snapshot + incremental)
     * - Wait: Complete snapshot (SNAPSHOT_START ... SNAPSHOT_END)
     * - Sync: Apply buffered incrementals (checkSnapshotSync)
     * 
     * NORMAL OPERATION:
     * - Forward: MEMarketUpdate to trading engine (via queue)
     * - Increment: next_exp_inc_seq_num_++
     * - Fast path: Minimal processing (1-5 μs)
     * 
     * Parameters:
     * - socket: Multicast socket that received data
     * 
     * NOEXCEPT:
     * - No exceptions on hot path
     * 
     * Implemented in market_data_consumer.cpp (see for details).
     */
    auto recvCallback(McastSocket *socket) noexcept -> void;

    /*
     * QUEUE MESSAGE - BUFFER DURING RECOVERY
     * =======================================
     * 
     * Queues market update message during recovery.
     * Stores in ordered map (automatic sorting by sequence number).
     * 
     * ALGORITHM:
     * 1. If snapshot message:
     *    - Check for duplicate (shouldn't happen)
     *    - If duplicate: Clear queue (start over)
     *    - Store: snapshot_queued_msgs_[seq_num] = update
     * 2. If incremental message:
     *    - Store: incremental_queued_msgs_[seq_num] = update
     * 3. Log queue sizes
     * 4. Check if recovery complete (checkSnapshotSync)
     * 
     * std::map ORDERING:
     * - Automatic: Sorted by key (sequence number)
     * - Advantage: Easy to iterate in order
     * - Disadvantage: O(log N) insert (acceptable during recovery)
     * 
     * DUPLICATE HANDLING:
     * - Snapshot: Shouldn't happen (clear queue, restart)
     * - Incremental: Allowed (may receive multiple times)
     * - Reason: UDP multicast can duplicate packets
     * 
     * Parameters:
     * - is_snapshot: true if from snapshot stream
     * - request: Market update message (with seq#)
     * 
     * Implemented in market_data_consumer.cpp (see for details).
     */
    auto queueMessage(bool is_snapshot, const Exchange::MDPMarketUpdate *request);

    /*
     * START SNAPSHOT SYNC - BEGIN RECOVERY
     * =====================================
     * 
     * Initiates snapshot synchronization process.
     * Called when gap detected (packet loss).
     * 
     * ALGORITHM:
     * 1. Clear buffered messages (start fresh)
     * 2. Initialize snapshot socket
     * 3. Join snapshot multicast group (IGMP)
     * 4. Begin receiving snapshot stream
     * 
     * IGMP JOIN:
     * - IGMP: Internet Group Management Protocol
     * - Multicast subscription: Join group
     * - Network: Router forwards multicast to this subscriber
     * 
     * Implemented in market_data_consumer.cpp (see for details).
     */
    auto startSnapshotSync() -> void;

    /*
     * CHECK SNAPSHOT SYNC - ATTEMPT RECOVERY
     * =======================================
     * 
     * Checks if recovery is possible from buffered messages.
     * Attempts to synchronize snapshot with incremental stream.
     * 
     * ALGORITHM:
     * 1. Validate snapshot complete:
     *    a. Must start with SNAPSHOT_START
     *    b. Must have all sequence numbers (no gaps)
     *    c. Must end with SNAPSHOT_END
     * 2. Extract sync point (last_snapshot_msg.order_id_)
     * 3. Validate incremental complete:
     *    a. Must have all seq# after sync point
     *    b. No gaps in incremental stream
     * 4. If complete:
     *    a. Forward snapshot messages (rebuild order book)
     *    b. Forward incremental messages (catch up)
     *    c. Exit recovery mode
     *    d. Unsubscribe from snapshot stream
     * 5. If incomplete:
     *    a. Clear snapshot queue (wait for next snapshot)
     *    b. Keep incremental queue (may still be useful)
     *    c. Continue buffering
     * 
     * SYNC POINT:
     * - Snapshot built up to seq# N (stored in SNAPSHOT_END.order_id_)
     * - Apply incrementals starting from seq# N+1
     * - Result: Order book synchronized, no data loss
     * 
     * COMPLEXITY:
     * - O(S + I) where S = snapshot size, I = incremental size
     * - Typical: S = 10K orders, I = 10-100 incrementals
     * - Time: 10-100 ms (not latency-critical)
     * 
     * Implemented in market_data_consumer.cpp (see for details).
     */
    auto checkSnapshotSync() -> void;
  };
}

/*
 * MARKET DATA CONSUMER DESIGN CONSIDERATIONS
 * ===========================================
 * 
 * 1. UDP MULTICAST:
 *    - Unreliable: Packets can be lost (0.01-0.1% typical)
 *    - Fast: No TCP overhead, one-to-many efficient
 *    - Industry standard: All major exchanges use multicast
 * 
 * 2. TWO-STREAM MODEL:
 *    - Incremental: Real-time updates (always subscribed)
 *    - Snapshot: Full order book (only during recovery)
 *    - Advantage: Low bandwidth (snapshots rare)
 *    - Industry: NASDAQ ITCH, CME MDP 3.0, etc.
 * 
 * 3. GAP DETECTION:
 *    - Sequence numbers: Monotonic (1, 2, 3, ...)
 *    - Gap: Missing sequence number (packet loss)
 *    - Action: Immediately enter recovery
 *    - Fast: Detect on first gap (don't wait)
 * 
 * 4. SNAPSHOT RECOVERY:
 *    - Subscribe: Join snapshot multicast group
 *    - Buffer: Queue all messages (snapshot + incremental)
 *    - Validate: Complete snapshot (START...END, no gaps)
 *    - Sync: Apply incrementals after snapshot
 *    - Unsubscribe: Leave snapshot group (save bandwidth)
 * 
 * 5. MESSAGE QUEUEING:
 *    - std::map: Automatic ordering by sequence number
 *    - Advantage: Simple, correct
 *    - Disadvantage: O(log N) insert (acceptable during recovery)
 *    - Alternative: Vector + sort (more complex)
 * 
 * 6. PERFORMANCE:
 *    - Normal: 1-5 μs per update (hot path)
 *    - Recovery: 10-100 ms (cold path, rare)
 *    - Trade-off: Optimize normal path, recovery can be slower
 * 
 * RECOVERY SCENARIOS:
 * 
 * A) Single Packet Loss:
 *    - Loss: Seq 101 dropped
 *    - Gap: Receive 100, 102 (detect gap)
 *    - Recovery: Subscribe snapshot, buffer
 *    - Snapshot: Built up to seq 100
 *    - Apply: Incremental 101, 102, ... (from buffer)
 *    - Result: Recovered, no data loss
 * 
 * B) Multiple Packet Loss:
 *    - Loss: Seq 101-105 dropped
 *    - Gap: Receive 100, 106 (detect gap)
 *    - Recovery: Subscribe snapshot, buffer
 *    - Snapshot: Built up to seq 105 (lucky)
 *    - Apply: Incremental 106, ... (from buffer)
 *    - Result: Recovered, no data loss
 * 
 * C) Snapshot Incomplete:
 *    - Loss: Seq 101 dropped
 *    - Recovery: Subscribe snapshot
 *    - Snapshot: Gap in snapshot stream (also lost packets)
 *    - Action: Clear, wait for next snapshot (1 second)
 *    - Result: Delayed recovery (acceptable)
 * 
 * D) Late Join (Startup):
 *    - Startup: First message seq 12345 (not 1)
 *    - Gap: Expected 1, received 12345
 *    - Recovery: Subscribe snapshot (standard recovery)
 *    - Result: Synchronized from snapshot
 * 
 * LATENCY IMPACT:
 * - Normal: No impact (incremental stream unaffected)
 * - Recovery: Queues messages (buffer in memory)
 * - Delay: 10-100 ms (waiting for complete snapshot)
 * - Acceptable: Recovery is rare (<0.01% of time)
 * 
 * BANDWIDTH:
 * - Incremental: ~40 bytes/update * 1M/sec = 40 MB/s = 320 Mbps
 * - Snapshot: ~50 MB per snapshot * 1/sec = 50 MB/s = 400 Mbps
 * - Recovery: +400 Mbps temporarily (during recovery)
 * - Total: 720 Mbps peak (1 Gbps network sufficient)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Retransmission: Request missing packets (TCP channel)
 * - FEC: Forward error correction (recover without retransmit)
 * - Redundant feeds: Subscribe to primary + backup streams
 * - Adaptive: Adjust snapshot frequency based on loss rate
 * - Monitoring: Track packet loss, recovery time
 * - Alerting: Notify on excessive loss (network issue)
 * - Compression: LZ4 (reduce bandwidth)
 * - Filtering: Subscribe only to relevant symbols
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) TCP Market Data:
 *    - Advantage: Reliable (no packet loss)
 *    - Disadvantage: Higher latency, one-to-one (not scalable)
 *    - Use case: Non-HFT retail trading
 * 
 * B) No Snapshot (Retransmission Only):
 *    - Request: Missing packets via TCP
 *    - Advantage: Lower bandwidth (no snapshots)
 *    - Disadvantage: Delay (TCP round-trip)
 *    - Some exchanges: Offer this option
 * 
 * C) Always Subscribe Snapshot:
 *    - Subscribe: Both streams always
 *    - Advantage: Faster recovery (already subscribed)
 *    - Disadvantage: High bandwidth (wasted)
 *    - Rare: Most systems subscribe on-demand
 */
