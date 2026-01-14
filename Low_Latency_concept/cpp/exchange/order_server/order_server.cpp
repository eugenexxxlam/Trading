#include "order_server.h"

/*
 * ORDER SERVER IMPLEMENTATION
 * ============================
 * 
 * Implementation of OrderServer constructor, destructor, start/stop methods.
 * Main run() loop and callbacks are implemented inline in header.
 * 
 * See order_server.h for detailed class documentation and architecture.
 */

namespace Exchange {
  /*
   * CONSTRUCTOR - INITIALIZE ORDER SERVER
   * ======================================
   * 
   * Sets up TCP server, FIFO sequencer, and per-client state tracking.
   * 
   * ALGORITHM:
   * 1. Store network configuration (interface, port)
   * 2. Store queue pointers (input/output channels)
   * 3. Initialize logger ("exchange_order_server.log")
   * 4. Initialize TCP server (for accepting client connections)
   * 5. Initialize FIFO sequencer (time-priority ordering)
   * 6. Initialize per-client tracking arrays (sequence numbers, sockets)
   * 7. Register TCP callbacks (recv, recvFinished)
   * 
   * SEQUENCE NUMBER INITIALIZATION:
   * - cid_next_outgoing_seq_num_: Start at 1 for each client
   * - cid_next_exp_seq_num_: Start at 1 for each client
   * - Filled with 1 (expect first message seq=1)
   * 
   * SOCKET MAP INITIALIZATION:
   * - cid_tcp_socket_: Start at nullptr for each client
   * - First message from client establishes mapping
   * 
   * TCP CALLBACKS:
   * - recv_callback_: Called when data received on socket
   *   - Lambda: [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); }
   *   - Captures this pointer, calls recvCallback method
   * - recv_finished_callback_: Called after all receives processed
   *   - Lambda: [this]() { recvFinishedCallback(); }
   *   - Triggers FIFO sequencer to publish batch
   * 
   * INITIALIZATION COST:
   * - Allocate logger, TCP server, sequencer: ~1-5 ms
   * - One-time startup cost (acceptable)
   * 
   * Parameters:
   * - client_requests: Lock-free queue to matching engine (output)
   * - client_responses: Lock-free queue from matching engine (input)
   * - iface: Network interface ("eth0", "lo", etc.)
   * - port: TCP port to listen on (e.g., 12345)
   */
  OrderServer::OrderServer(ClientRequestLFQueue *client_requests, ClientResponseLFQueue *client_responses, const std::string &iface, int port)
      : iface_(iface), port_(port),                      // Store network config
        outgoing_responses_(client_responses),           // Queue from matching engine
        logger_("exchange_order_server.log"),            // Initialize logger
        tcp_server_(logger_),                            // Initialize TCP server
        fifo_sequencer_(client_requests, &logger_) {    // Initialize FIFO sequencer
    
    // Initialize per-client sequence numbers (all start at 1)
    cid_next_outgoing_seq_num_.fill(1);  // Outgoing: First response seq=1
    cid_next_exp_seq_num_.fill(1);       // Incoming: First request seq=1
    cid_tcp_socket_.fill(nullptr);       // No sockets mapped yet

    // Register TCP receive callback (called when data arrives)
    // Lambda captures this pointer, calls recvCallback method
    tcp_server_.recv_callback_ = [this](auto socket, auto rx_time) { recvCallback(socket, rx_time); };
    
    // Register TCP receive finished callback (called after all receives)
    // Lambda triggers FIFO sequencer to sort and publish batch
    tcp_server_.recv_finished_callback_ = [this]() { recvFinishedCallback(); };
  }

