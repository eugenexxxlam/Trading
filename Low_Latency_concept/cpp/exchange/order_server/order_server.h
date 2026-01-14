#pragma once

#include <functional>

#include "common/thread_utils.h"
#include "common/macros.h"
#include "common/tcp_server.h"

#include "order_server/client_request.h"
#include "order_server/client_response.h"
#include "order_server/fifo_sequencer.h"

/*
 * ORDER SERVER - TCP ORDER GATEWAY
 * =================================
 * 
 * PURPOSE:
 * Accepts TCP connections from trading clients and handles order flow.
 * Receives orders (NEW, CANCEL) and sends execution reports (ACCEPTED, FILLED, etc.).
 * 
 * RESPONSIBILITIES:
 * 1. Accept TCP connections from clients (one connection per client)
 * 2. Receive client requests via TCP
 * 3. Validate sequence numbers (detect gaps, duplicates)
 * 4. Forward requests to FIFO sequencer (time-priority ordering)
 * 5. Consume execution reports from matching engine
 * 6. Send reports back to clients via TCP
 * 
 * ARCHITECTURE:
 * ```
 * Clients (TCP) <---> Order Server <---> [LF Queue] <---> Matching Engine
 *                          |                                      |
 *                          +--------------------------------------+
 *                                  [LF Queue] (responses)
 * ```
 * 
 * MESSAGE FLOW (INCOMING):
 * 1. Client sends OMClientRequest via TCP
 * 2. TCP server receives into buffer
 * 3. recvCallback: Parse, validate sequence, check client ID
 * 4. Forward to FIFO sequencer (batching)
 * 5. recvFinishedCallback: Sequence and publish batch to matching engine
 * 
 * MESSAGE FLOW (OUTGOING):
 * 1. Matching engine sends MEClientResponse via lock-free queue
 * 2. Order server consumes from queue
 * 3. Add sequence number
 * 4. Send via TCP to appropriate client
 * 
 * SEQUENCE NUMBER VALIDATION:
 * - Per-client sequence tracking (incoming and outgoing)
 * - Incoming: Expect 1, 2, 3, ... (detect gaps, duplicates)
 * - Outgoing: Send 1, 2, 3, ... (allow client gap detection)
 * - Rejection: Gap detected, duplicate, or wrong client
 * 
 * THREADING:
 * - Single thread (event loop)
 * - Dedicated CPU core (via affinity)
 * - Non-blocking I/O (poll-based)
 * - Lock-free queues for inter-component communication
 * 
 * PERFORMANCE:
 * - Receive latency: 1-5 μs (TCP + validation)
 * - Send latency: 1-3 μs (TCP write)
 * - Throughput: 100K-500K messages/second
 * - Multiple clients: One connection per client (up to 256)
 */

namespace Exchange {
  class OrderServer {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - client_requests: Lock-free queue to matching engine (output)
     * - client_responses: Lock-free queue from matching engine (input)
     * - iface: Network interface to bind (e.g., "eth0", "lo")
     * - port: TCP port to listen on (e.g., 12345)
     * 
     * Initializes:
     * - TCP server (listening socket)
     * - FIFO sequencer (time-priority ordering)
     * - Sequence number trackers (per client)
     * - Client socket map (ClientId -> TCPSocket)
     */
    OrderServer(ClientRequestLFQueue *client_requests, ClientResponseLFQueue *client_responses, const std::string &iface, int port);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleans up:
     * - TCP connections (close sockets)
     * - Thread resources
     */
    ~OrderServer();

    /*
     * START - BEGIN ORDER SERVER THREAD
     * ==================================
     * 
     * Creates and starts order server thread.
     * Sets CPU affinity for consistent performance.
     */
    auto start() -> void;

    /*
     * STOP - SHUTDOWN ORDER SERVER
     * =============================
     * 
     * Stops order server thread gracefully.
     * Closes TCP connections and drains queues.
     */
    auto stop() -> void;

