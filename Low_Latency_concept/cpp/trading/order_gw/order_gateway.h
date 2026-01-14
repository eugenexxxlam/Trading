#pragma once

#include <functional>

#include "common/thread_utils.h"
#include "common/macros.h"
#include "common/tcp_server.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"

/*
 * ORDER GATEWAY - TRADING FIRM'S ORDER SUBMISSION INTERFACE
 * ==========================================================
 * 
 * PURPOSE:
 * Trading firm's gateway to the exchange for order submission and execution reports.
 * Maintains TCP connection to exchange, sends orders, receives responses.
 * 
 * RESPONSIBILITIES:
 * 1. Maintain TCP connection to exchange's order server
 * 2. Consume orders from trading engine (via lock-free queue)
 * 3. Send orders to exchange (with sequence numbers)
 * 4. Receive execution reports from exchange
 * 5. Forward responses to trading engine (via lock-free queue)
 * 6. Validate sequence numbers (detect gaps)
 * 
 * ARCHITECTURE:
 * ```
 * Trading Engine --> [LF Queue] --> Order Gateway --TCP--> Exchange Order Server
 *                                        |
 *                                        | (TCP responses)
 *                                        v
 *                                  [LF Queue] --> Trading Engine
 * ```
 * 
 * MESSAGE FLOW (OUTGOING):
 * 1. Trading engine: Generates order (NEW, CANCEL)
 * 2. Enqueues to outgoing_requests_ (lock-free queue)
 * 3. Order gateway: Consumes from queue
 * 4. Adds sequence number (next_outgoing_seq_num_++)
 * 5. Sends via TCP to exchange
 * 
 * MESSAGE FLOW (INCOMING):
 * 1. Exchange: Sends execution report (ACCEPTED, FILLED, etc.)
 * 2. TCP receive callback (recvCallback)
 * 3. Validate: client_id, sequence number
 * 4. Enqueue to incoming_responses_ (lock-free queue)
 * 5. Trading engine: Consumes responses
 * 
 * SEQUENCE NUMBERS:
 * - Outgoing: next_outgoing_seq_num_ (1, 2, 3, ...)
 * - Incoming: next_exp_seq_num_ (1, 2, 3, ...)
 * - Purpose: Detect TCP packet loss (rare but possible)
 * - Action: Log error, continue (TCP should be reliable)
 * 
 * THREADING:
 * - Single thread (dedicated to order flow)
 * - Non-blocking TCP I/O
 * - Lock-free queues (no synchronization)
 * - CPU affinity (can be pinned to dedicated core)
 * 
 * PERFORMANCE:
 * - Order submission: 1-5 μs (queue read + TCP send)
 * - Response processing: 1-3 μs (TCP receive + queue write)
 * - Throughput: 100K-500K orders/second
 * - Latency-critical: Order flow is hot path
 * 
 * RELIABILITY:
 * - TCP: Reliable, ordered delivery
 * - Sequence numbers: Additional validation
 * - Heartbeats: Could add (detect connection loss)
 * - Reconnection: Could add (automatic recovery)
 */

namespace Trading {
  class OrderGateway {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Initializes order gateway with connection parameters.
     * 
     * Parameters:
     * - client_id: Trading firm's client identifier
     * - client_requests: Lock-free queue from trading engine (orders to send)
     * - client_responses: Lock-free queue to trading engine (responses received)
     * - ip: Exchange order server IP address
     * - iface: Network interface ("eth0", "lo", etc.)
     * - port: Exchange order server port
     * 
     * Initializes:
     * - TCP socket (not connected yet, connect in start())
     * - Logger ("trading_order_gateway_<client_id>.log")
     * - Sequence numbers (start at 1)
     * - Receive callback (for TCP responses)
     */
    OrderGateway(ClientId client_id,
                 Exchange::ClientRequestLFQueue *client_requests,
                 Exchange::ClientResponseLFQueue *client_responses,
                 std::string ip, const std::string &iface, int port);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleans up order gateway resources.
     * 
     * ALGORITHM:
     * 1. Stop thread (set run_ = false)
     * 2. Wait 5 seconds (drain queues, close TCP)
     * 
     * SLEEP:
     * - 5 seconds: Allow pending orders/responses to complete
     * - TCP close: Automatic (socket destructor)
     */
    ~OrderGateway() {
      stop();  // Stop thread

      // Wait for pending operations to complete
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(5s);
    }