  /*
   * DESTRUCTOR - CLEANUP ORDER SERVER
   * ==================================
   * 
   * Cleans up order server resources.
   * 
   * ALGORITHM:
   * 1. Stop order server thread (set run_ = false)
   * 2. Wait 1 second (allow thread to exit, drain queues, close sockets)
   * 
   * SLEEP:
   * - 1 second grace period
   * - Allows: Thread exit, queue drain, TCP close, logging finish
   * - Production: Could wait for explicit thread join
   * 
   * TCP CLEANUP:
   * - TCP server destructor closes all sockets
   * - Automatic cleanup (RAII)
   */
  OrderServer::~OrderServer() {
    stop();  // Set run_ = false (stop main loop)

    // Wait for thread to exit and resources to clean up
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);
  }

  /*
   * START - BEGIN ORDER SERVER THREAD
   * ==================================
   * 
   * Starts TCP server and order server thread.
   * 
   * ALGORITHM:
   * 1. Set run_ = true (enable main loop)
   * 2. Start TCP server listening (bind socket, listen for connections)
   * 3. Create thread with run() method
   * 4. Assert thread creation succeeded
   * 
   * TCP LISTEN:
   * - tcp_server_.listen(iface_, port_)
   * - Creates listening socket
   * - Binds to interface and port
   * - Begins accepting connections
   * - Non-blocking (returns immediately)
   * 
   * THREAD:
   * - Name: "Exchange/OrderServer"
   * - CPU affinity: -1 (any core, OS decides)
   * - Production: Pin to dedicated core
   * - Lambda: [this]() { run(); }
   * 
   * ERROR HANDLING:
   * - ASSERT: Thread creation must succeed
   * - Fatal if fails: Cannot accept orders
   * - TCP listen errors: Handled by tcp_server_ (logs error)
   */
  auto OrderServer::start() -> void {
    run_ = true;  // Enable main loop
    
    // Start TCP server listening for connections
    tcp_server_.listen(iface_, port_);

    // Create and start thread
    ASSERT(Common::createAndStartThread(-1, "Exchange/OrderServer", [this]() { run(); }) != nullptr, 
           "Failed to start OrderServer thread.");
  }

  /*
   * STOP - SHUTDOWN ORDER SERVER
   * ==============================
   * 
   * Stops order server thread gracefully.
   * 
   * ALGORITHM:
   * - Set run_ = false
   * - Main loop (run()) checks flag and exits
   * - Thread terminates naturally
   * - Destructor waits and cleans up
   * 
   * GRACEFUL:
   * - No forced termination
   * - Current messages complete
   * - TCP connections closed cleanly
   */
  auto OrderServer::stop() -> void {
    run_ = false;  // Disable main loop
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. INLINE METHODS:
 *    - run(), recvCallback(), recvFinishedCallback()
 *    - Implemented in header (performance)
 *    - Hot path: Called frequently
 *    - Compiler optimization: Inlining, devirtualization
 * 
 * 2. LAMBDA CALLBACKS:
 *    - Modern C++ (C++11+)
 *    - Captures this pointer: [this]
 *    - Advantage: Type-safe, inline-able
 *    - Alternative: Function pointers (older C style)
 * 
 * 3. FILL ARRAYS:
 *    - std::array::fill(value)
 *    - Fills all elements with value
 *    - Alternative: Loop (more verbose)
 *    - Performance: Same (compiler optimizes)
 * 
 * 4. TCP SERVER:
 *    - Common::TCPServer class (see common/tcp_server.h)
 *    - Non-blocking I/O (poll-based)
 *    - Manages multiple connections
 *    - Callbacks for receive events
 * 
 * 5. FIFO SEQUENCER:
 *    - Exchange::FIFOSequencer class (see fifo_sequencer.h)
 *    - Batches orders from all clients
 *    - Sorts by timestamp (time-priority)
 *    - Publishes to matching engine
 * 
 * 6. LOGGER:
 *    - Common::Logger class (see common/logging.h)
 *    - Async logging (lock-free queue)
 *    - File: "exchange_order_server.log"
 *    - Off hot path (non-blocking)
 * 
 * STARTUP SEQUENCE:
 * 1. Constructor: Initialize components
 * 2. start(): Begin TCP listening
 * 3. start(): Create thread
 * 4. run(): Main loop (poll, send/recv, process responses)
 * 5. Callbacks: recvCallback (parse orders), recvFinishedCallback (publish batch)
 * 
 * SHUTDOWN SEQUENCE:
 * 1. stop(): Set run_ = false
 * 2. run(): Exit loop naturally
 * 3. Thread terminates
 * 4. Destructor: Wait 1 second
 * 5. Destructor: Cleanup (automatic RAII)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Thread join: Wait for thread explicitly (not sleep)
 * - TCP errors: Handle bind/listen failures gracefully
 * - Client limits: Enforce max clients (256)
 * - Connection monitoring: Detect dead connections (heartbeat)
 * - Graceful client disconnect: Clean up client state
 * - TLS support: Secure connections (OpenSSL)
 * - Authentication: Verify client identity before accepting orders
 */
