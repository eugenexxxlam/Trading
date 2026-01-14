#pragma once

#include "common/types.h"
#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/mcast_socket.h"
#include "common/mem_pool.h"
#include "common/logging.h"

#include "market_data/market_update.h"
#include "matcher/me_order.h"

using namespace Common;

/*
 * SNAPSHOT SYNTHESIZER - FULL ORDER BOOK SNAPSHOTS
 * =================================================
 * 
 * PURPOSE:
 * Maintains full order book state and publishes periodic snapshots via multicast.
 * Allows late joiners and gap recovery for incremental market data feed.
 * 
 * PROBLEM:
 * - Incremental feed: Only order book changes (ADD, CANCEL, TRADE)
 * - Late joiner: Cannot build order book (missing initial state)
 * - Packet loss: Gap in incremental feed (missing updates)
 * - Solution: Periodic full snapshots
 * 
 * TWO-STREAM MODEL:
 * ```
 * Incremental Stream:   TRADE ... ADD ... CANCEL ... MODIFY ... (continuous)
 * Snapshot Stream:      [Full Book] -------- [Full Book] -------- [Full Book]
 *                       (every 1 sec)       (every 1 sec)       (every 1 sec)
 * ```
 * 
 * SNAPSHOT CONTENT:
 * - SNAPSHOT_START marker (begin of snapshot)
 * - All active orders (ADD messages for each live order)
 * - All price levels, all instruments
 * - SNAPSHOT_END marker (end of snapshot)
 * - Incremental sequence number (sync point)
 * 
 * RECOVERY PROCEDURE (SUBSCRIBER):
 * 1. Subscribe to both streams (incremental + snapshot)
 * 2. Buffer incremental updates (out of order)
 * 3. Wait for complete snapshot (SNAPSHOT_START -> SNAPSHOT_END)
 * 4. Build order book from snapshot
 * 5. Apply buffered incremental updates (after snapshot seq number)
 * 6. Continue processing incremental updates in real-time
 * 
 * EXAMPLE SNAPSHOT:
 * ```
 * SNAPSHOT_START (seq=12345)
 * ADD AAPL BUY 100@150.00 order_id=1
 * ADD AAPL BUY 200@149.95 order_id=2
 * ADD AAPL SELL 50@150.05 order_id=3
 * ADD MSFT BUY 100@300.00 order_id=4
 * ...
 * SNAPSHOT_END (seq=12345)
 * ```
 * 
 * ARCHITECTURE:
 * ```
 * Market Data Publisher --> [LF Queue] --> Snapshot Synthesizer
 *                                                   |
 *                                                   v
 *                                          Maintain Full Book
 *                                                   |
 *                                         (Every 1 second)
 *                                                   |
 *                                                   v
 *                                          Snapshot Multicast
 *                                          (UDP to all subscribers)
 * ```
 * 
 * RESPONSIBILITIES:
 * 1. Consume sequenced market updates (from publisher)
 * 2. Maintain full order book state (all instruments, all orders)
 * 3. Periodically publish snapshots (every N seconds/nanoseconds)
 * 4. Include incremental sequence number (sync point)
 * 
 * THREADING:
 * - Single thread (no locking)
 * - Dedicated CPU core (via affinity)
 * - Polling lock-free queue
 * - Periodic snapshot publishing
 * 
 * PERFORMANCE:
 * - Update processing: 50-100 ns per update
 * - Snapshot generation: 10-100 ms (depends on book size)
 * - Snapshot frequency: 1 second (typical)
 * - Not latency-critical (snapshot can lag)
 * 
 * MEMORY:
 * - Full order book: All active orders (up to 1M orders)
 * - Memory pool: Pre-allocated MEMarketUpdate objects
 * - Hash map: OrderId -> MEMarketUpdate* (fast lookup)
 */

namespace Exchange {
  class SnapshotSynthesizer {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - market_updates: Lock-free queue from market data publisher
     * - iface: Network interface for multicast
     * - snapshot_ip: Multicast group for snapshot stream
     * - snapshot_port: Port for snapshot stream
     * 
     * Initializes:
     * - Snapshot multicast socket
     * - Memory pool for order book (MEMarketUpdate objects)
     * - Hash map for order lookup (ticker -> order ID -> order)
     * - Snapshot timing (last_snapshot_time_)
     */
    SnapshotSynthesizer(MDPMarketUpdateLFQueue *market_updates, const std::string &iface,
                        const std::string &snapshot_ip, int snapshot_port);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleanup: Memory pool, multicast socket
     */
    ~SnapshotSynthesizer();