    /*
     * RUN - MAIN EVENT LOOP
     * =====================
     * 
     * Main loop for order server thread.
     * 
     * ALGORITHM:
     * 1. Poll TCP sockets (accept new connections, check for data)
     * 2. Send/receive on all active connections (non-blocking)
     * 3. Process outgoing responses from matching engine:
     *    a. Consume from lock-free queue
     *    b. Add sequence number (per client)
     *    c. Send via TCP to client
     *    d. Increment sequence number
     * 4. Repeat until stopped
     * 
     * TCP POLLING:
     * - tcp_server_.poll(): Accept new connections
     * - tcp_server_.sendAndRecv(): Non-blocking I/O on all sockets
     * - Callbacks: recvCallback, recvFinishedCallback
     * 
     * OUTGOING RESPONSES:
     * - Read from queue until empty (outgoing_responses_->size())
     * - Lookup client socket (cid_tcp_socket_[client_id])
     * - Send sequence number (8 bytes)
     * - Send response (MEClientResponse, ~45 bytes)
     * - Update sequence number (cid_next_outgoing_seq_num_++)
     * 
     * PERFORMANCE MEASUREMENT:
     * - T5t: Order server reads from queue
     * - T6t: Order server writes to TCP
     * - START_MEASURE / END_MEASURE: TCP send latency
     * 
     * ERROR HANDLING:
     * - ASSERT: Client must have TCP socket (internal consistency)
     * - Production: Handle disconnected clients gracefully
     */
    auto run() noexcept {
      // Log thread start
      logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      
      // Main event loop
      while (run_) {
        // Poll for new connections and socket events
        tcp_server_.poll();

        // Non-blocking send/receive on all active connections
        // Triggers recvCallback for each received message
        // Triggers recvFinishedCallback when all receives processed
        tcp_server_.sendAndRecv();

        // Process outgoing responses from matching engine
        for (auto client_response = outgoing_responses_->getNextToRead(); 
             outgoing_responses_->size() && client_response; 
             client_response = outgoing_responses_->getNextToRead()) {
          // Record timestamp (T5t = order server reads response from queue)
          TTT_MEASURE(T5t_OrderServer_LFQueue_read, logger_);

          // Get next sequence number for this client (outgoing)
          auto &next_outgoing_seq_num = cid_next_outgoing_seq_num_[client_response->client_id_];
          
          // Log outgoing response
          logger_.log("%:% %() % Processing cid:% seq:% %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                      client_response->client_id_, next_outgoing_seq_num, client_response->toString());

          // Validate client has TCP connection (internal consistency check)
          ASSERT(cid_tcp_socket_[client_response->client_id_] != nullptr,
                 "Dont have a TCPSocket for ClientId:" + std::to_string(client_response->client_id_));
          
          // Send to client via TCP (two sends: seq number + response)
          START_MEASURE(Exchange_TCPSocket_send);  // Begin send measurement
          cid_tcp_socket_[client_response->client_id_]->send(&next_outgoing_seq_num, sizeof(next_outgoing_seq_num));  // Send seq#
          cid_tcp_socket_[client_response->client_id_]->send(client_response, sizeof(MEClientResponse));  // Send response
          END_MEASURE(Exchange_TCPSocket_send, logger_);  // End send measurement

          // Consume from queue (advance read index)
          outgoing_responses_->updateReadIndex();
          
          // Record timestamp (T6t = order server writes to TCP)
          TTT_MEASURE(T6t_OrderServer_TCP_write, logger_);

          // Increment sequence number for next response
          ++next_outgoing_seq_num;
        }
      }
    }

    /*
     * RECV CALLBACK - TCP RECEIVE HANDLER
     * ====================================
     * 
     * Called by TCP server when data received on a socket.
     * Parses incoming OMClientRequest messages and validates.
     * 
     * ALGORITHM:
     * 1. Check buffer size (at least one full message)
     * 2. Parse messages from buffer (may be multiple)
     * 3. For each message:
     *    a. Validate client ID (first message sets mapping)
     *    b. Validate socket consistency (same client always on same socket)
     *    c. Validate sequence number (no gaps, no duplicates)
     *    d. Forward to FIFO sequencer (with receive timestamp)
     * 4. Shift remaining data in buffer (partial message)
     * 
     * CLIENT ID MAPPING:
     * - First message from client: Store socket mapping
     * - Subsequent messages: Verify same socket
     * - Purpose: Prevent client ID spoofing
     * 
     * SEQUENCE NUMBER VALIDATION:
     * - Per-client expected sequence (cid_next_exp_seq_num_)
     * - Expect: 1, 2, 3, ...
     * - Gap: Skip message, log error (TODO: send reject)
     * - Duplicate: Skip message, log error
     * 
     * BATCHING:
     * - All received messages queued in FIFO sequencer
     * - Not published yet (wait for recvFinishedCallback)
     * - Reason: Allow time-priority sorting across all clients
     * 
     * BUFFER MANAGEMENT:
     * - socket->inbound_data_: Receive buffer
     * - socket->next_rcv_valid_index_: Valid data length
     * - Parse complete messages, keep remainder for next call
     * 
     * PERFORMANCE:
     * - T1: TCP receive timestamp (rx_time parameter)
     * - START_MEASURE / END_MEASURE: FIFO sequencer add
     * 
     * Parameters:
     * - socket: TCP socket that received data
     * - rx_time: Receive timestamp (nanoseconds)
     * 
     * Called by: tcp_server_.sendAndRecv()
     */
    auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept {
      // Record timestamp (T1 = order server TCP receive)
      TTT_MEASURE(T1_OrderServer_TCP_read, logger_);
      
      // Log receive event
      logger_.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                  socket->socket_fd_, socket->next_rcv_valid_index_, rx_time);

      // Check if at least one complete message available
      if (socket->next_rcv_valid_index_ >= sizeof(OMClientRequest)) {
        size_t i = 0;
        
        // Parse all complete messages in buffer
        for (; i + sizeof(OMClientRequest) <= socket->next_rcv_valid_index_; i += sizeof(OMClientRequest)) {
          // Cast buffer to OMClientRequest
          auto request = reinterpret_cast<const OMClientRequest *>(socket->inbound_data_.data() + i);
          
          // Log parsed request
          logger_.log("%:% %() % Received %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), request->toString());

          // First message from this client: Store socket mapping
          if (UNLIKELY(cid_tcp_socket_[request->me_client_request_.client_id_] == nullptr)) {
            cid_tcp_socket_[request->me_client_request_.client_id_] = socket;
          }

          // Validate socket consistency (same client always on same socket)
          if (cid_tcp_socket_[request->me_client_request_.client_id_] != socket) {
            // Different socket for this client (potential spoofing or reconnect)
            // TODO: Send reject to client
            logger_.log("%:% %() % Received ClientRequest from ClientId:% on different socket:% expected:%\n", __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), request->me_client_request_.client_id_, socket->socket_fd_,
                        cid_tcp_socket_[request->me_client_request_.client_id_]->socket_fd_);
            continue;  // Skip this message
          }

          // Get expected sequence number for this client
          auto &next_exp_seq_num = cid_next_exp_seq_num_[request->me_client_request_.client_id_];
          
          // Validate sequence number (detect gaps or duplicates)
          if (request->seq_num_ != next_exp_seq_num) {
            // Sequence number mismatch (gap or duplicate)
            // TODO: Send reject to client
            logger_.log("%:% %() % Incorrect sequence number. ClientId:% SeqNum expected:% received:%\n", __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str_), request->me_client_request_.client_id_, next_exp_seq_num, request->seq_num_);
            continue;  // Skip this message
          }

          // Increment expected sequence number
          ++next_exp_seq_num;

          // Forward to FIFO sequencer (batching, will be sorted by timestamp)
          START_MEASURE(Exchange_FIFOSequencer_addClientRequest);
          fifo_sequencer_.addClientRequest(rx_time, request->me_client_request_);
          END_MEASURE(Exchange_FIFOSequencer_addClientRequest, logger_);
        }
        
        // Shift remaining data to beginning of buffer (incomplete message)
        // Example: Buffer has 1.5 messages, keep 0.5 for next receive
        memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_rcv_valid_index_ - i);
        socket->next_rcv_valid_index_ -= i;  // Update valid data length
      }
    }

    /*
     * RECV FINISHED CALLBACK - BATCH PUBLISH
     * =======================================
     * 
     * Called after all sockets processed in tcp_server_.sendAndRecv().
     * Triggers FIFO sequencer to sort and publish batched requests.
     * 
     * ALGORITHM:
     * 1. Call fifo_sequencer_.sequenceAndPublish()
     * 2. Sequencer sorts pending requests by timestamp
     * 3. Publishes sorted batch to matching engine
     * 
     * WHY BATCHING?
     * - Time-priority fairness: Orders sorted by receive timestamp
     * - Prevents connection-based advantage
     * - Regulatory requirement (MiFID II, Reg NMS)
     * 
     * LATENCY TRADE-OFF:
     * - Batching window: 10-50 μs (accumulated orders)
     * - Sort time: 0.1-1 μs (small batch)
     * - Added latency: 10-50 μs
     * - Acceptable: Fairness more important than nanoseconds
     * 
     * PERFORMANCE:
     * - START_MEASURE / END_MEASURE: Sequence and publish time
     * 
     * Called by: tcp_server_.sendAndRecv() (after all receives)
     */
    auto recvFinishedCallback() noexcept {
      // Sort and publish batched requests to matching engine
      START_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish);
      fifo_sequencer_.sequenceAndPublish();
      END_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish, logger_);
    }

    // Deleted constructors (prevent accidental copies)
    OrderServer() = delete;
    OrderServer(const OrderServer &) = delete;
    OrderServer(const OrderServer &&) = delete;
    OrderServer &operator=(const OrderServer &) = delete;
    OrderServer &operator=(const OrderServer &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Network configuration
    const std::string iface_;  // Network interface ("eth0", "lo", etc.)
    const int port_ = 0;       // TCP port (e.g., 12345)

    // Lock-free queue: Execution reports from matching engine
    ClientResponseLFQueue *outgoing_responses_ = nullptr;

    // Thread control flag
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger
    Logger logger_;

    // Per-client sequence number trackers
    // Outgoing: Next sequence number to send (1, 2, 3, ...)
    std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_outgoing_seq_num_;

    // Incoming: Next sequence number expected (1, 2, 3, ...)
    std::array<size_t, ME_MAX_NUM_CLIENTS> cid_next_exp_seq_num_;

    // Per-client TCP socket mapping (ClientId -> TCPSocket*)
    // Stores which socket belongs to which client
    // Used to send responses and validate requests
    std::array<Common::TCPSocket *, ME_MAX_NUM_CLIENTS> cid_tcp_socket_;

    // TCP server: Accepts connections, manages sockets
    // Non-blocking I/O, poll-based event loop
    Common::TCPServer tcp_server_;

    // FIFO sequencer: Time-priority ordering
    // Batches requests from all clients
    // Sorts by timestamp, publishes to matching engine
    FIFOSequencer fifo_sequencer_;
  };
}

