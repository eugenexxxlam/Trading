#include "snapshot_synthesizer.h"

/*
 * SNAPSHOT SYNTHESIZER IMPLEMENTATION
 * ====================================
 * 
 * Implementation of SnapshotSynthesizer: maintains full order book and publishes snapshots.
 * Allows subscribers to recover from packet loss or late joins.
 * 
 * See snapshot_synthesizer.h for detailed class documentation and architecture.
 */

namespace Exchange {
  /*
   * CONSTRUCTOR - INITIALIZE SNAPSHOT SYNTHESIZER
   * ==============================================
   * 
   * Sets up snapshot multicast socket and full order book state.
   * 
   * ALGORITHM:
   * 1. Store queue pointer (sequenced updates from publisher)
   * 2. Initialize logger ("exchange_snapshot_synthesizer.log")
   * 3. Initialize snapshot multicast socket
   * 4. Initialize memory pool (for order book orders)
   * 5. Initialize order book hash map (all nullptr)
   * 
   * ORDER POOL:
   * - MemPool<MEMarketUpdate>: Pre-allocate space for all orders
   * - Capacity: ME_MAX_ORDER_IDS (e.g., 1M orders)
   * - Size: 1M * 40 bytes = 40 MB
   * - O(1) allocation/deallocation
   * 
   * ORDER BOOK INITIALIZATION:
   * - ticker_orders_: 2D array [ticker_id][order_id] -> MEMarketUpdate*
   * - All entries initialized to nullptr (no orders yet)
   * - Size: ME_MAX_TICKERS * ME_MAX_ORDER_IDS * 8 bytes = ~800 MB
   * - Large but necessary for full order book
   * 
   * SNAPSHOT SOCKET:
   * - UDP multicast socket
   * - Group: snapshot_ip (e.g., "239.0.0.2")
   * - Port: snapshot_port (e.g., 20001)
   * - Interface: iface (e.g., "eth0")
   * - is_listening: false (sending only)
   * 
   * ERROR HANDLING:
   * - ASSERT: Socket initialization must succeed
   * - errno: System error code
   * - Fatal if fails: Cannot publish snapshots
   * 
   * Parameters:
   * - market_updates: Lock-free queue from market data publisher
   * - iface: Network interface for multicast
   * - snapshot_ip: Multicast group for snapshot stream
   * - snapshot_port: Port for snapshot stream
   */
  SnapshotSynthesizer::SnapshotSynthesizer(MDPMarketUpdateLFQueue *market_updates, const std::string &iface,
                                           const std::string &snapshot_ip, int snapshot_port)
      : snapshot_md_updates_(market_updates),                    // Queue from publisher
        logger_("exchange_snapshot_synthesizer.log"),            // Initialize logger
        snapshot_socket_(logger_),                               // Initialize snapshot socket
        order_pool_(ME_MAX_ORDER_IDS) {                         // Initialize memory pool
    
    // Initialize snapshot multicast socket
    ASSERT(snapshot_socket_.init(snapshot_ip, iface, snapshot_port, /*is_listening*/ false) >= 0,
           "Unable to create snapshot mcast socket. error:" + std::string(std::strerror(errno)));
    
    // Initialize full order book (all instruments, all orders to nullptr)
    // ticker_orders_[ticker_id][order_id] = nullptr (no orders yet)
    for(auto& orders : ticker_orders_)
      orders.fill(nullptr);  // Fill all order IDs with nullptr
  }

  /*
   * DESTRUCTOR - CLEANUP SNAPSHOT SYNTHESIZER
   * ==========================================
   * 
   * Stops thread and cleans up resources.
   * 
   * ALGORITHM:
   * - Call stop() to set run_ = false
   * - Thread exits naturally
   * - Memory pool auto-cleanup (RAII)
   */
  ~SnapshotSynthesizer() {
    stop();  // Stop thread
  }

  /*
   * START - BEGIN SNAPSHOT THREAD
   * ==============================
   * 
   * Creates and starts snapshot synthesizer thread.
   * 
   * ALGORITHM:
   * 1. Set run_ = true (enable main loop)
   * 2. Create thread with run() method
   * 3. Assert thread creation succeeded
   * 
   * THREAD:
   * - Name: "Exchange/SnapshotSynthesizer"
   * - CPU affinity: -1 (any core)
   * - Production: Could pin to dedicated core
   * - Lambda: [this]() { run(); }
   */
  void SnapshotSynthesizer::start() {
    run_ = true;  // Enable main loop
    
    // Create and start thread
    ASSERT(Common::createAndStartThread(-1, "Exchange/SnapshotSynthesizer", [this]() { run(); }) != nullptr,
           "Failed to start SnapshotSynthesizer thread.");
  }

