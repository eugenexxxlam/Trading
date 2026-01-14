#include "market_data_consumer.h"

/*
 * MARKET DATA CONSUMER IMPLEMENTATION
 * ====================================
 * 
 * Implementation of MarketDataConsumer with gap recovery logic.
 * Handles multicast market data, packet loss detection, snapshot synchronization.
 * 
 * See market_data_consumer.h for detailed class documentation and architecture.
 * 
 * KEY ALGORITHMS:
 * - recvCallback: Process market updates, detect gaps
 * - startSnapshotSync: Begin recovery (subscribe to snapshot)
 * - checkSnapshotSync: Complete recovery (validate and apply)
 * - queueMessage: Buffer messages during recovery
 */

namespace Trading {
  /*
   * CONSTRUCTOR - INITIALIZE MARKET DATA CONSUMER
   * ==============================================
   * 
   * Sets up multicast sockets and initializes recovery state.
   * 
   * ALGORITHM:
   * 1. Store queue pointer, connection parameters
   * 2. Initialize logger ("trading_market_data_consumer_<client_id>.log")
   * 3. Initialize multicast sockets (with logger)
   * 4. Create receive callback (lambda, shared by both sockets)
   * 5. Initialize incremental socket (connect + join)
   * 6. Initialize snapshot socket (not connected yet, only during recovery)
   * 
   * INCREMENTAL SOCKET:
   * - init(): Create socket, bind to interface/port
   * - join(): Join multicast group (IGMP subscription)
   * - is_listening = true: Receiving (not sending)
   * - Always subscribed: Real-time market data
   * 
   * SNAPSHOT SOCKET:
   * - Not initialized yet (only during recovery)
   * - Saved parameters: iface_, snapshot_ip_, snapshot_port_
   * - Used by startSnapshotSync() when gap detected
   * 
   * SHARED CALLBACK:
   * - recv_callback: Lambda [this](socket) { recvCallback(socket); }
   * - Both sockets use same callback
   * - Callback determines source by socket_fd_
   * 
   * ERROR HANDLING:
   * - ASSERT: Socket operations must succeed
   * - errno: System error code
   * - Fatal if fails: Cannot receive market data
   * 
   * Parameters:
   * - client_id: Trading firm's client identifier (for logging)
   * - market_updates: Lock-free queue to trading engine
   * - iface: Network interface
   * - snapshot_ip: Snapshot multicast group
   * - snapshot_port: Snapshot port
   * - incremental_ip: Incremental multicast group
   * - incremental_port: Incremental port
   */
  MarketDataConsumer::MarketDataConsumer(Common::ClientId client_id, Exchange::MEMarketUpdateLFQueue *market_updates,
                                         const std::string &iface,
                                         const std::string &snapshot_ip, int snapshot_port,
                                         const std::string &incremental_ip, int incremental_port)
      : incoming_md_updates_(market_updates),                    // Store queue pointer
        run_(false),                                              // Not started yet
        logger_("trading_market_data_consumer_" + std::to_string(client_id) + ".log"),  // Initialize logger
        incremental_mcast_socket_(logger_),                      // Initialize incremental socket
        snapshot_mcast_socket_(logger_),                         // Initialize snapshot socket
        iface_(iface), snapshot_ip_(snapshot_ip), snapshot_port_(snapshot_port) {  // Store snapshot params
    
    // Create shared receive callback (lambda captures this pointer)
    // Both sockets use same callback (determines source by socket_fd_)
    auto recv_callback = [this](auto socket) {
      recvCallback(socket);
    };

    // Initialize incremental socket (always subscribed)
    incremental_mcast_socket_.recv_callback_ = recv_callback;  // Register callback
    
    // Create and bind socket
    ASSERT(incremental_mcast_socket_.init(incremental_ip, iface, incremental_port, /*is_listening*/ true) >= 0,
           "Unable to create incremental mcast socket. error:" + std::string(std::strerror(errno)));

    // Join multicast group (IGMP subscription)
    ASSERT(incremental_mcast_socket_.join(incremental_ip),
           "Join failed on:" + std::to_string(incremental_mcast_socket_.socket_fd_) + 
           " error:" + std::string(std::strerror(errno)));

    // Register callback for snapshot socket (not initialized yet)
    // Will be initialized by startSnapshotSync() when gap detected
    snapshot_mcast_socket_.recv_callback_ = recv_callback;
  }

