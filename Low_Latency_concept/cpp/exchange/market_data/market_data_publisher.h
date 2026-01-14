#pragma once

#include <functional>

#include "market_data/snapshot_synthesizer.h"

/*
 * MARKET DATA PUBLISHER - MULTICAST MARKET DATA DISTRIBUTION
 * ===========================================================
 * 
 * PURPOSE:
 * Broadcasts order book changes to all market participants via UDP multicast.
 * Provides real-time market data feed for traders, market makers, data vendors.
 * 
 * RESPONSIBILITIES:
 * 1. Consume market updates from matching engine (lock-free queue)
 * 2. Add sequence numbers (gap detection, retransmission)
 * 3. Broadcast via UDP multicast (incremental feed)
 * 4. Forward to snapshot synthesizer (periodic full book snapshots)
 * 
 * TWO MULTICAST STREAMS:
 * 
 * 1. INCREMENTAL STREAM:
 *    - Real-time order book changes
 *    - ADD, CANCEL, MODIFY, TRADE updates
 *    - Sequence numbered (1, 2, 3, ...)
 *    - High frequency (1M+ messages/second)
 *    - Subscribers build order book from incremental updates
 * 
 * 2. SNAPSHOT STREAM:
 *    - Periodic full order book state
 *    - Sent every N seconds (e.g., 1 second)
 *    - Allows late joiners to sync
 *    - Allows gap recovery (missed incrementals)
 *    - Handled by SnapshotSynthesizer component
 * 
 * ARCHITECTURE:
 * ```
 * Matching Engine --> [LF Queue] --> Market Data Publisher
 *                                          |
 *                    +---------------------+---------------------+
 *                    |                                           |
 *                    v                                           v
 *             Incremental Multicast               [LF Queue] --> Snapshot Synthesizer
 *             (UDP to all subscribers)                               |
 *                                                                    v
 *                                                          Snapshot Multicast
 *                                                          (UDP to all subscribers)
 * ```
 * 
 * MULTICAST BENEFITS:
 * - One sender, many receivers (efficient)
 * - No per-subscriber overhead
 * - Network-level fanout (switch/router)
 * - Scales to thousands of subscribers
 * - Industry standard (NASDAQ ITCH, CME MDP 3.0)
 * 
 * SEQUENCE NUMBERS:
 * - Monotonically increasing (1, 2, 3, ...)
 * - Allows gap detection (received seq 100, 102 -> missing 101)
 * - Subscribers request retransmission (separate TCP channel, not shown)
 * - Or wait for next snapshot (recover from full state)
 * 
 * RELIABILITY:
 * - UDP: Unreliable (packets can be lost)
 * - Sequence numbers: Detect losses
 * - Snapshots: Periodic recovery mechanism
 * - Retransmission: Request missing messages (production feature)
 * 
 * THREADING:
 * - Single thread (no locking)
 * - Dedicated CPU core (via affinity)
 * - Polling lock-free queue
 * - Non-blocking UDP send
 * 
 * PERFORMANCE:
 * - Consume from queue: 10-20 ns
 * - Add sequence number: 1-5 ns
 * - UDP multicast send: 1-10 μs
 * - Total: 1-10 μs per update
 * - Throughput: 1M+ updates/second
 */

namespace Exchange {
  class MarketDataPublisher {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - market_updates: Lock-free queue from matching engine
     * - iface: Network interface for multicast (e.g., "eth0")
     * - snapshot_ip: Multicast group for snapshot stream
     * - snapshot_port: Port for snapshot stream
     * - incremental_ip: Multicast group for incremental stream
     * - incremental_port: Port for incremental stream
     * 
     * Initializes:
     * - Incremental multicast socket
     * - Snapshot synthesizer (separate component + thread)
     * - Sequence number counter (starts at 1)
     */
    MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                        const std::string &snapshot_ip, int snapshot_port,
                        const std::string &incremental_ip, int incremental_port);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleanup:
     * 1. Stop publishing (set run_ = false)
     * 2. Wait 5 seconds (drain queues, finish publishing)
     * 3. Delete snapshot synthesizer
     * 
     * Sleep: Allows in-flight messages to complete
     */
    ~MarketDataPublisher() {
      stop();  // Stop threads

      // Wait for in-flight messages to complete
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);