    /*
     * START - BEGIN ORDER GATEWAY THREAD
     * ===================================
     * 
     * Connects to exchange and starts order gateway thread.
     * 
     * ALGORITHM:
     * 1. Set run_ = true (enable main loop)
     * 2. Connect TCP socket to exchange
     *    - ip_: Exchange server IP
     *    - port_: Exchange server port
     *    - iface_: Local network interface
     *    - blocking = false (non-blocking I/O)
     * 3. Assert connection succeeded
     * 4. Create and start thread (run() method)
     * 5. Assert thread creation succeeded
     * 
     * TCP CONNECTION:
     * - Non-blocking: Returns immediately (async connect)
     * - Persistent: Connection remains open (not request/response)
     * - Failure: ASSERT aborts (cannot operate without connection)
     * - Production: Retry logic, exponential backoff
     * 
     * THREAD:
     * - Name: "Trading/OrderGateway"
     * - CPU affinity: -1 (any core, OS decides)
     * - Production: Pin to dedicated core (consistent performance)
     * - Lambda: [this]() { run(); }
     * 
     * ERROR HANDLING:
     * - ASSERT: Connection must succeed
     * - errno: System error code (from socket connect)
     * - Fatal if fails: Cannot send orders
     * - Production: Graceful error handling, reconnection
     */
    auto start() {
      run_ = true;  // Enable main loop
      
      // Connect TCP socket to exchange order server
      // connect(ip, iface, port, blocking=false)
      ASSERT(tcp_socket_.connect(ip_, iface_, port_, false) >= 0,
             "Unable to connect to ip:" + ip_ + " port:" + std::to_string(port_) + 
             " on iface:" + iface_ + " error:" + std::string(std::strerror(errno)));
      
      // Create and start thread
      ASSERT(Common::createAndStartThread(-1, "Trading/OrderGateway", [this]() { run(); }) != nullptr, 
             "Failed to start OrderGateway thread.");
    }

    /*
     * STOP - SHUTDOWN ORDER GATEWAY
     * ==============================
     * 
     * Stops order gateway thread gracefully.
     * 
     * ALGORITHM:
     * - Set run_ = false
     * - Main loop (run()) checks flag and exits
     * - Thread terminates naturally
     * - Destructor waits and cleans up
     */
    auto stop() -> void {
      run_ = false;  // Disable main loop
    }