  /*
   * RUN - MAIN MARKET DATA LOOP
   * ============================
   * 
   * Main loop for market data consumer thread.
   * Polls both multicast sockets (incremental always, snapshot during recovery).
   * 
   * ALGORITHM:
   * 1. Log thread start
   * 2. While run_ is true:
   *    a. Poll incremental socket (sendAndRecv)
   *       - Receives real-time market updates
   *       - Triggers recvCallback when data received
   *    b. Poll snapshot socket (sendAndRecv)
   *       - Only active during recovery (in_recovery_ = true)
   *       - Receives snapshot messages
   *       - Triggers recvCallback when data received
   * 3. Repeat until stopped
   * 
   * MULTICAST POLLING:
   * - sendAndRecv(): Non-blocking UDP receive
   * - Returns immediately (no blocking)
   * - Calls recv_callback_ when packet received
   * - Both sockets polled (even if snapshot inactive)
   * 
   * BUSY-WAIT:
   * - Continuously polls (even if no packets)
   * - Low latency: Immediate packet processing
   * - High CPU: Acceptable for trading (dedicated core)
   * 
   * NOEXCEPT:
   * - No exception handling overhead
   * - Performance: Market data is latency-sensitive
   */
  auto MarketDataConsumer::run() noexcept -> void {
    // Log thread start
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
    
    // Main market data loop
    while (run_) {
      // Poll incremental socket (always active)
      incremental_mcast_socket_.sendAndRecv();
      
      // Poll snapshot socket (only active during recovery)
      snapshot_mcast_socket_.sendAndRecv();
    }
  }

  /*
   * START SNAPSHOT SYNC - BEGIN RECOVERY
   * =====================================
   * 
   * Initiates snapshot synchronization when gap detected.
   * Subscribes to snapshot multicast stream.
   * 
   * ALGORITHM:
   * 1. Clear buffered messages (start fresh recovery)
   *    - snapshot_queued_msgs_: Clear previous attempts
   *    - incremental_queued_msgs_: Clear (will buffer new ones)
   * 2. Initialize snapshot socket (create, bind)
   * 3. Join snapshot multicast group (IGMP subscription)
   * 4. Begin receiving snapshot stream
   * 
   * WHY CLEAR QUEUES:
   * - Previous recovery may have failed (incomplete snapshot)
   * - Start fresh: Wait for next complete snapshot
   * - Clean state: No mixed messages from different snapshots
   * 
   * IGMP JOIN:
   * - join(): Subscribe to multicast group
   * - Network: Router begins forwarding packets to this host
   * - Immediate: Starts receiving next snapshot
   * 
   * SNAPSHOT FREQUENCY:
   * - Typical: 1 snapshot per second
   * - Wait time: 0-1 second (depends on timing)
   * - Acceptable: Recovery not latency-critical
   * 
   * ERROR HANDLING:
   * - ASSERT: Socket operations must succeed
   * - errno: System error code
   * - Fatal if fails: Cannot recover from gap
   */
  auto MarketDataConsumer::startSnapshotSync() -> void {
    // Clear buffered messages (start fresh recovery)
    snapshot_queued_msgs_.clear();       // Clear previous snapshot attempts
    incremental_queued_msgs_.clear();    // Clear previous incremental buffer

    // Initialize snapshot socket (create, bind to interface/port)
    ASSERT(snapshot_mcast_socket_.init(snapshot_ip_, iface_, snapshot_port_, /*is_listening*/ true) >= 0,
           "Unable to create snapshot mcast socket. error:" + std::string(std::strerror(errno)));
    
    // Join snapshot multicast group (IGMP subscription)
    ASSERT(snapshot_mcast_socket_.join(snapshot_ip_),  // Subscribe to multicast group
           "Join failed on:" + std::to_string(snapshot_mcast_socket_.socket_fd_) + 
           " error:" + std::string(std::strerror(errno)));
  }