      // Cleanup snapshot synthesizer
      delete snapshot_synthesizer_;
      snapshot_synthesizer_ = nullptr;
    }

    /*
     * START - BEGIN PUBLISHER THREADS
     * ================================
     * 
     * Starts two threads:
     * 1. Market data publisher thread (this class)
     * 2. Snapshot synthesizer thread (nested component)
     * 
     * ALGORITHM:
     * 1. Set run_ = true
     * 2. Create thread, call run() method
     * 3. Start snapshot synthesizer thread
     * 
     * CPU AFFINITY:
     * - -1: No specific core (let OS decide)
     * - Production: Pin to dedicated core
     */
    auto start() {
      run_ = true;  // Enable main loop

      // Create and start market data publisher thread
      ASSERT(Common::createAndStartThread(-1, "Exchange/MarketDataPublisher", [this]() { run(); }) != nullptr, "Failed to start MarketData thread.");

      // Start snapshot synthesizer thread
      snapshot_synthesizer_->start();
    }

    /*
     * STOP - SHUTDOWN PUBLISHER
     * ==========================
     * 
     * Stops both publisher threads gracefully.
     * 
     * ALGORITHM:
     * 1. Set run_ = false (stop main loop)
     * 2. Stop snapshot synthesizer
     * 3. Threads exit naturally
     */
    auto stop() -> void {
      run_ = false;  // Disable main loop
      snapshot_synthesizer_->stop();  // Stop snapshot thread
    }

    /*
     * RUN - MAIN PUBLISHING LOOP
     * ===========================
     * 
     * Main loop for market data publisher thread.
     * Continuously polls for market updates and publishes.
     * 
     * ALGORITHM:
     * 1. Poll lock-free queue for market updates
     * 2. If update available:
     *    a. Add sequence number (next_inc_seq_num_++)
     *    b. Publish to incremental multicast stream
     *    c. Forward to snapshot synthesizer (via lock-free queue)
     *    d. Update read index (consume from queue)
     * 3. Repeat until stopped
     * 
     * SEQUENCING:
     * - next_inc_seq_num_: Starts at 1, increments for each update
     * - Added to MEMarketUpdate -> MDPMarketUpdate
     * - Allows subscribers to detect gaps
     * 
     * MULTICAST SEND:
     * - incremental_socket_.send(): UDP multicast
     * - Non-blocking (returns immediately)
     * - No ACK (fire-and-forget)
     * - 1-10 μs latency (network + NIC)
     * 
     * SNAPSHOT FORWARDING:
     * - snapshot_md_updates_: Lock-free queue to synthesizer
     * - Synthesizer maintains full order book
     * - Publishes snapshots periodically
     * 
     * PERFORMANCE:
     * - Queue read: 10-20 ns
     * - Sequence numbering: 1-5 ns
     * - UDP send: 1-10 μs (dominates)
     * - Queue write: 10-20 ns
     * - Total: 1-10 μs per update
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto run() noexcept -> void;

    // Deleted constructors (prevent accidental copies)
    MarketDataPublisher() = delete;
    MarketDataPublisher(const MarketDataPublisher &) = delete;
    MarketDataPublisher(const MarketDataPublisher &&) = delete;
    MarketDataPublisher &operator=(const MarketDataPublisher &) = delete;
    MarketDataPublisher &operator=(const MarketDataPublisher &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Sequence number for incremental stream
    // Starts at 1, increments for each update
    // Used for gap detection
    size_t next_inc_seq_num_ = 1;

    // Lock-free queue: Market updates from matching engine
    // Input: MEMarketUpdate (internal format, no sequence number)
    MEMarketUpdateLFQueue *outgoing_md_updates_ = nullptr;

    // Lock-free queue: Sequenced updates to snapshot synthesizer
    // Output: MDPMarketUpdate (with sequence number)
    MDPMarketUpdateLFQueue snapshot_md_updates_;

    // Thread control flag
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger
    Logger logger_;

    // Multicast socket for incremental stream
    // Broadcasts order book changes to all subscribers
    Common::McastSocket incremental_socket_;

    // Snapshot synthesizer component (separate thread)
    // Maintains full order book, publishes snapshots periodically
    SnapshotSynthesizer *snapshot_synthesizer_ = nullptr;
  };
}

/*
 * MARKET DATA PUBLISHING BEST PRACTICES
 * ======================================
 * 
 * 1. MULTICAST vs UNICAST:
 *    - Multicast: One-to-many efficiently
 *    - Unicast: One-to-one (N subscribers = N sends)
 *    - Multicast advantage: Network fanout, scales to thousands
 *    - Industry standard: All major exchanges use multicast
 * 
 * 2. UDP vs TCP:
 *    - UDP: Low latency, connectionless, unreliable
 *    - TCP: Higher latency, connection overhead, reliable
 *    - Market data: UDP preferred (latency critical)
 *    - Reliability: Sequence numbers + snapshots
 * 
 * 3. SEQUENCE NUMBERING:
 *    - Critical: Detect packet loss
 *    - Per-stream: Incremental and snapshot separate sequences
 *    - Monotonic: Never decreases or repeats
 *    - Gap handling: Request retransmission or wait for snapshot
 * 
 * 4. SNAPSHOT FREQUENCY:
 *    - Too frequent: Bandwidth waste, subscriber overhead
 *    - Too infrequent: Slow gap recovery
 *    - Typical: 1 snapshot per second
 *    - Adaptive: More frequent during high volatility
 * 
 * 5. RETRANSMISSION:
 *    - Separate TCP channel (not shown in this implementation)
 *    - Subscriber detects gap, requests missing messages
 *    - Publisher maintains recent history (cache)
 *    - Timeout: Fall back to next snapshot
 * 
 * 6. MESSAGE ORDERING:
 *    - Subscribers process in sequence number order
 *    - Buffer out-of-order messages (UDP can reorder)
 *    - Timeout: Gap or lost packet
 * 
 * LATENCY BREAKDOWN:
 * - Queue read: 10-20 ns (lock-free)
 * - Add sequence number: 1-5 ns
 * - Serialize: 0 ns (binary, no serialization needed)
 * - UDP send: 1-10 μs (kernel + network + NIC)
 * - Network propagation: 0.1-1 μs (LAN, co-located)
 * - TOTAL: 1-11 μs (matching engine to subscriber)
 * 
 * THROUGHPUT:
 * - Typical: 100K-1M updates/second
 * - Peak: 5M-10M updates/second (extreme volatility)
 * - Bandwidth: ~40 bytes/update * 1M = 40 MB/s = 320 Mbps
 * - Network: 1 Gbps sufficient, 10 Gbps for peak
 * 
 * SUBSCRIBER DESIGN:
 * ```cpp
 * // Join multicast group
 * mcast_socket.join("239.0.0.1", 12345);
 * 
 * // Receive loop
 * size_t expected_seq = 1;
 * while (true) {
 *   MDPMarketUpdate update;
 *   mcast_socket.recv(&update, sizeof(update));
 *   
 *   if (update.seq_num_ != expected_seq) {
 *     // Gap detected (lost packet)
 *     requestRetransmission(expected_seq, update.seq_num_ - 1);
 *     // Or wait for next snapshot
 *   }
 *   
 *   processUpdate(update);  // Update local order book
 *   expected_seq = update.seq_num_ + 1;
 * }
 * ```
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Retransmission server: TCP channel for gap recovery
 * - Message batching: Multiple updates per packet (reduce overhead)
 * - Compression: DEFLATE, LZ4 (reduce bandwidth)
 * - Encryption: Secure multicast (IPsec, DTLS)
 * - Heartbeats: Periodic empty messages (liveness check)
 * - Redundant feeds: Primary and secondary (failover)
 * - FEC: Forward error correction (recover without retransmit)
 * - Traffic shaping: Rate limiting (prevent network congestion)
 * - Priority queues: Market orders before limit orders
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Unicast per Subscriber:
 *    - Simple: One TCP/UDP per subscriber
 *    - Doesn't scale: N subscribers = N sends = N × latency
 *    - Bandwidth: N × message size
 *    - Not suitable for public market data
 * 
 * B) Publish-Subscribe Middleware:
 *    - Examples: ZeroMQ, RabbitMQ, Kafka
 *    - Advantages: Rich features, reliability, routing
 *    - Disadvantages: Higher latency (10-100 μs), complexity
 *    - Not suitable for ultra-low-latency HFT
 * 
 * C) Kernel Bypass (DPDK, Solarflare):
 *    - Bypass kernel UDP stack
 *    - Lower latency (sub-microsecond)
 *    - Complex: Custom UDP implementation
 *    - Expensive: Specialized NICs
 *    - Used in: Ultra-low-latency HFT
 * 
 * INDUSTRY PROTOCOLS:
 * - NASDAQ ITCH: Binary protocol, multicast, sequence numbers
 * - CME MDP 3.0: Multicast, snapshot + incremental, FIX/SBE
 * - NYSE PILLAR: Binary, multicast, high throughput
 * - LSE UTP: Multicast, snapshot + incremental
 * - This implementation: Simplified version for education
 * 
 * REGULATORY REQUIREMENTS:
 * - MiFID II: Provide pre/post-trade data to public
 * - Reg NMS: Fair and equal access to market data
 * - SEC CAT: Consolidated audit trail
 * - Latency: Microsecond timestamps mandatory
 * - Audit: Log every published message
 * 
 * PERFORMANCE OPTIMIZATION:
 * - Zero-copy: Avoid memory allocation/copying
 * - Binary protocol: No serialization overhead
 * - Lock-free queues: No synchronization overhead
 * - CPU affinity: Dedicated core, no context switches
 * - Non-blocking I/O: No syscall blocking
 * - Batch sends: Multiple messages per syscall (production)
 * - Kernel bypass: Sub-microsecond latency (advanced)
 */