  /*
   * STOP - SHUTDOWN SNAPSHOT THREAD
   * ================================
   * 
   * Stops snapshot synthesizer thread gracefully.
   * 
   * ALGORITHM:
   * - Set run_ = false
   * - Main loop (run()) checks flag and exits
   * - Thread terminates naturally
   */
  void SnapshotSynthesizer::stop() {
    run_ = false;  // Disable main loop
  }

  /*
   * ADD TO SNAPSHOT - UPDATE ORDER BOOK STATE
   * ==========================================
   * 
   * Process incremental market update and maintain snapshot order book.
   * 
   * ALGORITHM:
   * 1. Extract internal update (me_market_update_)
   * 2. Get order array for instrument (ticker_orders_[ticker_id])
   * 3. Switch on update type:
   *    - ADD: Create new order in snapshot
   *    - MODIFY: Update existing order (price, qty)
   *    - CANCEL: Remove order from snapshot
   *    - TRADE, CLEAR, SNAPSHOT_*: Ignore (don't affect snapshot)
   * 4. Validate sequence number (incremental)
   * 5. Update last_inc_seq_num_ (track incremental stream position)
   * 
   * ADD UPDATE:
   * - Lookup order: orders[order_id]
   * - Assert: Order must not exist (nullptr)
   * - Allocate from pool: order_pool_.allocate(me_market_update)
   * - Store: orders[order_id] = new_order
   * - Result: Order now in snapshot
   * 
   * MODIFY UPDATE:
   * - Lookup order: orders[order_id]
   * - Assert: Order must exist (not nullptr)
   * - Assert: order_id and side match (consistency check)
   * - Update: order->qty_, order->price_
   * - Result: Order quantity/price updated
   * 
   * CANCEL UPDATE:
   * - Lookup order: orders[order_id]
   * - Assert: Order must exist (not nullptr)
   * - Assert: order_id and side match
   * - Deallocate: order_pool_.deallocate(order)
   * - Clear: orders[order_id] = nullptr
   * - Result: Order removed from snapshot
   * 
   * IGNORED UPDATES:
   * - TRADE: Already reflected in MODIFY (passive order qty reduced)
   * - CLEAR: Only used in snapshot publishing (not incremental)
   * - SNAPSHOT_START/END: Markers (not order book changes)
   * - INVALID: Error condition
   * 
   * SEQUENCE NUMBER VALIDATION:
   * - last_inc_seq_num_: Last processed sequence number
   * - Expected: market_update->seq_num_ = last_inc_seq_num_ + 1
   * - Assert: Sequence numbers must be consecutive (no gaps)
   * - Purpose: Detect lost messages from publisher
   * 
   * MEMORY:
   * - O(1) allocation (memory pool)
   * - O(1) lookup (array indexing)
   * - O(1) update
   * - Total: 50-100 ns per update
   * 
   * Parameters:
   * - market_update: Sequenced market update (from publisher)
   */
  auto SnapshotSynthesizer::addToSnapshot(const MDPMarketUpdate *market_update) {
    // Extract internal market update (without sequence number)
    const auto &me_market_update = market_update->me_market_update_;
    
    // Get order array for this instrument
    // ticker_orders_[ticker_id] is array of MEMarketUpdate* (all orders for ticker)
    auto *orders = &ticker_orders_.at(me_market_update.ticker_id_);
    
    // Process based on update type
    switch (me_market_update.type_) {
      case MarketUpdateType::ADD: {
        // Add new order to snapshot
        auto order = orders->at(me_market_update.order_id_);  // Should be nullptr
        
        // Validate: Order must not exist (new order)
        ASSERT(order == nullptr, "Received:" + me_market_update.toString() + " but order already exists:" + (order ? order->toString() : ""));
        
        // Allocate order from pool and store in snapshot
        // order_pool_.allocate(): Creates copy of me_market_update
        orders->at(me_market_update.order_id_) = order_pool_.allocate(me_market_update);
      }
        break;
        
      case MarketUpdateType::MODIFY: {
        // Update existing order in snapshot
        auto order = orders->at(me_market_update.order_id_);  // Should exist
        
        // Validate: Order must exist
        ASSERT(order != nullptr, "Received:" + me_market_update.toString() + " but order does not exist.");
        // Validate: Order IDs match (consistency)
        ASSERT(order->order_id_ == me_market_update.order_id_, "Expecting existing order to match new one.");
        // Validate: Sides match (can't change BUY to SELL)
        ASSERT(order->side_ == me_market_update.side_, "Expecting existing order to match new one.");

        // Update order quantity and price (from MODIFY update)
        order->qty_ = me_market_update.qty_;      // Updated quantity (after partial fill)
        order->price_ = me_market_update.price_;  // Updated price (rare, but possible)
      }
        break;
        
      case MarketUpdateType::CANCEL: {
        // Remove order from snapshot
        auto order = orders->at(me_market_update.order_id_);  // Should exist
        
        // Validate: Order must exist
        ASSERT(order != nullptr, "Received:" + me_market_update.toString() + " but order does not exist.");
        // Validate: Order IDs match
        ASSERT(order->order_id_ == me_market_update.order_id_, "Expecting existing order to match new one.");
        // Validate: Sides match
        ASSERT(order->side_ == me_market_update.side_, "Expecting existing order to match new one.");

        // Deallocate order (return to pool)
        order_pool_.deallocate(order);
        // Clear pointer (order no longer in snapshot)
        orders->at(me_market_update.order_id_) = nullptr;
      }
        break;
        
      // Ignore these update types (don't affect snapshot state)
      case MarketUpdateType::SNAPSHOT_START:  // Snapshot marker (publishing only)
      case MarketUpdateType::CLEAR:            // Clear marker (publishing only)
      case MarketUpdateType::SNAPSHOT_END:     // Snapshot marker (publishing only)
      case MarketUpdateType::TRADE:            // Already reflected in MODIFY
      case MarketUpdateType::INVALID:          // Error condition
        break;
    }

    // Validate sequence number (detect gaps)
    // Expect: seq_num = last_inc_seq_num + 1 (consecutive)
    ASSERT(market_update->seq_num_ == last_inc_seq_num_ + 1, "Expected incremental seq_nums to increase.");
    
    // Update last processed sequence number
    // This is the sync point for snapshot publishing
    last_inc_seq_num_ = market_update->seq_num_;
  }

