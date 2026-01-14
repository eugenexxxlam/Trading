#include "order_gateway.h"

/*
 * ORDER GATEWAY IMPLEMENTATION
 * =============================
 * 
 * Implementation of OrderGateway constructor and main methods.
 * Handles order submission and execution report processing.
 * 
 * See order_gateway.h for detailed class documentation and architecture.
 */

namespace Trading {
  /*
   * CONSTRUCTOR - INITIALIZE ORDER GATEWAY
   * =======================================
   * 
   * Sets up TCP connection parameters and initializes components.
   * 
   * ALGORITHM:
   * 1. Store client_id, connection parameters (ip, iface, port)
   * 2. Store queue pointers (orders, responses)
   * 3. Initialize logger ("trading_order_gateway_<client_id>.log")
   * 4. Initialize TCP socket (with logger)
   * 5. Register TCP receive callback (for execution reports)
   * 
   * LOGGER:
   * - Per-client log file: Separate logs for each trading client
   * - File: "trading_order_gateway_<client_id>.log"
   * - Async: Non-blocking writes (off hot path)
   * 
   * TCP CALLBACK:
   * - recv_callback_: Lambda function [this](socket, rx_time) { recvCallback(...); }
   * - Captures this pointer
   * - Called when TCP data received
   * - Handles execution reports from exchange
   * 
   * Parameters:
   * - client_id: Trading firm's client identifier
   * - client_requests: Lock-free queue from trading engine
   * - client_responses: Lock-free queue to trading engine
   * - ip: Exchange order server IP
   * - iface: Network interface
   * - port: Exchange order server port
   */
  OrderGateway::OrderGateway(ClientId client_id,
                             Exchange::ClientRequestLFQueue *client_requests,
                             Exchange::ClientResponseLFQueue *client_responses,
                             std::string ip, const std::string &iface, int port)
      : client_id_(client_id),                                    // Store client ID
        ip_(ip), iface_(iface), port_(port),                     // Store connection params
        outgoing_requests_(client_requests),                      // Queue from trading engine
        incoming_responses_(client_responses),                    // Queue to trading engine
      logger_("trading_order_gateway_" + std::to_string(client_id) + ".log"),  // Initialize logger
      tcp_socket_(logger_) {                                      // Initialize TCP socket
    
    // Register TCP receive callback (lambda captures this pointer)
    // Called when execution report received from exchange
    tcp_socket_.recv_callback_ = [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); };
  }

  /*
   * RUN - MAIN ORDER GATEWAY LOOP
   * ==============================
   * 
   * Main loop for order gateway thread.
   * Sends orders to exchange and receives execution reports.
   * 
   * ALGORITHM:
   * 1. Log thread start
   * 2. While run_ is true:
   *    a. Poll TCP socket (send/receive non-blocking)
   *       - Calls recvCallback when data received
   *       - Flushes outgoing data (actual network send)
   *    b. Consume orders from queue (outgoing_requests_)
   *    c. For each order:
   *       - Record timestamp (T11)
   *       - Log order
   *       - Send sequence number (8 bytes)
   *       - Send order (MEClientRequest, ~30 bytes)
   *       - Record timestamp (T12)
   *       - Consume from queue (updateReadIndex)
   *       - Increment sequence number
   * 3. Repeat until stopped
   * 
   * TCP SEND/RECV:
   * - sendAndRecv(): Non-blocking I/O
   * - Sends: Flushes buffered outgoing data
   * - Receives: Reads incoming data, triggers callback
   * - No blocking (returns immediately)
   * 
   * ORDER LOOP:
   * - Polls queue for orders (getNextToRead)
   * - nullptr: No orders available (continues polling)
   * - Order*: Process and send order
   * - Loop until queue empty
   * 
   * TWO TCP SENDS:
   * - First: Sequence number (size_t, 8 bytes)
   * - Second: Order (MEClientRequest, ~30 bytes)
   * - Reason: Separate fields (simpler code)
   * - Alternative: Combined struct (one send, more efficient)
   * 
   * PERFORMANCE MEASUREMENT:
   * - T11: Order gateway reads from queue (start of order flow)
   * - T12: Order gateway writes to TCP (order sent to exchange)
   * - START_MEASURE / END_MEASURE: TCP send latency
   * 
   * SEQUENCE NUMBERING:
   * - next_outgoing_seq_num_: Starts at 1, increments for each order
   * - Sent with each order (exchange validates)
   * - Allows detection of message loss
   * 
   * BUSY-WAIT:
   * - Continuously polls (even if no orders)
   * - Low latency: No sleep, immediate response
   * - High CPU: Acceptable for trading (dedicated core)
   * 
   * NOEXCEPT:
   * - No exception handling overhead
   * - Performance-critical: Order submission is hot path
   */
  auto OrderGateway::run() noexcept -> void {
    // Log thread start
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
    
    // Main order gateway loop
    while (run_) {
      // Poll TCP socket (non-blocking send/receive)
      // Triggers recvCallback when execution report received
      tcp_socket_.sendAndRecv();

      // Consume orders from queue (from trading engine)
      for(auto client_request = outgoing_requests_->getNextToRead(); 
          client_request; 
          client_request = outgoing_requests_->getNextToRead()) {
        
        // Record timestamp (T11 = order gateway queue read)
        TTT_MEASURE(T11_OrderGateway_LFQueue_read, logger_);

        // Log outgoing order
        logger_.log("%:% %() % Sending cid:% seq:% %\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_), client_id_, next_outgoing_seq_num_, client_request->toString());
        
        // Send order to exchange (two TCP sends)
        START_MEASURE(Trading_TCPSocket_send);  // Begin send measurement
        
        // Send 1: Sequence number (8 bytes)
        tcp_socket_.send(&next_outgoing_seq_num_, sizeof(next_outgoing_seq_num_));
        
        // Send 2: Order (MEClientRequest, ~30 bytes)
        tcp_socket_.send(client_request, sizeof(Exchange::MEClientRequest));
        
        END_MEASURE(Trading_TCPSocket_send, logger_);  // End send measurement
        
        // Consume from queue (advance read index)
        outgoing_requests_->updateReadIndex();
        
        // Record timestamp (T12 = order gateway TCP write)
        TTT_MEASURE(T12_OrderGateway_TCP_write, logger_);

        // Increment sequence number (1, 2, 3, ...)
        next_outgoing_seq_num_++;
      }
    }
  }

  /*
   * RECV CALLBACK - PROCESS EXECUTION REPORTS
   * ==========================================
   * 
   * Called by TCP socket when execution report received from exchange.
   * Validates and forwards to trading engine.
   * 
   * ALGORITHM:
   * 1. Record timestamp (T7t = TCP receive)
   * 2. Log receive event (socket, buffer length, timestamp)
   * 3. Check if buffer contains complete response (>= sizeof(OMClientResponse))
   * 4. Parse all complete responses in buffer:
   *    a. Cast buffer to OMClientResponse (seq# + MEClientResponse)
   *    b. Log response
   *    c. Validate client_id (must match this client)
   *    d. Validate sequence number (must be next expected)
   *    e. If valid:
   *       - Increment expected sequence number
   *       - Enqueue MEClientResponse to trading engine
   *       - Record timestamp (T8t)
   *    f. If invalid:
   *       - Log error
   *       - Skip this response (continue with next)
   * 5. Shift remaining data in buffer (incomplete response)
   * 
   * VALIDATION (CLIENT ID):
   * - Check: response->me_client_response_.client_id_ == client_id_
   * - Purpose: Sanity check (should never fail)
   * - Failure: Exchange bug (wrong client's response)
   * - Action: Log error, skip response
   * 
   * VALIDATION (SEQUENCE NUMBER):
   * - Check: response->seq_num_ == next_exp_seq_num_
   * - Purpose: Detect message loss (TCP should be reliable)
   * - Failure: TCP dropped packet (rare) or exchange bug
   * - Action: Log error, skip response
   * - Production: Could request retransmission or reconnect
   * 
   * BUFFER PARSING:
   * - Buffer may contain multiple responses (batch receive)
   * - Loop: Parse all complete responses
   * - Increment: i += sizeof(OMClientResponse) for each parsed
   * - Remainder: Incomplete response, keep for next receive
   * 
   * SHIFT BUFFER:
   * - memcpy: Move incomplete response to beginning
   * - Update: socket->next_rcv_valid_index_ (new length)
   * - Reason: Accumulate data until complete response
   * 
   * PERFORMANCE:
   * - T7t: TCP receive timestamp (start of response processing)
   * - T8t: Queue write timestamp (response forwarded to trading engine)
   * - START_MEASURE / END_MEASURE: Callback total latency
   * 
   * NOEXCEPT:
   * - No exceptions on hot path
   * - Performance-critical: Execution reports are latency-sensitive
   * 
   * Parameters:
   * - socket: TCP socket that received data
   * - rx_time: Receive timestamp (nanoseconds, from kernel or user space)
   */
  auto OrderGateway::recvCallback(TCPSocket *socket, Nanos rx_time) noexcept -> void {
    // Record timestamp (T7t = order gateway TCP receive)
    TTT_MEASURE(T7t_OrderGateway_TCP_read, logger_);

    // Begin callback measurement
    START_MEASURE(Trading_OrderGateway_recvCallback);
    
    // Log receive event
    logger_.log("%:% %() % Received socket:% len:% %\n", __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), socket->socket_fd_, socket->next_rcv_valid_index_, rx_time);

    // Check if buffer contains at least one complete response
    if (socket->next_rcv_valid_index_ >= sizeof(Exchange::OMClientResponse)) {
      size_t i = 0;  // Byte offset in buffer
      
      // Parse all complete responses in buffer
      for (; i + sizeof(Exchange::OMClientResponse) <= socket->next_rcv_valid_index_; 
           i += sizeof(Exchange::OMClientResponse)) {
        
        // Cast buffer to OMClientResponse (seq# + MEClientResponse)
        auto response = reinterpret_cast<const Exchange::OMClientResponse *>(socket->inbound_data_.data() + i);
        
        // Log response
        logger_.log("%:% %() % Received %\n", __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_), response->toString());

        // VALIDATION 1: Check client ID (sanity check)
        if(response->me_client_response_.client_id_ != client_id_) {
          // Wrong client ID: Exchange bug (should never happen)
          logger_.log("%:% %() % ERROR Incorrect client id. ClientId expected:% received:%.\n", 
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), client_id_, response->me_client_response_.client_id_);
          continue;  // Skip this response
        }
        
        // VALIDATION 2: Check sequence number (detect gaps)
        if(response->seq_num_ != next_exp_seq_num_) {
          // Sequence gap: TCP dropped packet (rare) or exchange bug
          logger_.log("%:% %() % ERROR Incorrect sequence number. ClientId:%. SeqNum expected:% received:%.\n", 
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), client_id_, next_exp_seq_num_, response->seq_num_);
          continue;  // Skip this response
        }

        // Increment expected sequence number (validated, good response)
        ++next_exp_seq_num_;

        // Forward response to trading engine (via lock-free queue)
        auto next_write = incoming_responses_->getNextToWriteTo();  // Reserve slot
        *next_write = std::move(response->me_client_response_);     // Write MEClientResponse (without seq#)
        incoming_responses_->updateWriteIndex();                     // Commit (publish to trading engine)
        
        // Record timestamp (T8t = order gateway queue write)
        TTT_MEASURE(T8t_OrderGateway_LFQueue_write, logger_);
      }
      
      // Shift remaining data to beginning of buffer (incomplete response)
      // Example: Buffer has 1.5 responses, keep 0.5 for next receive
      memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_rcv_valid_index_ - i);
      socket->next_rcv_valid_index_ -= i;  // Update valid data length
    }
    
    // End callback measurement
    END_MEASURE(Trading_OrderGateway_recvCallback, logger_);
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. TWO TCP SENDS:
 *    - Current: Separate sends for seq# and order
 *    - Advantage: Simple, clear code
 *    - Disadvantage: Two syscalls (slight overhead)
 *    - Alternative: Combined struct (one send)
 *    ```cpp
 *    struct OMClientRequestWire {
 *      size_t seq_num;
 *      MEClientRequest request;
 *    };
 *    OMClientRequestWire wire = {next_outgoing_seq_num_, *client_request};
 *    tcp_socket_.send(&wire, sizeof(wire));  // One send
 *    ```
 * 
 * 2. SEQUENCE NUMBER VALIDATION:
 *    - Detects TCP drops (rare: ~0.01%)
 *    - Detects exchange bugs (wrong sequence)
 *    - Action: Log and skip (continue processing)
 *    - Production: More sophisticated recovery:
 *      • Request retransmission
 *      • Reconnect and reconcile
 *      • Alert monitoring system
 * 
 * 3. CLIENT ID VALIDATION:
 *    - Sanity check (should never fail)
 *    - If fails: Exchange sent wrong client's response
 *    - Rare bug: Exchange routing error
 *    - Action: Log and skip
 * 
 * 4. BUFFER MANAGEMENT:
 *    - Fixed-size buffer (socket->inbound_data_)
 *    - Accumulates data until complete response
 *    - memcpy: Shift incomplete data (simple, fast for small sizes)
 *    - Alternative: Circular buffer (avoid copying)
 * 
 * 5. NOEXCEPT FUNCTIONS:
 *    - run(), recvCallback(): No exceptions
 *    - Reason: Performance (no exception overhead)
 *    - Hot path: Order flow is latency-critical
 *    - Errors: Logged, not thrown
 * 
 * 6. PERFORMANCE MEASUREMENT:
 *    - TTT_MEASURE: Timestamp tracking points
 *    - T11-T12: Order submission flow
 *    - T7t-T8t: Response processing flow
 *    - Enables: End-to-end latency analysis
 * 
 * LATENCY BREAKDOWN (ORDER SUBMISSION):
 * - T11 (Queue read): 10-20 ns
 * - TCP send (2 calls): 0.5-2 μs
 * - T12 (TCP write): Total 0.5-2 μs
 * - Measurement overhead: ~20-50 ns (rdtsc)
 * - Total: ~1-3 μs (order gateway processing)
 * 
 * LATENCY BREAKDOWN (RESPONSE PROCESSING):
 * - T7t (TCP read): Triggered by kernel
 * - Validation: 5-10 ns (two checks)
 * - Queue write: 10-20 ns
 * - T8t (Queue write): Total 15-30 ns
 * - Buffer shift: 10-50 ns (memcpy, depends on size)
 * - Total: ~30-100 ns (response gateway processing)
 * 
 * ERROR RECOVERY STRATEGIES:
 * 
 * A) Sequence Gap (Current):
 *    - Log error, skip response
 *    - Simple, but loses data
 *    - Acceptable: TCP drops are rare
 * 
 * B) Request Retransmission:
 *    - Send request to exchange for missing response
 *    - Requires: Exchange retransmission server
 *    - Complex: Separate TCP channel, caching
 *    - Industry: Common for market data, less for orders
 * 
 * C) Reconnect and Reconcile:
 *    - Disconnect, reconnect
 *    - Request order status for all open orders
 *    - Ensures: Synchronized state
 *    - Disadvantage: Downtime (seconds)
 * 
 * D) Redundant Connections:
 *    - Maintain two TCP connections (primary, backup)
 *    - Send orders on both (exchange deduplicates)
 *    - Advantage: No single point of failure
 *    - Disadvantage: Complexity, bandwidth
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Heartbeats: Periodic messages (detect connection loss)
 * - Automatic reconnection: Exponential backoff
 * - Order status: Reconciliation on startup/reconnect
 * - Monitoring: Track latency, throughput, errors
 * - Alerting: Notify on sequence gaps, connection loss
 * - Drop copy: Forward orders to risk system
 * - Pre-trade risk: Validate before sending
 * - Rate limiting: Prevent order spam
 * - FIX protocol: Industry standard (vs custom binary)
 * - Encryption: TLS for secure connections
 * - Authentication: API keys, certificates
 */