  /*
   * CHECK SNAPSHOT SYNC - ATTEMPT RECOVERY COMPLETION
   * ==================================================
   * 
   * Checks if recovery can be completed from buffered messages.
   * Validates snapshot completeness and synchronizes with incremental stream.
   * 
   * This is the most complex method in the market data consumer.
   * It implements the core snapshot synchronization algorithm.
   * 
   * ALGORITHM:
   * 
   * PART 1: VALIDATE SNAPSHOT START
   * 1. Check if snapshot queue empty (return, wait for data)
   * 2. Check first message is SNAPSHOT_START
   *    - If not: Clear queue, return (invalid snapshot)
   * 
   * PART 2: VALIDATE SNAPSHOT COMPLETENESS
   * 3. Iterate all snapshot messages:
   *    a. Check sequence numbers consecutive (no gaps)
   *    b. Add valid messages to final_events (exclude START/END)
   *    c. If gap found: Clear queue, return (incomplete)
   * 
   * PART 3: VALIDATE SNAPSHOT END
   * 4. Check last message is SNAPSHOT_END
   *    - If not: Return (snapshot incomplete, wait for more)
   * 
   * PART 4: VALIDATE INCREMENTAL COMPLETENESS
   * 5. Get sync point (last_snapshot_msg.order_id_)
   * 6. Iterate incremental messages after sync point:
   *    a. Skip messages before sync point (already in snapshot)
   *    b. Check consecutive sequences (no gaps)
   *    c. Add to final_events
   *    d. If gap found: Clear snapshot, return (incomplete)
   * 
   * PART 5: APPLY RECOVERY
   * 7. Forward all final_events to trading engine
   * 8. Clear queues (recovery complete)
   * 9. Exit recovery mode (in_recovery_ = false)
   * 10. Unsubscribe from snapshot (leave multicast group)
   * 
   * KEY CONCEPTS:
   * 
   * SYNC POINT:
   * - Stored in SNAPSHOT_END.order_id_ (repurposed field)
   * - Represents: Last incremental seq# included in snapshot
   * - Example: Snapshot built up to seq# 12345
   *   - Snapshot messages: seq 0, 1, 2, ..., N
   *   - Sync point: 12345 (last incremental seq# in snapshot)
   *   - Apply incrementals: 12346, 12347, ... (after sync point)
   * 
   * SNAPSHOT SEQUENCE vs INCREMENTAL SEQUENCE:
   * - Snapshot: Internal sequence (0, 1, 2, ...) within snapshot
   * - Incremental: Global sequence (1, 2, 3, ...) of market updates
   * - Sync point: Bridges two sequence spaces
   * 
   * COMPLETENESS CHECKS:
   * - Snapshot: Must have START, consecutive seq#, END
   * - Incremental: Must have all seq# after sync point
   * - Any gap: Recovery fails, wait for next snapshot
   * 
   * FINAL EVENTS:
   * - Snapshot: All messages except START/END (CLEAR, ADD, ...)
   * - Incremental: All messages after sync point
   * - Order: Snapshot first (rebuild book), then incremental (catch up)
   * - Result: Trading engine rebuilds order book from scratch
   * 
   * PERFORMANCE:
   * - Complexity: O(S + I) where S = snapshot size, I = incremental size
   * - Typical: S = 10K orders, I = 10-100 incrementals
   * - Time: 10-100 ms (iteration + queue writes)
   * - Not latency-critical: Recovery is rare event
   */
  auto MarketDataConsumer::checkSnapshotSync() -> void {
    // PART 1: CHECK SNAPSHOT NOT EMPTY
    if (snapshot_queued_msgs_.empty()) {
      return;  // Wait for snapshot data
    }

    // PART 2: VALIDATE SNAPSHOT START
    // First message must be SNAPSHOT_START
    const auto &first_snapshot_msg = snapshot_queued_msgs_.begin()->second;
    if (first_snapshot_msg.type_ != Exchange::MarketUpdateType::SNAPSHOT_START) {
      // Invalid snapshot: Doesn't start with SNAPSHOT_START
      logger_.log("%:% %() % Returning because have not seen a SNAPSHOT_START yet.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();  // Clear invalid snapshot
      return;
    }

    // PART 3: VALIDATE SNAPSHOT COMPLETENESS
    // Collect valid snapshot messages (exclude START/END)
    std::vector<Exchange::MEMarketUpdate> final_events;

    auto have_complete_snapshot = true;
    size_t next_snapshot_seq = 0;  // Expected snapshot sequence number
    
    // Iterate all snapshot messages (map is sorted by seq#)
    for (auto &snapshot_itr: snapshot_queued_msgs_) {
      // Log each snapshot message
      logger_.log("%:% %() % % => %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), snapshot_itr.first, snapshot_itr.second.toString());
      
      // Check for gap in snapshot sequence
      if (snapshot_itr.first != next_snapshot_seq) {
        // Gap detected: Snapshot incomplete (missing packets)
        have_complete_snapshot = false;
        logger_.log("%:% %() % Detected gap in snapshot stream expected:% found:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), next_snapshot_seq, snapshot_itr.first, snapshot_itr.second.toString());
        break;
      }

      // Add to final events (exclude START/END markers)
      if (snapshot_itr.second.type_ != Exchange::MarketUpdateType::SNAPSHOT_START &&
          snapshot_itr.second.type_ != Exchange::MarketUpdateType::SNAPSHOT_END)
        final_events.push_back(snapshot_itr.second);

      ++next_snapshot_seq;  // Expect next sequence number
    }

    // If snapshot has gaps, wait for next complete snapshot
    if (!have_complete_snapshot) {
      logger_.log("%:% %() % Returning because found gaps in snapshot stream.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();  // Clear incomplete snapshot
      return;
    }

    // PART 4: VALIDATE SNAPSHOT END
    // Last message must be SNAPSHOT_END
    const auto &last_snapshot_msg = snapshot_queued_msgs_.rbegin()->second;  // reverse begin = last element
    if (last_snapshot_msg.type_ != Exchange::MarketUpdateType::SNAPSHOT_END) {
      // Snapshot not complete yet: Wait for SNAPSHOT_END
      logger_.log("%:% %() % Returning because have not seen a SNAPSHOT_END yet.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      return;  // Keep snapshot, wait for more data
    }

    // PART 5: EXTRACT SYNC POINT
    // Sync point: Last incremental seq# included in snapshot
    // Stored in: SNAPSHOT_END.order_id_ (repurposed field)
    // Resume from: sync_point + 1
    auto have_complete_incremental = true;
    size_t num_incrementals = 0;
    next_exp_inc_seq_num_ = last_snapshot_msg.order_id_ + 1;  // Resume after sync point
    
    // PART 6: VALIDATE INCREMENTAL COMPLETENESS
    // Check buffered incremental messages (after sync point)
    for (auto inc_itr = incremental_queued_msgs_.begin(); 
         inc_itr != incremental_queued_msgs_.end(); 
         ++inc_itr) {
      
      // Log checking (debugging)
      logger_.log("%:% %() % Checking next_exp:% vs. seq:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), next_exp_inc_seq_num_, inc_itr->first, inc_itr->second.toString());

      // Skip incrementals before sync point (already in snapshot)
      if (inc_itr->first < next_exp_inc_seq_num_)
        continue;

      // Check for gap in incremental sequence
      if (inc_itr->first != next_exp_inc_seq_num_) {
        // Gap detected: Incremental stream incomplete
        logger_.log("%:% %() % Detected gap in incremental stream expected:% found:% %.\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), next_exp_inc_seq_num_, inc_itr->first, inc_itr->second.toString());
        have_complete_incremental = false;
        break;
      }

      // Log valid incremental message
      logger_.log("%:% %() % % => %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), inc_itr->first, inc_itr->second.toString());

      // Add to final events (exclude START/END markers)
      if (inc_itr->second.type_ != Exchange::MarketUpdateType::SNAPSHOT_START &&
          inc_itr->second.type_ != Exchange::MarketUpdateType::SNAPSHOT_END)
        final_events.push_back(inc_itr->second);

      ++next_exp_inc_seq_num_;  // Expect next sequence
      ++num_incrementals;        // Count applied incrementals
    }

    // If incremental has gaps, wait (may catch up with next packets)
    if (!have_complete_incremental) {
      logger_.log("%:% %() % Returning because have gaps in queued incrementals.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      snapshot_queued_msgs_.clear();  // Clear snapshot, try again
      return;
    }

    // PART 7: APPLY RECOVERY
    // Forward all final events to trading engine
    // Order: Snapshot messages first (rebuild book), then incrementals (catch up)
    for (const auto &itr: final_events) {
      auto next_write = incoming_md_updates_->getNextToWriteTo();  // Reserve slot
      *next_write = itr;                                            // Write market update
      incoming_md_updates_->updateWriteIndex();                     // Commit (publish)
    }

    // Log recovery success
    logger_.log("%:% %() % Recovered % snapshot and % incremental orders.\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), 
                snapshot_queued_msgs_.size() - 2,  // Exclude START/END
                num_incrementals);

    // PART 8: CLEANUP AND EXIT RECOVERY
    // Clear buffered messages (recovery complete)
    snapshot_queued_msgs_.clear();
    incremental_queued_msgs_.clear();
    
    // Exit recovery mode (resume normal operation)
    in_recovery_ = false;

    // Unsubscribe from snapshot stream (save bandwidth)
    snapshot_mcast_socket_.leave(snapshot_ip_, snapshot_port_);
  }

  /*
   * QUEUE MESSAGE - BUFFER DURING RECOVERY
   * =======================================
   * 
   * Buffers market update message during recovery.
   * Stores in ordered map (std::map automatically sorts by sequence number).
   * 
   * ALGORITHM:
   * 1. Determine source: snapshot or incremental
   * 2. If snapshot:
   *    a. Check for duplicate (shouldn't happen)
   *    b. If duplicate: Clear queue (corrupted, restart)
   *    c. Store in snapshot_queued_msgs_
   * 3. If incremental:
   *    a. Store in incremental_queued_msgs_
   * 4. Log queue sizes (debugging)
   * 5. Check if recovery complete (checkSnapshotSync)
   * 
   * DUPLICATE HANDLING:
   * - Snapshot: Duplicate indicates packet reordering or corruption
   *   - Action: Clear queue, wait for next clean snapshot
   * - Incremental: std::map overwrites (same key)
   *   - OK: Latest version kept
   * 
   * std::map ORDERING:
   * - Automatic sorting by key (sequence number)
   * - Advantage: Easy iteration in order
   * - Performance: O(log N) insert (acceptable during recovery)
   * 
   * TRIGGER RECOVERY CHECK:
   * - Every message: Call checkSnapshotSync()
   * - Check: Can we complete recovery now?
   * - Early completion: As soon as complete snapshot + incrementals
   * 
   * Parameters:
   * - is_snapshot: true if from snapshot stream
   * - request: Market update message (with seq#)
   */
  auto MarketDataConsumer::queueMessage(bool is_snapshot, const Exchange::MDPMarketUpdate *request) {
    if (is_snapshot) {
      // Check for duplicate snapshot message (shouldn't happen)
      if (snapshot_queued_msgs_.find(request->seq_num_) != snapshot_queued_msgs_.end()) {
        // Duplicate: Packet reordering or corruption
        logger_.log("%:% %() % Packet drops on snapshot socket. Received for a 2nd time:%\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), request->toString());
        snapshot_queued_msgs_.clear();  // Clear corrupted queue, start over
      }
      // Store in snapshot queue (map key = seq#, value = update)
      snapshot_queued_msgs_[request->seq_num_] = request->me_market_update_;
    } else {
      // Store in incremental queue (map automatically handles duplicates)
      incremental_queued_msgs_[request->seq_num_] = request->me_market_update_;
    }

    // Log queue sizes (debugging)
    logger_.log("%:% %() % size snapshot:% incremental:% % => %\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), 
                snapshot_queued_msgs_.size(), 
                incremental_queued_msgs_.size(), 
                request->seq_num_, 
                request->toString());

    // Check if recovery can be completed now
    checkSnapshotSync();
  }

  /*
   * RECV CALLBACK - PROCESS MARKET DATA UPDATE
   * ===========================================
   * 
   * Called by multicast socket when market update received.
   * Handles both incremental and snapshot messages.
   * Core logic: Gap detection, recovery triggering, normal forwarding.
   * 
   * ALGORITHM:
   * 
   * PART 1: DETERMINE SOURCE
   * 1. Check socket_fd_ to determine incremental vs snapshot
   * 2. If snapshot but not in recovery: Discard (unexpected)
   * 
   * PART 2: PARSE BUFFER
   * 3. Check buffer contains complete message (>= sizeof(MDPMarketUpdate))
   * 4. Parse all complete messages in buffer
   * 
   * PART 3: GAP DETECTION
   * 5. For each message:
   *    a. Check if already in recovery (already_in_recovery)
   *    b. Check if gap detected (seq# != expected)
   *    c. If gap and not already recovering: Enter recovery
   *       - Log gap
   *       - Call startSnapshotSync()
   *    d. Update in_recovery_ flag
   * 
   * PART 4: MESSAGE ROUTING
   * 6. If in recovery: Buffer message (queueMessage)
   * 7. If normal operation: Forward to trading engine
   * 
   * PART 5: BUFFER MANAGEMENT
   * 8. Shift remaining data in buffer (incomplete message)
   * 
   * KEY LOGIC:
   * 
   * GAP DETECTION:
   * - Check: request->seq_num_ != next_exp_inc_seq_num_
   * - Example: Expected 101, received 102 (gap)
   * - Action: Enter recovery immediately
   * 
   * RECOVERY STATE MACHINE:
   * - Normal: in_recovery_ = false, forward to trading engine
   * - Gap detected: Enter recovery, subscribe to snapshot
   * - Recovery: Buffer all messages (snapshot + incremental)
   * - Complete: Exit recovery, resume normal operation
   * 
   * TWO STREAMS:
   * - Incremental: Always received (even during recovery)
   * - Snapshot: Only during recovery (subscribed on-demand)
   * 
   * PERFORMANCE:
   * - T7: Market data UDP read (start of update processing)
   * - T8: Market data queue write (forwarded to trading engine)
   * - START_MEASURE / END_MEASURE: Callback total latency
   * 
   * Parameters:
   * - socket: Multicast socket that received data
   * 
   * NOEXCEPT:
   * - No exceptions on hot path
   * - Performance-critical: Market data is latency-sensitive
   */
  auto MarketDataConsumer::recvCallback(McastSocket *socket) noexcept -> void {
    // Record timestamp (T7 = market data UDP receive)
    TTT_MEASURE(T7_MarketDataConsumer_UDP_read, logger_);

    // Begin callback measurement
    START_MEASURE(Trading_MarketDataConsumer_recvCallback);
    
    // PART 1: DETERMINE SOURCE (incremental vs snapshot)
    const auto is_snapshot = (socket->socket_fd_ == snapshot_mcast_socket_.socket_fd_);
    
    // If snapshot message but not in recovery: Discard (unexpected)
    if (UNLIKELY(is_snapshot && !in_recovery_)) {  // UNLIKELY = rare condition
      socket->next_rcv_valid_index_ = 0;  // Clear buffer

      logger_.log("%:% %() % WARN Not expecting snapshot messages.\n",
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));

      return;  // Discard unexpected snapshot
    }

    // PART 2: CHECK BUFFER HAS COMPLETE MESSAGE
    if (socket->next_rcv_valid_index_ >= sizeof(Exchange::MDPMarketUpdate)) {
      size_t i = 0;  // Byte offset in buffer
      
      // Parse all complete messages in buffer
      for (; i + sizeof(Exchange::MDPMarketUpdate) <= socket->next_rcv_valid_index_; 
           i += sizeof(Exchange::MDPMarketUpdate)) {
        
        // Cast buffer to MDPMarketUpdate (seq# + MEMarketUpdate)
        auto request = reinterpret_cast<const Exchange::MDPMarketUpdate *>(socket->inbound_data_.data() + i);
        
        // Log received message
        logger_.log("%:% %() % Received % socket len:% %\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_),
                    (is_snapshot ? "snapshot" : "incremental"), 
                    sizeof(Exchange::MDPMarketUpdate), 
                    request->toString());

        // PART 3: GAP DETECTION AND RECOVERY TRIGGERING
        const bool already_in_recovery = in_recovery_;
        
        // Check for gap: seq# != expected (packet loss)
        in_recovery_ = (already_in_recovery || request->seq_num_ != next_exp_inc_seq_num_);

        if (UNLIKELY(in_recovery_)) {  // UNLIKELY = recovery is rare
          // If just entered recovery (first gap), start snapshot sync
          if (UNLIKELY(!already_in_recovery)) {
            // Log gap detected
            logger_.log("%:% %() % Packet drops on % socket. SeqNum expected:% received:%\n", 
                        __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), 
                        (is_snapshot ? "snapshot" : "incremental"), 
                        next_exp_inc_seq_num_, 
                        request->seq_num_);
            
            // Start recovery: Subscribe to snapshot stream
            startSnapshotSync();
          }

          // PART 4A: RECOVERY MODE - BUFFER MESSAGE
          // Queue message and check if recovery can be completed
          queueMessage(is_snapshot, request);
        } else if (!is_snapshot) {
          // PART 4B: NORMAL OPERATION - FORWARD MESSAGE
          // Not in recovery, incremental message, process normally
          
          // Log message
          logger_.log("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), request->toString());

          // Increment expected sequence number
          ++next_exp_inc_seq_num_;

          // Forward to trading engine (via lock-free queue)
          auto next_write = incoming_md_updates_->getNextToWriteTo();  // Reserve slot
          *next_write = std::move(request->me_market_update_);         // Write MEMarketUpdate (without seq#)
          incoming_md_updates_->updateWriteIndex();                     // Commit (publish)
          
          // Record timestamp (T8 = market data queue write)
          TTT_MEASURE(T8_MarketDataConsumer_LFQueue_write, logger_);
        }
      }
      
      // PART 5: SHIFT REMAINING DATA IN BUFFER (incomplete message)
      // Example: Buffer has 1.5 messages, keep 0.5 for next receive
      memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_rcv_valid_index_ - i);
      socket->next_rcv_valid_index_ -= i;  // Update valid data length
    }
    
    // End callback measurement
    END_MEASURE(Trading_MarketDataConsumer_recvCallback, logger_);
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. RECOVERY ALGORITHM COMPLEXITY:
 *    - State machine: Normal -> Gap detected -> Recovery -> Normal
 *    - Validation: Snapshot complete, incremental complete
 *    - Synchronization: Sync point bridges snapshot and incremental
 *    - Robust: Handles gaps, duplicates, incomplete snapshots
 * 
 * 2. PERFORMANCE OPTIMIZATION:
 *    - Normal path: Minimal processing (1-5 μs)
 *    - Recovery path: More complex (10-100 ms)
 *    - Acceptable: Recovery is rare (<0.01% of time)
 *    - Trade-off: Optimize normal, recovery can be slower
 * 
 * 3. std::map USAGE:
 *    - Automatic ordering by sequence number
 *    - Simplifies: Iteration in order, gap detection
 *    - Performance: O(log N) insert (acceptable during recovery)
 *    - Alternative: vector + sort (more complex, similar performance)
 * 
 * 4. ERROR RECOVERY:
 *    - Incomplete snapshot: Clear, wait for next (1 second)
 *    - Gap in snapshot: Clear, wait for next
 *    - Gap in incremental: Clear snapshot, try again
 *    - Robust: Eventually recovers (within 1-2 seconds typical)
 * 
 * 5. BANDWIDTH MANAGEMENT:
 *    - Unsubscribe: Leave snapshot group after recovery
 *    - Saves: ~400 Mbps bandwidth (snapshot stream)
 *    - Important: Reduce network load, server load
 * 
 * 6. LOGGING:
 *    - Extensive logging (every step)
 *    - Purpose: Debugging, monitoring, audit
 *    - Async: Non-blocking (off hot path)
 *    - Production: Can reduce verbosity
 * 
 * RECOVERY TIMING:
 * - Gap detection: Immediate (<1 μs)
 * - Snapshot subscription: 100-500 μs (IGMP join)
 * - Snapshot wait: 0-1 second (depends on timing)
 * - Snapshot receive: 10-100 ms (depends on size)
 * - Apply: 1-10 ms (forward messages)
 * - Total: Typically 100-1000 ms (1-2 seconds worst case)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Retransmission: Request specific missing packets (TCP channel)
 * - FEC: Forward error correction (recover without waiting)
 * - Adaptive: Adjust snapshot frequency based on loss rate
 * - Redundant feeds: Primary + backup streams (automatic failover)
 * - Compression: LZ4 (reduce bandwidth)
 * - Filtering: Subscribe only to relevant symbols (reduce load)
 * - Monitoring: Track loss rate, recovery time, queue sizes
 * - Alerting: Notify on excessive loss (network issue)
 * - Pre-buffering: Always buffer incrementals (faster recovery)
 * - Parallel recovery: Continue processing while recovering (complex)
 * 
 * ALTERNATIVE RECOVERY STRATEGIES:
 * 
 * A) Retransmission (Current + Enhancement):
 *    - Request missing packets via TCP
 *    - Advantage: Faster (no snapshot wait)
 *    - Disadvantage: Requires exchange retransmission server
 *    - Industry: Some exchanges offer this
 * 
 * B) FEC (Forward Error Correction):
 *    - Extra redundancy packets (like RAID)
 *    - Advantage: Recover without request (instant)
 *    - Disadvantage: Higher bandwidth (10-20% overhead)
 *    - Industry: Used by some HFT firms
 * 
 * C) Always Buffer (Pre-emptive):
 *    - Always buffer incrementals (not just during recovery)
 *    - Advantage: Faster recovery (already buffered)
 *    - Disadvantage: Higher memory, complexity
 *    - Rare: Most systems buffer on-demand
 * 
 * D) State Reconciliation:
 *    - Request full order book from exchange (TCP)
 *    - Advantage: Guaranteed correct state
 *    - Disadvantage: Slow (TCP round-trip + processing)
 *    - Use case: Startup, periodic validation
 */