  /*
   * PUBLISH SNAPSHOT - BROADCAST FULL ORDER BOOK
   * =============================================
   * 
   * Publish complete order book snapshot via multicast.
   * 
   * ALGORITHM:
   * 1. Send SNAPSHOT_START marker (with last incremental seq#)
   * 2. For each instrument (ticker):
   *    a. Send CLEAR marker (subscriber clears local book)
   *    b. For each active order:
   *       - Send ADD message (with full order details)
   *       - Flush socket (sendAndRecv)
   * 3. Send SNAPSHOT_END marker (with last incremental seq#)
   * 4. Log snapshot size (number of orders published)
   * 
   * SNAPSHOT SEQUENCE:
   * - snapshot_size: Counter (0, 1, 2, ...)
   * - Used as "snapshot sequence number" (not same as incremental seq#)
   * - Allows tracking progress through snapshot
   * - Not used for gap detection (snapshot is reliable - if gap, wait for next)
   * 
   * SNAPSHOT_START:
   * - Marker indicating snapshot beginning
   * - Contains: last_inc_seq_num_ (sync point)
   * - Subscriber: Start rebuilding book, buffer incremental updates
   * 
   * CLEAR:
   * - One per instrument
   * - Subscriber: Clear local order book for this ticker
   * - Reason: Remove stale orders before rebuilding
   * 
   * ADD MESSAGES:
   * - One per active order
   * - Contains: All order fields (ticker, side, price, qty, etc.)
   * - Subscriber: Add order to local book
   * - sendAndRecv: Flush socket after each order (ensure delivery)
   * 
   * SNAPSHOT_END:
   * - Marker indicating snapshot completion
   * - Contains: last_inc_seq_num_ (same as SNAPSHOT_START)
   * - Subscriber: Apply buffered incremental updates (seq# > snapshot seq#)
   * 
   * SYNC POINT:
   * - last_inc_seq_num_: Last incremental update processed
   * - Included in SNAPSHOT_START and SNAPSHOT_END
   * - Subscriber uses to resume incremental stream
   * - Example: Snapshot built up to seq# 12345
   *   - Snapshot contains seq# 12345
   *   - Subscriber applies incremental updates 12346, 12347, ...
   * 
   * PERFORMANCE:
   * - Snapshot size: N orders * 48 bytes
   * - Typical: 10K orders = 480 KB
   * - Peak: 1M orders = 48 MB
   * - Send time: 10-100 ms (network-bound)
   * - Frequency: 60 seconds (see run() method)
   * 
   * FLUSH:
   * - sendAndRecv: Flush UDP buffer (actual network send)
   * - Called after each order (ensures reliable delivery)
   * - Production: Could batch (send multiple orders, then flush)
   * 
   * LOGGING:
   * - Log each message (debugging, audit)
   * - Async logging (off critical path)
   * - Final log: Total orders published
   */
  auto SnapshotSynthesizer::publishSnapshot() {
    size_t snapshot_size = 0;  // Snapshot sequence counter (0, 1, 2, ...)

    // STEP 1: Send SNAPSHOT_START marker
    // Contains: last_inc_seq_num_ (sync point for subscribers)
    // Stored in order_id_ field (repurposing, not actual order ID)
    const MDPMarketUpdate start_market_update{snapshot_size++, {MarketUpdateType::SNAPSHOT_START, last_inc_seq_num_}};
    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_), start_market_update.toString());
    snapshot_socket_.send(&start_market_update, sizeof(MDPMarketUpdate));

    // STEP 2: Publish all active orders (all instruments)
    for (size_t ticker_id = 0; ticker_id < ticker_orders_.size(); ++ticker_id) {
      const auto &orders = ticker_orders_.at(ticker_id);  // All orders for this instrument

      // Create CLEAR marker for this instrument
      MEMarketUpdate me_market_update;
      me_market_update.type_ = MarketUpdateType::CLEAR;
      me_market_update.ticker_id_ = ticker_id;

      // Send CLEAR marker (subscriber clears local book for this ticker)
      const MDPMarketUpdate clear_market_update{snapshot_size++, me_market_update};
      logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_), clear_market_update.toString());
      snapshot_socket_.send(&clear_market_update, sizeof(MDPMarketUpdate));

      // Publish each active order for this instrument
      for (const auto order: orders) {
        if (order) {  // nullptr = order doesn't exist (canceled)
          // Create ADD message (full order details)
          // Stored in MEMarketUpdate (reuse struct, type=ADD)
          const MDPMarketUpdate market_update{snapshot_size++, *order};
          logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_), market_update.toString());
          
          // Send order
          snapshot_socket_.send(&market_update, sizeof(MDPMarketUpdate));
          
          // Flush socket (ensure delivery)
          // Could batch for performance (send multiple, then flush once)
          snapshot_socket_.sendAndRecv();
        }
      }
    }

    // STEP 3: Send SNAPSHOT_END marker
    // Contains: last_inc_seq_num_ (same sync point as SNAPSHOT_START)
    const MDPMarketUpdate end_market_update{snapshot_size++, {MarketUpdateType::SNAPSHOT_END, last_inc_seq_num_}};
    logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_), end_market_update.toString());
    snapshot_socket_.send(&end_market_update, sizeof(MDPMarketUpdate));
    
    // Final flush (ensure all messages sent)
    snapshot_socket_.sendAndRecv();

    // Log snapshot completion (snapshot_size - 1 = actual orders, excluding SNAPSHOT_START)
    logger_.log("%:% %() % Published snapshot of % orders.\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_), snapshot_size - 1);
  }

  /*
   * RUN - MAIN SNAPSHOT LOOP
   * =========================
   * 
   * Main loop for snapshot synthesizer thread.
   * Processes incremental updates and publishes snapshots periodically.
   * 
   * ALGORITHM:
   * 1. Log thread start
   * 2. While run_ is true:
   *    a. Poll snapshot_md_updates_ queue
   *    b. For each market update:
   *       - Log update
   *       - Process update (addToSnapshot)
   *       - Consume from queue
   *    c. Check if snapshot interval elapsed (60 seconds)
   *    d. If yes: Publish snapshot (publishSnapshot)
   * 3. Repeat until stopped
   * 
   * UPDATE PROCESSING:
   * - Poll lock-free queue (non-blocking)
   * - addToSnapshot: Maintain full order book
   * - updateReadIndex: Consume from queue
   * 
   * SNAPSHOT TIMING:
   * - Interval: 60 seconds (60 * NANOS_TO_SECS)
   * - last_snapshot_time_: Timestamp of last snapshot
   * - Check: getCurrentNanos() - last_snapshot_time_ > 60 seconds
   * - If elapsed: Publish snapshot, update last_snapshot_time_
   * 
   * WHY 60 SECONDS?
   * - Balance: Frequent enough for recovery, infrequent enough to save bandwidth
   * - Typical industry: 1-60 seconds
   * - Production: Configurable (e.g., 1 second for HFT, 60 seconds for retail)
   * 
   * PERFORMANCE:
   * - Update processing: 50-100 ns per update
   * - Snapshot publishing: 10-100 ms (once per 60 seconds)
   * - CPU usage: ~1-10% (not latency-critical)
   * - Not hot path: Can afford higher latency
   * 
   * BUSY-WAIT:
   * - Continuously polls queue (even if empty)
   * - Trade-off: CPU usage for responsiveness
   * - Acceptable: Dedicated core, not latency-critical
   */
  void SnapshotSynthesizer::run() {
    // Log thread start
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_));
    
    // Main snapshot loop
    while (run_) {
      // Poll for incremental market updates (non-blocking)
      // Loop while queue has data
      for (auto market_update = snapshot_md_updates_->getNextToRead(); 
           snapshot_md_updates_->size() && market_update; 
           market_update = snapshot_md_updates_->getNextToRead()) {
        
        // Log update (debugging)
        logger_.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str_),
                    market_update->toString().c_str());

        // Process update (maintain full order book)
        addToSnapshot(market_update);

        // Consume from queue (advance read index)
        snapshot_md_updates_->updateReadIndex();
      }

      // Check if snapshot interval elapsed (60 seconds)
      if (getCurrentNanos() - last_snapshot_time_ > 60 * NANOS_TO_SECS) {
        // Update last snapshot time (now)
        last_snapshot_time_ = getCurrentNanos();
        
        // Publish full order book snapshot
        publishSnapshot();
      }
    }
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. MEMORY POOL:
 *    - MemPool<MEMarketUpdate>: Pre-allocated orders
 *    - Capacity: ME_MAX_ORDER_IDS (1M)
 *    - Size: ~40 MB
 *    - O(1) allocation/deallocation
 *    - Critical for performance (no heap)
 * 
 * 2. ORDER BOOK STRUCTURE:
 *    - ticker_orders_: 2D array [ticker][order_id] -> MEMarketUpdate*
 *    - Size: ~800 MB (large but necessary)
 *    - O(1) lookup (array indexing)
 *    - Alternative: Sparse hash map (less memory, more complex)
 * 
 * 3. SNAPSHOT INTERVAL:
 *    - 60 seconds (configurable via NANOS_TO_SECS)
 *    - Trade-off: Recovery time vs bandwidth
 *    - Production: Could be adaptive (more frequent during volatility)
 * 
 * 4. FLUSH FREQUENCY:
 *    - sendAndRecv after each order
 *    - Ensures reliable delivery
 *    - Production: Could batch (send N orders, then flush)
 *    - Trade-off: Latency vs efficiency
 * 
 * 5. SYNC POINT:
 *    - last_inc_seq_num_: Critical for subscriber recovery
 *    - Stored in SNAPSHOT_START and SNAPSHOT_END
 *    - Allows subscriber to resume from correct position
 * 
 * 6. ASSERTIONS:
 *    - Extensive validation (order existence, ID matching)
 *    - Debug builds: Catch inconsistencies
 *    - Production: Could be removed (#ifdef NDEBUG)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Incremental Snapshots:
 *    - Full snapshot + deltas (changes since last snapshot)
 *    - Advantage: Smaller bandwidth
 *    - Disadvantage: More complex (subscriber must apply deltas)
 *    - Suitable: High-frequency snapshots
 * 
 * B) Snapshot on Demand:
 *    - Subscriber requests snapshot (TCP channel)
 *    - Advantage: No bandwidth waste
 *    - Disadvantage: More complex (server state, request handling)
 *    - Suitable: Few subscribers, infrequent gaps
 * 
 * C) Compressed Snapshots:
 *    - LZ4 or DEFLATE compression
 *    - Advantage: 50-90% size reduction
 *    - Disadvantage: CPU overhead (compression/decompression)
 *    - Suitable: Bandwidth-constrained environments
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Configurable interval (not hardcoded 60 seconds)
 * - Adaptive interval (based on volatility or subscriber requests)
 * - Compression (LZ4 for speed, DEFLATE for size)
 * - Batched flush (send N orders, flush once)
 * - Book depth limits (top N levels only)
 * - Differential snapshots (changes since last)
 * - Snapshot caching (store on disk for recovery)
 * - Redundancy (multiple snapshot streams)
 */
