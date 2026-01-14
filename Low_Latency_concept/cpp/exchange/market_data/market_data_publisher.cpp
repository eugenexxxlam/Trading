#include "market_data_publisher.h"

/*
 * MARKET DATA PUBLISHER IMPLEMENTATION
 * =====================================
 * 
 * Implementation of MarketDataPublisher constructor and run() loop.
 * Broadcasts order book changes via UDP multicast to all subscribers.
 * 
 * See market_data_publisher.h for detailed class documentation and architecture.
 */

namespace Exchange {
  /*
   * CONSTRUCTOR - INITIALIZE MARKET DATA PUBLISHER
   * ===============================================
   * 
   * Sets up multicast sockets and snapshot synthesizer.
   * 
   * ALGORITHM:
   * 1. Store queue pointer (market updates from matching engine)
   * 2. Initialize snapshot queue (to snapshot synthesizer)
   * 3. Initialize logger ("exchange_market_data_publisher.log")
   * 4. Initialize incremental multicast socket
   * 5. Create snapshot synthesizer (separate component + thread)
   * 
   * INCREMENTAL SOCKET:
   * - UDP multicast socket
   * - Group: incremental_ip (e.g., "239.0.0.1")
   * - Port: incremental_port (e.g., 20000)
   * - Interface: iface (e.g., "eth0")
   * - is_listening: false (sending only, not receiving)
   * 
   * SNAPSHOT SYNTHESIZER:
   * - Separate component (maintains full order book)
   * - Heap allocated (delete in destructor)
   * - Own thread (started in start() method)
   * - Receives updates via snapshot_md_updates_ queue
   * 
   * SNAPSHOT QUEUE:
   * - Lock-free queue (SPSC)
   * - Capacity: ME_MAX_MARKET_UPDATES (e.g., 256K)
   * - Publisher (this) writes, synthesizer reads
   * 
   * ERROR HANDLING:
   * - ASSERT: Multicast socket initialization must succeed
   * - errno: System error code (from socket() syscall)
   * - Fatal if fails: Cannot broadcast market data
   * 
   * Parameters:
   * - market_updates: Lock-free queue from matching engine
   * - iface: Network interface for multicast
   * - snapshot_ip: Multicast group for snapshot stream
   * - snapshot_port: Port for snapshot stream
   * - incremental_ip: Multicast group for incremental stream
   * - incremental_port: Port for incremental stream
   */
  MarketDataPublisher::MarketDataPublisher(MEMarketUpdateLFQueue *market_updates, const std::string &iface,
                                           const std::string &snapshot_ip, int snapshot_port,
                                           const std::string &incremental_ip, int incremental_port)
      : outgoing_md_updates_(market_updates),                    // Queue from matching engine
        snapshot_md_updates_(ME_MAX_MARKET_UPDATES),             // Queue to snapshot synthesizer
        run_(false),                                              // Thread not started yet
        logger_("exchange_market_data_publisher.log"),           // Initialize logger
        incremental_socket_(logger_) {                           // Initialize incremental socket
    
    // Initialize incremental multicast socket (for broadcasting)
    // Parameters: multicast group, interface, port, is_listening=false (send only)
    ASSERT(incremental_socket_.init(incremental_ip, iface, incremental_port, /*is_listening*/ false) >= 0,
           "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));
    
    // Create snapshot synthesizer (separate component)
    // Heap allocated (delete in destructor)
    snapshot_synthesizer_ = new SnapshotSynthesizer(&snapshot_md_updates_, iface, snapshot_ip, snapshot_port);
  }

  /*
   * RUN - MAIN PUBLISHING LOOP
   * ===========================
   * 
   * Main loop for market data publisher thread.
   * Consumes market updates, adds sequence numbers, broadcasts.
   * 
   * ALGORITHM:
   * 1. Log thread start
   * 2. While run_ is true:
   *    a. Poll outgoing_md_updates_ queue
   *    b. For each market update:
   *       - Record timestamp (T5)
   *       - Log update (debugging)
   *       - Send sequence number (8 bytes)
   *       - Send market update (MEMarketUpdate, ~40 bytes)
   *       - Record timestamp (T6)
   *       - Forward to snapshot synthesizer (with sequence number)
   *       - Increment sequence number
   *    c. Flush multicast socket (sendAndRecv)
   * 
   * TWO UDP SENDS:
   * - First send: Sequence number (size_t, 8 bytes)
   * - Second send: Market update (MEMarketUpdate, ~40 bytes)
   * - Reason: Separate fields (could combine in production)
   * - Total: 48 bytes per update
   * 
   * SEQUENCE NUMBERING:
   * - next_inc_seq_num_: Starts at 1, increments for each update
   * - Sent before update (subscribers read seq# first)
   * - Allows gap detection (received seq 100, 102 -> missing 101)
   * 
   * MULTICAST SEND:
   * - incremental_socket_.send(): Non-blocking UDP send
   * - Buffered in kernel (not sent immediately)
   * - incremental_socket_.sendAndRecv(): Flush buffer (actual send)
   * - 1-10 μs latency (kernel + network + NIC)
   * 
   * SNAPSHOT FORWARDING:
   * - Two-step write to snapshot_md_updates_ queue:
   *   1. getNextToWriteTo(): Reserve slot
   *   2. Fill MDPMarketUpdate (seq# + update)
   *   3. updateWriteIndex(): Commit (publish to synthesizer)
   * - Snapshot synthesizer: Maintains full order book, publishes snapshots
   * 
   * PERFORMANCE MEASUREMENT:
   * - T5: Publisher reads from queue
   * - T6: Publisher writes to multicast socket
   * - START_MEASURE / END_MEASURE: Socket send latency
   * 
   * BUSY-WAIT:
   * - Continuously polls queue (even if empty)
   * - 100% CPU utilization (acceptable for HFT)
   * - Low latency (no sleep, no blocking)
   * 
   * LOOP EXIT:
   * - run_ = false (set by stop() method)
   * - Loop exits naturally
   * - Thread terminates
   */
  auto MarketDataPublisher::run() noexcept -> void {
    // Log thread start
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
    
    // Main publishing loop
    while (run_) {
      // Poll for market updates (non-blocking)
      // Loop while queue has data (outgoing_md_updates_->size() > 0)
      for (auto market_update = outgoing_md_updates_->getNextToRead();
           outgoing_md_updates_->size() && market_update; 
           market_update = outgoing_md_updates_->getNextToRead()) {
        
        // Record timestamp (T5 = publisher queue read)
        TTT_MEASURE(T5_MarketDataPublisher_LFQueue_read, logger_);

        // Log update (debugging)
        logger_.log("%:% %() % Sending seq:% %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), 
                    next_inc_seq_num_, market_update->toString().c_str());

        // Send to incremental multicast stream (two UDP sends)
        START_MEASURE(Exchange_McastSocket_send);  // Begin send measurement
        
        // Send sequence number (8 bytes)
        incremental_socket_.send(&next_inc_seq_num_, sizeof(next_inc_seq_num_));
        
        // Send market update (MEMarketUpdate, ~40 bytes)
        incremental_socket_.send(market_update, sizeof(MEMarketUpdate));
        
        END_MEASURE(Exchange_McastSocket_send, logger_);  // End send measurement

        // Consume from queue (advance read index)
        outgoing_md_updates_->updateReadIndex();
        
        // Record timestamp (T6 = publisher UDP write)
        TTT_MEASURE(T6_MarketDataPublisher_UDP_write, logger_);

        // Forward to snapshot synthesizer (via lock-free queue)
        auto next_write = snapshot_md_updates_.getNextToWriteTo();  // Reserve slot
        next_write->seq_num_ = next_inc_seq_num_;                    // Add sequence number
        next_write->me_market_update_ = *market_update;              // Copy update
        snapshot_md_updates_.updateWriteIndex();                     // Commit (publish)

        // Increment sequence number (1, 2, 3, ...)
        ++next_inc_seq_num_;
      }

      // Flush multicast socket (actual UDP send to network)
      // Sends buffered data from kernel to network
      incremental_socket_.sendAndRecv();
    }
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. TWO UDP SENDS:
 *    - Why: Separate sequence number and update
 *    - Production: Could combine into single packet (struct with both fields)
 *    - Advantage: One syscall (faster)
 *    - Current: Two sends (simpler code, small overhead)
 * 
 * 2. MULTICAST SOCKET:
 *    - Common::McastSocket class (see common/mcast_socket.h)
 *    - UDP multicast (one-to-many)
 *    - Non-blocking I/O
 *    - Kernel buffering (send() queues, sendAndRecv() flushes)
 * 
 * 3. SNAPSHOT SYNTHESIZER:
 *    - Separate thread, separate component
 *    - Maintains full order book state
 *    - Publishes snapshots periodically (e.g., 1 second)
 *    - Allows late joiners and gap recovery
 * 
 * 4. SEQUENCE NUMBER:
 *    - Starts at 1 (not 0)
 *    - Monotonically increasing
 *    - Never resets (session-based)
 *    - Production: Could wrap at 2^64 (unlikely)
 * 
 * 5. NOEXCEPT:
 *    - run() marked noexcept (no exceptions)
 *    - Performance: No exception handling overhead
 *    - Hot path: Critical for latency
 * 
 * 6. BUSY-WAIT POLLING:
 *    - Continuously checks queue (even if empty)
 *    - Trade-off: High CPU usage for low latency
 *    - Acceptable: Dedicated core, HFT priority
 * 
 * LATENCY BREAKDOWN:
 * - Queue read: 10-20 ns (lock-free)
 * - Log (async): 0 ns (off path)
 * - UDP send (2 calls): 1-5 μs (kernel + buffering)
 * - Queue write (snapshot): 10-20 ns
 * - Sequence increment: 1 ns
 * - sendAndRecv (flush): 1-5 μs (actual network send)
 * - TOTAL: 2-10 μs per update
 * 
 * THROUGHPUT:
 * - Typical: 100K-1M updates/second
 * - Peak: 5M-10M updates/second (extreme)
 * - Bandwidth: 48 bytes * 1M = 48 MB/s = 384 Mbps
 * - Network: 1 Gbps sufficient, 10 Gbps for peak
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Combined packet: Seq# + update in single struct (one send)
 * - Batching: Multiple updates per packet (reduce overhead)
 * - Compression: LZ4, DEFLATE (reduce bandwidth)
 * - Redundancy: Duplicate streams (failover)
 * - FEC: Forward error correction (recover without retransmit)
 * - Rate limiting: Prevent network congestion
 * - Kernel bypass: DPDK, Solarflare (sub-microsecond)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Single Packet Structure:
 *    ```cpp
 *    struct MDPMarketUpdateWire {
 *      size_t seq_num;
 *      MEMarketUpdate update;
 *    };
 *    MDPMarketUpdateWire wire = {seq_num, *market_update};
 *    incremental_socket_.send(&wire, sizeof(wire));  // One send
 *    ```
 *    Advantage: Fewer syscalls (faster)
 *    Current design: Two sends (simpler, small overhead)
 * 
 * B) Batching Multiple Updates:
 *    ```cpp
 *    struct MDPMarketUpdateBatch {
 *      size_t count;
 *      MDPMarketUpdate updates[100];
 *    };
 *    ```
 *    Advantage: Fewer packets (less overhead)
 *    Disadvantage: Higher latency (wait for batch)
 *    Use case: High throughput, not ultra-low latency
 * 
 * C) Kernel Bypass (DPDK):
 *    - Bypass kernel TCP/UDP stack
 *    - Direct NIC access from user space
 *    - Sub-microsecond latency
 *    - Complex: Custom UDP implementation
 *    - Expensive: Specialized NICs
 *    - Use case: Ultra-low-latency HFT
 */