    // Deleted constructors (prevent accidental copies)
    OrderGateway() = delete;
    OrderGateway(const OrderGateway &) = delete;
    OrderGateway(const OrderGateway &&) = delete;
    OrderGateway &operator=(const OrderGateway &) = delete;
    OrderGateway &operator=(const OrderGateway &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Trading firm's client identifier
    // Used to identify this client to the exchange
    const ClientId client_id_;

    // Exchange order server TCP address
    std::string ip_;               // IP address (e.g., "192.168.1.100")
    const std::string iface_;      // Network interface (e.g., "eth0")
    const int port_ = 0;           // TCP port (e.g., 12345)

    // Lock-free queue: Orders from trading engine (input)
    // Trading engine produces, order gateway consumes
    // Capacity: Configured by trading engine
    Exchange::ClientRequestLFQueue *outgoing_requests_ = nullptr;

    // Lock-free queue: Execution reports to trading engine (output)
    // Order gateway produces, trading engine consumes
    // Capacity: Configured by trading engine
    Exchange::ClientResponseLFQueue *incoming_responses_ = nullptr;

    // Thread control flag
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (per-client log file)
    // File: "trading_order_gateway_<client_id>.log"
    Logger logger_;

    // Sequence number tracking (per-client session)
    // Outgoing: Sequence number for next order to send (1, 2, 3, ...)
    size_t next_outgoing_seq_num_ = 1;
    
    // Incoming: Expected sequence number for next response (1, 2, 3, ...)
    size_t next_exp_seq_num_ = 1;

    // TCP socket: Connection to exchange order server
    // Non-blocking I/O (poll-based)
    // Persistent connection (not closed between orders)
    Common::TCPSocket tcp_socket_;

  private:
    /*
     * PRIVATE METHODS
     * ===============
     */
    
    /*
     * RUN - MAIN ORDER GATEWAY LOOP
     * ==============================
     * 
     * Main loop for order gateway thread.
     * Sends orders to exchange and receives responses.
     * 
     * ALGORITHM:
     * 1. Log thread start
     * 2. While run_ is true:
     *    a. Poll TCP socket (send/receive non-blocking)
     *    b. Consume orders from queue (outgoing_requests_)
     *    c. For each order:
     *       - Log order
     *       - Send sequence number (8 bytes)
     *       - Send order (MEClientRequest, ~30 bytes)
     *       - Update read index (consume from queue)
     *       - Increment sequence number
     * 3. TCP receive callback (recvCallback) handles incoming responses
     * 
     * TCP SEND/RECV:
     * - sendAndRecv(): Non-blocking I/O on TCP socket
     * - Triggers recvCallback when data received
     * - Flushes outgoing data (actual network send)
     * 
     * ORDER SENDING:
     * - Two TCP sends per order:
     *   1. Sequence number (size_t, 8 bytes)
     *   2. Order (MEClientRequest, ~30 bytes)
     * - Total: 38 bytes per order
     * - Could combine: Single send (struct with both fields)
     * 
     * PERFORMANCE MEASUREMENT:
     * - T11: Order gateway reads from queue
     * - T12: Order gateway writes to TCP
     * - START_MEASURE / END_MEASURE: TCP send latency
     * 
     * BUSY-WAIT:
     * - Continuously polls (even if no orders)
     * - 100% CPU utilization (acceptable for trading)
     * - Low latency (no sleep, no blocking)
     * 
     * NOEXCEPT:
     * - No exception handling overhead
     * - Performance-critical path
     * 
     * Implemented in order_gateway.cpp (see for details).
     */
    auto run() noexcept -> void;

    /*
     * RECV CALLBACK - PROCESS EXECUTION REPORTS
     * ==========================================
     * 
     * Called by TCP socket when execution report received from exchange.
     * Validates and forwards to trading engine.
     * 
     * ALGORITHM:
     * 1. Record timestamp (T7t)
     * 2. Log receive event
     * 3. Parse incoming buffer (may contain multiple responses)
     * 4. For each OMClientResponse:
     *    a. Log response
     *    b. Validate client_id (must match this client)
     *    c. Validate sequence number (must be next expected)
     *    d. If valid: Enqueue to incoming_responses_ (to trading engine)
     *    e. If invalid: Log error, skip
     *    f. Increment expected sequence number
     * 5. Shift remaining data in buffer (partial response)
     * 
     * RESPONSE STRUCTURE:
     * - OMClientResponse: size_t seq_num + MEClientResponse (~53 bytes)
     * - May receive multiple in one TCP receive
     * - Must parse all complete responses
     * 
     * VALIDATION:
     * - client_id: Must match this client (sanity check)
     *   - Failure: Exchange bug (should never happen)
     * - seq_num: Must be consecutive (1, 2, 3, ...)
     *   - Failure: TCP dropped packet (rare) or exchange bug
     *   - Action: Log error, continue (skip this response)
     * 
     * BUFFER MANAGEMENT:
     * - socket->inbound_data_: Receive buffer
     * - socket->next_rcv_valid_index_: Valid data length
     * - Parse complete responses, keep remainder for next call
     * - memcpy: Shift remaining data to beginning
     * 
     * PERFORMANCE:
     * - T7t: TCP receive timestamp
     * - T8t: Queue write timestamp
     * - START_MEASURE / END_MEASURE: Callback latency
     * 
     * Parameters:
     * - socket: TCP socket that received data
     * - rx_time: Receive timestamp (nanoseconds)
     * 
     * NOEXCEPT:
     * - No exceptions on hot path
     * 
     * Implemented in order_gateway.cpp (see for details).
     */
    auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept -> void;
  };
}

/*
 * ORDER GATEWAY DESIGN CONSIDERATIONS
 * ====================================
 * 
 * 1. TCP CONNECTION:
 *    - Persistent: One connection per client (not per order)
 *    - Advantage: No connection overhead, lower latency
 *    - Disadvantage: Connection loss requires reconnection
 *    - Production: Heartbeats, automatic reconnection
 * 
 * 2. SEQUENCE NUMBERS:
 *    - Per-client session (reset on reconnect)
 *    - Purpose: Detect message loss (TCP should be reliable)
 *    - Rare: TCP drops are very rare (0.01% typical)
 *    - Action: Log error (may indicate network issue)
 * 
 * 3. TWO TCP SENDS:
 *    - Current: Separate sends for seq# and order
 *    - Advantage: Simple code
 *    - Disadvantage: Two syscalls (slightly slower)
 *    - Alternative: Combined struct (one send)
 *    ```cpp
 *    struct OMClientRequestWire {
 *      size_t seq_num;
 *      MEClientRequest request;
 *    };
 *    ```
 * 
 * 4. LOCK-FREE QUEUES:
 *    - Why: No locking overhead (10-100x faster than mutex)
 *    - SPSC: Single producer, single consumer
 *    - Latency: 10-20 ns (vs 10-100 μs for mutex)
 *    - Critical: Order flow is latency-sensitive
 * 
 * 5. NON-BLOCKING I/O:
 *    - sendAndRecv(): Poll-based (no blocking)
 *    - Advantage: Low latency, predictable
 *    - Disadvantage: CPU usage (busy-wait)
 *    - Acceptable: Dedicated core, trading priority
 * 
 * 6. ERROR HANDLING:
 *    - Current: Log and continue (skip invalid responses)
 *    - Production: More sophisticated:
 *      • Reconnection logic (exponential backoff)
 *      • Request order status (reconciliation)
 *      • Alert monitoring system (network issues)
 *      • Graceful degradation (backup exchange)
 * 
 * LATENCY BREAKDOWN (ORDER SUBMISSION):
 * - Queue read: 10-20 ns (lock-free)
 * - TCP send (2 calls): 0.5-2 μs (kernel + buffering)
 * - Network (LAN): 0.1-1 μs (co-located)
 * - Exchange receive: 1-5 μs (order server processing)
 * - TOTAL: 2-8 μs (trading engine to exchange order book)
 * 
 * LATENCY BREAKDOWN (EXECUTION REPORT):
 * - Exchange send: 1-5 μs (order server processing)
 * - Network (LAN): 0.1-1 μs (co-located)
 * - TCP receive: 0.5-2 μs (kernel + user space)
 * - Callback processing: 0.1-0.5 μs (validation + queue write)
 * - Queue write: 10-20 ns (lock-free)
 * - TOTAL: 2-9 μs (exchange order book to trading engine)
 * 
 * ROUND-TRIP LATENCY:
 * - Order submission + execution report: 4-17 μs
 * - Co-located: Typically 5-10 μs (best case)
 * - Industry: Sub-10 μs is competitive for co-located trading
 * 
 * THROUGHPUT:
 * - Orders/second: 100K-500K (per client)
 * - Limited by: TCP throughput, network bandwidth
 * - Typical: 10K-50K orders/second (normal trading)
 * - Peak: 100K+ orders/second (high-frequency trading)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Heartbeats: Periodic messages (detect connection loss)
 * - Reconnection: Automatic (exponential backoff)
 * - Order status: Request from exchange (reconciliation)
 * - Drop copy: Send orders to risk system (compliance)
 * - Pre-trade risk: Validate orders before sending
 * - Rate limiting: Prevent excessive orders (fat finger protection)
 * - Monitoring: Metrics (latency, throughput, errors)
 * - Alerting: Notify on errors (sequence gaps, connection loss)
 * - FIX protocol: Industry standard (not custom binary)
 * - Encryption: TLS (secure connections)
 * - Authentication: API keys, certificates
 * - Redundancy: Multiple connections (primary, backup)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) FIX Protocol:
 *    - Industry standard (Financial Information eXchange)
 *    - Advantage: Standardized, interoperable
 *    - Disadvantage: Text-based (slower), complex parsing
 *    - Use case: Institutional trading (not ultra-HFT)
 * 
 * B) Kernel Bypass:
 *    - DPDK, Solarflare, Onload
 *    - Advantage: Sub-microsecond latency
 *    - Disadvantage: Complex, expensive hardware
 *    - Use case: Ultra-low-latency HFT (market makers)
 * 
 * C) UDP:
 *    - Advantage: Lower latency (no TCP overhead)
 *    - Disadvantage: Unreliable (need custom reliability)
 *    - Rare: Most exchanges require TCP for orders
 * 
 * REGULATORY REQUIREMENTS:
 * - MiFID II: Record all orders (timestamps, client ID)
 * - SEC CAT: Consolidated audit trail
 * - Timestamps: Microsecond precision mandatory
 * - Audit: Log every order and response
 * - Compliance: Pre-trade risk checks
 */