    /*
     * START - BEGIN SNAPSHOT THREAD
     * ==============================
     * 
     * Creates and starts snapshot synthesizer thread.
     * Calls run() method in loop.
     */
    auto start() -> void;

    /*
     * STOP - SHUTDOWN SNAPSHOT THREAD
     * ================================
     * 
     * Stops snapshot synthesizer thread gracefully.
     * Sets run_ = false, waits for thread to exit.
     */
    auto stop() -> void;

    /*
     * ADD TO SNAPSHOT - UPDATE ORDER BOOK STATE
     * ==========================================
     * 
     * Process incremental market update and maintain order book snapshot.
     * 
     * ALGORITHM:
     * 1. Switch on update type:
     *    - ADD: Create new order in snapshot
     *    - MODIFY: Update order quantity
     *    - CANCEL: Remove order from snapshot
     *    - TRADE: Update quantities (passive order reduced)
     *    - CLEAR: Remove all orders for instrument
     * 2. Update hash map: ticker_orders_[ticker_id][order_id]
     * 3. Allocate/deallocate from memory pool
     * 
     * DATA STRUCTURE:
     * - ticker_orders_: 2D array [ticker_id][order_id] -> MEMarketUpdate*
     * - nullptr: Order doesn't exist
     * - non-nullptr: Order exists (with current state)
     * 
     * ADD UPDATE:
     * - Allocate MEMarketUpdate from pool
     * - Copy update data (price, qty, side, etc.)
     * - Store in ticker_orders_[ticker_id][order_id]
     * 
     * CANCEL UPDATE:
     * - Lookup order: ticker_orders_[ticker_id][order_id]
     * - Deallocate from pool
     * - Set ticker_orders_[ticker_id][order_id] = nullptr
     * 
     * MODIFY UPDATE:
     * - Lookup order
     * - Update qty_ field
     * - Keep in place
     * 
     * COMPLEXITY:
     * - Lookup: O(1) (array indexing)
     * - Update: O(1) (direct access)
     * - Memory: O(1) (pool allocation)
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto addToSnapshot(const MDPMarketUpdate *market_update);

    /*
     * PUBLISH SNAPSHOT - BROADCAST FULL ORDER BOOK
     * =============================================
     * 
     * Publish complete order book snapshot via multicast.
     * 
     * ALGORITHM:
     * 1. Send SNAPSHOT_START marker
     *    - Includes last incremental sequence number (sync point)
     * 2. Iterate all instruments (tickers)
     * 3. For each instrument, iterate all active orders
     *    - Send ADD message for each order
     *    - Include all order details (price, qty, side, etc.)
     * 4. Send SNAPSHOT_END marker
     *    - Includes same incremental sequence number
     * 
     * SNAPSHOT MESSAGE FORMAT:
     * - Same as incremental (MDPMarketUpdate)
     * - Type: SNAPSHOT_START, ADD, SNAPSHOT_END
     * - Sequence number: Incremental seq (not separate snapshot seq)
     * 
     * SUBSCRIBER PROCESSING:
     * 1. Receive SNAPSHOT_START
     * 2. Clear local order book
     * 3. Process all ADD messages (rebuild book)
     * 4. Receive SNAPSHOT_END
     * 5. Apply buffered incremental updates (with seq > snapshot seq)
     * 6. Resume normal incremental processing
     * 
     * PERFORMANCE:
     * - Snapshot size: N orders * 48 bytes
     * - Typical: 10K orders = 480 KB
     * - Peak: 1M orders = 48 MB
     * - Send time: 10-100 ms (depends on size, network)
     * - Frequency: 1 second (not latency-critical)
     * 
     * MULTICAST:
     * - UDP multicast (same as incremental, different group/port)
     * - Broadcast to all subscribers
     * - Unreliable (packet loss possible)
     * - Retransmission: Wait for next snapshot (1 second)
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto publishSnapshot();

    /*
     * RUN - MAIN SNAPSHOT LOOP
     * =========================
     * 
     * Main loop for snapshot synthesizer thread.
     * 
     * ALGORITHM:
     * 1. Poll lock-free queue for market updates
     * 2. For each update:
     *    a. Process update (addToSnapshot)
     *    b. Update last_inc_seq_num_ (track incremental sequence)
     *    c. Consume from queue
     * 3. Check if snapshot interval elapsed (e.g., 1 second)
     * 4. If yes: Publish snapshot (publishSnapshot)
     * 5. Repeat until stopped
     * 
     * SNAPSHOT TIMING:
     * - last_snapshot_time_: Timestamp of last snapshot
     * - Interval: 1 second (or configurable)
     * - Check: getCurrentNanos() - last_snapshot_time_ > interval
     * 
     * PERFORMANCE:
     * - Update processing: 50-100 ns per update
     * - Queue polling: 10-20 ns per iteration
     * - Snapshot publishing: 10-100 ms (once per second)
     * - CPU usage: ~1-10% (not latency-critical)
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto run() -> void;

    // Deleted constructors (prevent accidental copies)
    SnapshotSynthesizer() = delete;
    SnapshotSynthesizer(const SnapshotSynthesizer &) = delete;
    SnapshotSynthesizer(const SnapshotSynthesizer &&) = delete;
    SnapshotSynthesizer &operator=(const SnapshotSynthesizer &) = delete;
    SnapshotSynthesizer &operator=(const SnapshotSynthesizer &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Lock-free queue: Sequenced updates from market data publisher
    // Input: MDPMarketUpdate (with sequence number)
    MDPMarketUpdateLFQueue *snapshot_md_updates_ = nullptr;

    // Async logger
    Logger logger_;

    // Thread control flag
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;

    // Multicast socket for snapshot stream
    // Broadcasts full order book snapshots
    McastSocket snapshot_socket_;

    // Full order book state (all instruments, all orders)
    // 2D hash map: [ticker_id][order_id] -> MEMarketUpdate*
    // - ticker_id: Which instrument (0=AAPL, 1=MSFT, etc.)
    // - order_id: Which order (1, 2, 3, ...)
    // - MEMarketUpdate*: Order details (price, qty, side, etc.)
    // - nullptr: Order doesn't exist (canceled or never added)
    // 
    // SIZE: ME_MAX_TICKERS * ME_MAX_ORDER_IDS * 8 bytes (pointer)
    // Example: 100 tickers * 1M orders * 8 bytes = 800 MB
    // Large but acceptable for full order book
    std::array<std::array<MEMarketUpdate *, ME_MAX_ORDER_IDS>, ME_MAX_TICKERS> ticker_orders_;
    
    // Last incremental sequence number processed
    // Used as sync point in snapshot (tells subscribers where to resume)
    size_t last_inc_seq_num_ = 0;
    
    // Timestamp of last published snapshot (nanoseconds)
    // Used to determine when to publish next snapshot
    Nanos last_snapshot_time_ = 0;

    // Memory pool for order objects (MEMarketUpdate)
    // Pre-allocated, O(1) allocation/deallocation
    // Capacity: ME_MAX_ORDER_IDS (e.g., 1M orders)
    MemPool<MEMarketUpdate> order_pool_;
  };
}

/*
 * SNAPSHOT SYNTHESIZER DESIGN CONSIDERATIONS
 * ===========================================
 * 
 * 1. SNAPSHOT FREQUENCY:
 *    - Trade-off: Bandwidth vs recovery time
 *    - Frequent (0.1 sec): Fast recovery, high bandwidth
 *    - Infrequent (10 sec): Slow recovery, low bandwidth
 *    - Typical: 1 second (balance)
 *    - Adaptive: More frequent during high volatility
 * 
 * 2. SNAPSHOT SIZE:
 *    - Depends on: Number of active orders
 *    - Small (100 orders): ~5 KB
 *    - Medium (10K orders): ~500 KB
 *    - Large (1M orders): ~50 MB
 *    - Compression: Can reduce by 50-90% (production)
 * 
 * 3. MEMORY POOL:
 *    - Pre-allocate: ME_MAX_ORDER_IDS orders
 *    - Example: 1M orders * 40 bytes = 40 MB
 *    - O(1) allocation/deallocation
 *    - No heap fragmentation
 *    - Predictable performance
 * 
 * 4. HASH MAP SIZE:
 *    - ticker_orders_: 2D array of pointers
 *    - Size: tickers * orders * 8 bytes
 *    - Example: 100 * 1M * 8 = 800 MB
 *    - Large but manageable
 *    - Alternative: Sparse hash map (less memory, more complex)
 * 
 * 5. SNAPSHOT COMPRESSION:
 *    - Uncompressed: 40 bytes per order
 *    - DEFLATE: ~10-20 bytes per order (50-75% reduction)
 *    - LZ4: ~20-30 bytes per order (fast, less compression)
 *    - Production: LZ4 (balance speed and size)
 * 
 * 6. CONFLATION:
 *    - Problem: Multiple updates to same order between snapshots
 *    - Solution: Only publish final state (conflation)
 *    - Already done: Snapshot reflects current state only
 * 
 * LATENCY vs RECOVERY TIME:
 * - Incremental feed: 1-10 μs latency (critical path)
 * - Snapshot feed: 10-100 ms latency (non-critical)
 * - Recovery time: Up to 1 second (wait for snapshot)
 * - Acceptable: Late joiners can wait, traders use incremental
 * 
 * BANDWIDTH CALCULATION:
 * - Snapshot size: 10K orders * 48 bytes = 480 KB
 * - Frequency: 1 per second
 * - Bandwidth: 480 KB/s = 3.8 Mbps
 * - Plus incremental: ~320 Mbps
 * - Total: ~324 Mbps (1 Gbps network sufficient)
 * 
 * SUBSCRIBER RECOVERY ALGORITHM:
 * ```cpp
 * // 1. Subscribe to both streams
 * McastSocket incremental_socket, snapshot_socket;
 * incremental_socket.join(incremental_ip, incremental_port);
 * snapshot_socket.join(snapshot_ip, snapshot_port);
 * 
 * // 2. Buffer incremental updates (out of order)
 * std::queue<MDPMarketUpdate> buffered_updates;
 * 
 * // 3. Wait for snapshot
 * MDPMarketUpdate update;
 * while (true) {
 *   snapshot_socket.recv(&update, sizeof(update));
 *   if (update.me_market_update_.type_ == MarketUpdateType::SNAPSHOT_START) {
 *     size_t snapshot_seq = update.seq_num_;
 *     order_book.clear();  // Clear local order book
 *     
 *     // 4. Process snapshot
 *     while (true) {
 *       snapshot_socket.recv(&update, sizeof(update));
 *       if (update.me_market_update_.type_ == MarketUpdateType::SNAPSHOT_END) {
 *         break;  // Snapshot complete
 *       }
 *       order_book.add(update);  // Add order to local book
 *     }
 *     
 *     // 5. Apply buffered incremental updates (after snapshot)
 *     while (!buffered_updates.empty()) {
 *       auto& buffered = buffered_updates.front();
 *       if (buffered.seq_num_ > snapshot_seq) {
 *         order_book.process(buffered);
 *       }
 *       buffered_updates.pop();
 *     }
 *     
 *     break;  // Ready for real-time processing
 *   }
 * }
 * 
 * // 6. Resume normal incremental processing
 * while (true) {
 *   incremental_socket.recv(&update, sizeof(update));
 *   order_book.process(update);
 * }
 * ```
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Compression: LZ4, DEFLATE (reduce bandwidth)
 * - Incremental snapshots: Only changes since last snapshot
 * - Snapshot caching: Store on disk (faster recovery)
 * - Redundancy: Multiple snapshot sources (failover)
 * - Delta snapshots: Full + incremental deltas
 * - Book depth limits: Top N levels only (reduce size)
 * - Snapshot on demand: Client requests (not periodic)
 * - Snapshot retransmission: Separate recovery channel
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) No Snapshots:
 *    - Only incremental feed
 *    - Late joiners: Cannot recover
 *    - Gap: Must disconnect and reconnect
 *    - Not suitable for public market data
 * 
 * B) Snapshot on Request:
 *    - Client requests snapshot (separate TCP channel)
 *    - Advantages: No bandwidth waste, faster recovery
 *    - Disadvantages: More complex, server state
 *    - Suitable: Private feeds, few clients
 * 
 * C) State Machine Replication:
 *    - Replay all messages from beginning
 *    - Advantages: Perfect consistency
 *    - Disadvantages: Slow (thousands of messages)
 *    - Not suitable for real-time trading
 * 
 * INDUSTRY PRACTICES:
 * - NASDAQ ITCH: Snapshot every 1 second
 * - CME MDP 3.0: Snapshot every 1 second, incremental feed
 * - NYSE PILLAR: Snapshot + incremental
 * - Most exchanges: 1 second snapshot interval
 * - Some: Snapshot on demand (TCP channel)
 * 
 * PERFORMANCE TUNING:
 * - Snapshot interval: Adjust based on load
 * - Compression: Enable if bandwidth limited
 * - Book depth: Limit to top N levels (reduce size)
 * - Conflation: Aggregate updates (reduce messages)
 * - Batching: Multiple updates per packet
 * - Memory pool: Pre-allocate sufficient capacity
 * 
 * TESTING:
 * - Packet loss: Verify gap detection and recovery
 * - Late join: Verify snapshot recovery
 * - High load: Verify snapshot under 1M orders
 * - Compression: Verify correctness and performance
 * - Stress test: 10M updates/second
 */