/*
 * ORDER SERVER DESIGN CONSIDERATIONS
 * ===================================
 * 
 * 1. TCP vs UDP:
 *    - TCP chosen: Reliable, ordered delivery
 *    - Critical: Orders must not be lost or reordered
 *    - UDP would require: Retransmission, sequencing, state management
 *    - TCP overhead: Acceptable (1-5 μs, connection-based)
 * 
 * 2. SEQUENCE NUMBER VALIDATION:
 *    - Purpose: Detect lost messages, prevent duplicates
 *    - Per-client tracking: Independent sequences
 *    - Gaps: Log and reject (TODO: Send reject to client)
 *    - Production: More sophisticated gap handling
 * 
 * 3. FIFO SEQUENCING:
 *    - Fairness: All clients treated equally
 *    - Time-priority: Earlier orders processed first
 *    - Batching: Accumulate 10-50 μs, sort, publish
 *    - Regulatory: MiFID II, Reg NMS compliance
 * 
 * 4. BUFFER MANAGEMENT:
 *    - Fixed-size buffers: Pre-allocated (no heap)
 *    - Partial messages: Keep remainder for next receive
 *    - memcpy: Shift data (simple, fast for small sizes)
 *    - Production: Circular buffer (avoid copying)
 * 
 * 5. NON-BLOCKING I/O:
 *    - poll(): Check all sockets for events
 *    - send/recv: Non-blocking (return immediately)
 *    - Busy-wait: Continuously polling (low latency)
 *    - Alternative: epoll (event-driven, more complex)
 * 
 * 6. CLIENT SOCKET MAPPING:
 *    - One socket per client (persistent connection)
 *    - ClientId -> TCPSocket* mapping
 *    - First message: Establishes mapping
 *    - Validation: Prevent client ID spoofing
 * 
 * LATENCY BREAKDOWN (ORDER SUBMISSION):
 * - TCP receive: 0.5-2 μs (kernel -> user space)
 * - Parse + validate: 0.1-0.5 μs
 * - FIFO sequencer add: 0.01-0.05 μs
 * - Batching window: 10-50 μs (intentional delay)
 * - Sort batch: 0.1-1 μs
 * - Queue to matching engine: 0.01-0.02 μs
 * - TOTAL: 11-54 μs (dominated by batching window)
 * 
 * LATENCY BREAKDOWN (EXECUTION REPORT):
 * - Queue read: 0.01-0.02 μs
 * - TCP send: 0.5-2 μs (user space -> kernel)
 * - TOTAL: 0.5-2 μs
 * 
 * THROUGHPUT:
 * - Incoming: 100K-500K orders/second
 * - Outgoing: 100K-500K reports/second
 * - Per client: 10K-50K messages/second typical
 * - Limit: 256 clients (ME_MAX_NUM_CLIENTS)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Reject messages: Send back to client (not just log)
 * - Reconnection: Handle client reconnects gracefully
 * - Heartbeats: Detect dead connections
 * - Flow control: Back-pressure if client too slow
 * - TLS encryption: Secure connections
 * - Authentication: Verify client identity (API keys, certs)
 * - Drop copy: Send reports to third party (clearing firm)
 * - Replay protection: Time windows on sequence numbers
 * - Graceful degradation: Continue on client errors
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) One Thread per Client:
 *    - Simpler per-client code
 *    - Higher latency (thread context switches)
 *    - More memory (stack per thread)
 *    - Not suitable for HFT
 * 
 * B) Event-driven (epoll/kqueue):
 *    - More efficient for many connections
 *    - Slightly higher latency (syscall overhead)
 *    - More complex (callback-based)
 *    - Good for non-HFT systems
 * 
 * C) Kernel bypass (DPDK, Solarflare):
 *    - Lowest latency (bypass kernel TCP stack)
 *    - Complex (custom TCP implementation)
 *    - Expensive (specialized hardware)
 *    - Used in ultra-low-latency HFT (sub-microsecond)
 * 
 * INDUSTRY PRACTICES:
 * - Most exchanges: TCP for orders (reliable)
 * - Some: Binary protocols (FIX, proprietary)
 * - Ultra-low latency: Kernel bypass, FPGA
 * - This implementation: Standard TCP, educational
 */
