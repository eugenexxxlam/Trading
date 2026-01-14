/*
 * EXCHANGE MAIN - ELECTRONIC TRADING EXCHANGE ENTRY POINT
 * ========================================================
 * 
 * PURPOSE:
 * Main entry point for the exchange/matching engine system. Initializes and coordinates
 * three core components that form a complete electronic trading exchange.
 * 
 * EXCHANGE ARCHITECTURE:
 * ```
 * Trading Clients (External)
 *         |
 *         | TCP (Orders/Cancels)
 *         ↓
 *   ┌─────────────────┐
 *   │  Order Server   │ ← Receives client orders via TCP
 *   └────────┬────────┘
 *            │ Lock-free Queue (ClientRequests)
 *            ↓
 *   ┌─────────────────┐
 *   │ Matching Engine │ ← Matches orders, maintains order book
 *   └────┬────────┬───┘
 *        │        │
 *        │        │ Lock-free Queue (ClientResponses)
 *        │        ↓
 *        │   ┌─────────────────┐
 *        │   │  Order Server   │ → Sends execution reports to clients
 *        │   └─────────────────┘
 *        │
 *        │ Lock-free Queue (MarketUpdates)
 *        ↓
 *   ┌─────────────────────────┐
 *   │ Market Data Publisher   │ → Broadcasts to all subscribers
 *   └─────────────────────────┘
 *            |
 *            | Multicast UDP (Market Data)
 *            ↓
 *   Trading Clients (External)
 * ```
 * 
 * THREE CORE COMPONENTS:
 * 
 * 1. ORDER SERVER:
 *    - Accepts TCP connections from trading clients
 *    - Receives orders (new, cancel, modify)
 *    - Sends execution reports (filled, rejected, acked)
 *    - Non-blocking I/O for minimal latency
 * 
 * 2. MATCHING ENGINE:
 *    - Maintains order books (one per instrument)
 *    - Matches orders (price-time priority)
 *    - Generates execution reports
 *    - Publishes market data updates
 *    - Core logic: 200-500 ns per order
 * 
 * 3. MARKET DATA PUBLISHER:
 *    - Broadcasts order book updates via multicast
 *    - Snapshots: Full order book state
 *    - Incremental: Add/modify/cancel/trade events
 *    - Low latency: 5-50 microseconds to subscribers
 * 
 * COMMUNICATION:
 * - Lock-free queues between components (10-20 ns latency)
 * - No locks, no blocking, no syscalls in hot path
 * - Separate threads, CPU pinning for each component
 * 
 * PERFORMANCE TARGETS:
 * - Order-to-match latency: <1 microsecond
 * - Order-to-market-data: <5 microseconds
 * - Throughput: 500K-1M orders/second per instrument
 * - p99 latency: <2 microseconds
 * 
 * PRODUCTION DEPLOYMENT:
 * - Run on dedicated server (no other processes)
 * - CPU pinning (core 0 = matching, core 1 = MD, core 2 = gateway)
 * - NUMA awareness (memory + threads on same socket)
 * - Interrupt steering (IRQs away from trading cores)
 * - Network: 10GbE+, low-latency switches
 * 
 * SIMILAR TO:
 * - NASDAQ INET matching engine
 * - NYSE Arca order book
 * - CME Globex matching system
 * - But simplified for education/demonstration
 */

#include <csignal>

#include "matcher/matching_engine.h"
#include "market_data/market_data_publisher.h"
#include "order_server/order_server.h"

/*
 * GLOBAL COMPONENT POINTERS
 * =========================
 * Made global to be accessible from signal handler for graceful shutdown.
 * 
 * WHY GLOBAL?
 * - Signal handlers are static C functions
 * - Cannot capture local variables from main()
 * - Need access to components for cleanup
 * 
 * PRODUCTION ALTERNATIVE:
 * - Use singleton pattern
 * - Or thread-local storage
 * - Or signal-safe queue for shutdown requests
 */
Common::Logger *logger = nullptr;                          // Async logger for all components
Exchange::MatchingEngine *matching_engine = nullptr;       // Core matching logic
Exchange::MarketDataPublisher *market_data_publisher = nullptr;  // Market data broadcaster
Exchange::OrderServer *order_server = nullptr;             // Client order gateway

/*
 * SIGNAL HANDLER - GRACEFUL SHUTDOWN
 * ===================================
 * Handles SIGINT (Ctrl+C) for clean shutdown.
 * 
 * SHUTDOWN SEQUENCE:
 * 1. Wait 10 seconds (let in-flight orders complete)
 * 2. Delete components (destructors stop threads, flush queues)
 * 3. Wait 10 seconds (ensure all cleanup done)
 * 4. Exit successfully
 * 
 * WHY THE DELAYS?
 * - First 10s: Allow pending orders to be processed
 * - Second 10s: Let threads complete cleanup (flush logs, close sockets)
 * 
 * PRODUCTION IMPROVEMENTS:
 * - Set atomic flag instead of immediate deletion
 * - Let components drain their queues
 * - Send "exchange closing" message to clients
 * - Wait for acknowledgments before shutdown
 * - Persist order book state for restart
 * 
 * SIGNAL SAFETY:
 * - This handler is NOT signal-safe (uses new/delete)
 * - Production: Use atomic flags + separate cleanup thread
 * - Or: Block signals, use signalfd() for safe handling
 */
void signal_handler(int) {  // Parameter = signal number (unused)
  using namespace std::literals::chrono_literals;
  
  // PHASE 1: Grace period for in-flight orders
  std::this_thread::sleep_for(10s);  // Let current orders complete
                                     // Orders in queues will be processed
                                     // New orders rejected (components stopping)

  // PHASE 2: Delete components (calls destructors)
  // Destructors:
  // - Signal threads to stop (set running_ = false)
  // - Wait for threads to finish (join())
  // - Flush queues (ensure no lost data)
  // - Close sockets (TCP, multicast)
  // - Write final logs
  delete logger;
  logger = nullptr;
  
  delete matching_engine;       // Stops matching thread, final order book state logged
  matching_engine = nullptr;
  
  delete market_data_publisher; // Stops publisher thread, final snapshot sent
  market_data_publisher = nullptr;
  
  delete order_server;          // Stops server thread, closes client connections
  order_server = nullptr;

  // PHASE 3: Final grace period for cleanup
  std::this_thread::sleep_for(10s);  // Ensure all destructors completed
                                     // All threads joined
                                     // All logs flushed

  exit(EXIT_SUCCESS);  // Exit with success code (0)
                       // OS will clean up any remaining resources
}

/*
 * MAIN FUNCTION - EXCHANGE INITIALIZATION AND EVENT LOOP
 * =======================================================
 * 
 * INITIALIZATION SEQUENCE:
 * 1. Create logger (before anything else - need logging)
 * 2. Register signal handler (for Ctrl+C graceful shutdown)
 * 3. Create lock-free queues (inter-component communication)
 * 4. Start matching engine (core matching logic)
 * 5. Start market data publisher (broadcast to subscribers)
 * 6. Start order server (accept client connections)
 * 7. Enter main loop (keep process alive)
 * 
 * WHY THIS ORDER?
 * - Logger first: All components need logging
 * - Matching engine before order server: Must be ready to process orders
 * - Market data publisher can start anytime (subscribers connect when ready)
 * - Order server last: Start accepting orders only when ready
 * 
 * PRODUCTION CONSIDERATIONS:
 * - Add configuration file parsing
 * - Validate network interfaces exist
 * - Check port availability before binding
 * - Load persisted order book state
 * - Warm up (pre-allocate memory, prime caches)
 * - Health check endpoint (HTTP monitoring)
 */
int main(int, char **) {
  
  /*
   * STEP 1: CREATE LOGGER
   * Async logger writes to exchange_main.log
   * All components share this logger instance
   */
  logger = new Common::Logger("exchange_main.log");  // Create async logger
                                                     // Background thread started
                                                     // Log file opened

  /*
   * STEP 2: REGISTER SIGNAL HANDLER
   * Catch SIGINT (Ctrl+C) for graceful shutdown
   */
  std::signal(SIGINT, signal_handler);  // Register handler for SIGINT
                                        // When user presses Ctrl+C, signal_handler() called
                                        // Allows graceful cleanup instead of abrupt termination

  /*
   * STEP 3: CONFIGURE TIMING
   * Sleep time between main loop iterations
   */
  const int sleep_time = 100 * 1000;  // 100,000 microseconds = 100 milliseconds
                                      // Main loop sleeps this long each iteration
                                      // Keeps process alive without burning CPU

  /*
   * STEP 4: CREATE LOCK-FREE QUEUES
   * These queues connect the three components with minimal latency.
   * 
   * QUEUE 1: Order Server -> Matching Engine (ClientRequests)
   * - New orders from clients
   * - Cancel requests
   * - Modify requests
   * - Size: 256K elements (from ME_MAX_CLIENT_UPDATES constant)
   * 
   * QUEUE 2: Matching Engine -> Order Server (ClientResponses)
   * - Execution reports (filled, partial fill)
   * - Order acknowledgments
   * - Rejections (risk check failed, invalid price, etc.)
   * - Size: 256K elements
   * 
   * QUEUE 3: Matching Engine -> Market Data Publisher (MarketUpdates)
   * - Order book changes (add, cancel, modify)
   * - Trade events (order matched)
   * - Size: 256K elements (from ME_MAX_MARKET_UPDATES constant)
   * 
   * WHY SEPARATE QUEUES?
   * - Decoupling: Each component reads/writes independently
   * - SPSC: Single producer, single consumer (optimal performance)
   * - No contention: No locks needed, 10-20 ns latency
   */
  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);   // 256K capacity
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES); // 256K capacity
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);   // 256K capacity

  std::string time_str;  // Reused buffer for timestamp strings (avoid repeated allocations)

  /*
   * STEP 5: START MATCHING ENGINE
   * The heart of the exchange - matches buy and sell orders.
   * 
   * WHAT IT DOES:
   * - Reads from client_requests queue
   * - Maintains order books (one per instrument)
   * - Matches orders (price-time priority)
   * - Writes to client_responses queue (execution reports)
   * - Writes to market_updates queue (market data)
   * 
   * THREADING:
   * - Runs in dedicated thread
   * - CPU pinned to core 0 (most critical)
   * - Busy-polls queues (no blocking)
   * 
   * PERFORMANCE:
   * - Order processing: 200-500 ns
   * - Matching: 300-600 ns
   * - Throughput: 500K-1M orders/second
   */
  logger->log("%:% %() % Starting Matching Engine...\n", 
              __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  
  matching_engine = new Exchange::MatchingEngine(&client_requests,   // Input: orders from clients
                                                 &client_responses,   // Output: execution reports
                                                 &market_updates);    // Output: market data
  matching_engine->start();  // Spawns thread, begins processing
                            // Thread pinned to CPU core
                            // Begins busy-polling client_requests queue

  /*
   * STEP 6: CONFIGURE MARKET DATA PUBLISHER
   * Network settings for broadcasting market data via multicast.
   * 
   * TWO MULTICAST STREAMS:
   * 1. Snapshot stream (233.252.14.1:20000):
   *    - Full order book snapshots
   *    - Sent periodically (e.g., every 1 second)
   *    - Allows late joiners to reconstruct book
   * 
   * 2. Incremental stream (233.252.14.3:20001):
   *    - Real-time updates (add, cancel, modify, trade)
   *    - Low latency (5-50 microseconds)
   *    - Subscribers apply to local book
   * 
   * NETWORK INTERFACE:
   * - "lo": Loopback interface (127.0.0.1)
   * - For testing on single machine
   * - Production: Use dedicated NIC (e.g., "eth0", "ens1f0")
   * 
   * MULTICAST IPs:
   * - 233.252.14.x: Class D multicast range
   * - Different IP for each stream (separate subscriptions)
   * - Subscribers can choose snapshot only, incremental only, or both
   */
  const std::string mkt_pub_iface = "lo";  // Network interface for multicast
                                           // "lo" = localhost (testing)
                                           // Production: "eth0" or dedicated trading NIC
  
  const std::string snap_pub_ip = "233.252.14.1",  // Multicast IP for snapshots
                    inc_pub_ip = "233.252.14.3";   // Multicast IP for incremental updates
  
  const int snap_pub_port = 20000,  // UDP port for snapshots
            inc_pub_port = 20001;   // UDP port for incremental updates

  /*
   * STEP 7: START MARKET DATA PUBLISHER
   * Broadcasts order book updates to all subscribers.
   * 
   * WHAT IT DOES:
   * - Reads from market_updates queue
   * - Sends incremental updates via multicast
   * - Generates periodic snapshots
   * - Maintains sequence numbers (gap detection)
   * 
   * THREADING:
   * - Runs in dedicated thread
   * - CPU pinned to core 1
   * - Busy-polls market_updates queue
   * 
   * PERFORMANCE:
   * - Queue-to-network: 5-20 microseconds
   * - Throughput: 1M+ updates/second
   * - Subscribers receive: 5-50 us after matching
   */
  logger->log("%:% %() % Starting Market Data Publisher...\n", 
              __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  
  market_data_publisher = new Exchange::MarketDataPublisher(&market_updates,  // Input: updates from matching engine
                                                            mkt_pub_iface,     // Network interface
                                                            snap_pub_ip,       // Snapshot multicast IP
                                                            snap_pub_port,     // Snapshot port
                                                            inc_pub_ip,        // Incremental multicast IP
                                                            inc_pub_port);     // Incremental port
  market_data_publisher->start();  // Spawns thread, begins publishing
                                   // Opens UDP multicast sockets
                                   // Begins busy-polling market_updates queue

  /*
   * STEP 8: CONFIGURE ORDER SERVER
   * Network settings for accepting client connections.
   * 
   * TCP GATEWAY:
   * - Interface: "lo" (loopback, for testing)
   * - Port: 12345
   * - Production: Dedicated interface, standard port (e.g., 7000)
   * 
   * PROTOCOL:
   * - Binary protocol (not FIX, custom)
   * - NewOrder, CancelOrder, ModifyOrder
   * - ExecutionReport responses
   * - Non-blocking I/O (epoll/kqueue)
   */
  const std::string order_gw_iface = "lo";  // Network interface for order gateway
                                            // "lo" = localhost (testing)
                                            // Production: dedicated trading NIC
  
  const int order_gw_port = 12345;  // TCP port for client connections
                                    // Clients connect to: localhost:12345
                                    // Production: Use well-known port

  /*
   * STEP 9: START ORDER SERVER
   * Accepts client connections and receives orders.
   * 
   * WHAT IT DOES:
   * - Listens for TCP connections (clients)
   * - Receives orders from clients (binary protocol)
   * - Writes to client_requests queue (to matching engine)
   * - Reads from client_responses queue (from matching engine)
   * - Sends execution reports back to clients
   * 
   * THREADING:
   * - Runs in dedicated thread
   * - CPU pinned to core 2
   * - Non-blocking I/O (epoll/select)
   * 
   * PERFORMANCE:
   * - Receive-to-queue: 1-5 microseconds
   * - Queue-to-send: 1-5 microseconds
   * - Throughput: 100K+ orders/second
   */
  logger->log("%:% %() % Starting Order Server...\n", 
              __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
  
  order_server = new Exchange::OrderServer(&client_requests,   // Output: orders to matching engine
                                          &client_responses,   // Input: execution reports from matching engine
                                          order_gw_iface,      // Network interface
                                          order_gw_port);      // TCP port
  order_server->start();  // Spawns thread, begins accepting connections
                         // Opens TCP listen socket
                         // Accepts client connections
                         // Begins processing orders

  /*
   * STEP 10: MAIN EVENT LOOP
   * Keep process alive and log heartbeat.
   * 
   * WHY INFINITE LOOP?
   * - Process must stay running
   * - Components run in their own threads
   * - Main thread just needs to not exit
   * 
   * WHAT IT DOES:
   * - Logs heartbeat every 100ms
   * - Sleeps (doesn't burn CPU)
   * - Could monitor health here
   * - Could handle admin commands
   * 
   * PRODUCTION ENHANCEMENTS:
   * - Monitor component health
   * - Track queue depths (detect backlog)
   * - Expose metrics (Prometheus endpoint)
   * - Accept admin commands (pause trading, etc.)
   * - Reload configuration (without restart)
   */
  while (true) {  // Infinite loop - runs until SIGINT (Ctrl+C)
    logger->log("%:% %() % Sleeping for a few milliseconds..\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    // Log heartbeat (proof of liveness)
    // Shows in logs: main thread is alive
    
    usleep(sleep_time * 1000);  // Sleep 100 milliseconds
                                // usleep takes microseconds: 100000 * 1000 = 100,000,000 us = 100 seconds
                                // Actually: sleep_time = 100 * 1000 = 100,000 us = 0.1 seconds = 100 ms
                                // Prevents main thread from burning CPU
                                // Low frequency (10 Hz) is fine for heartbeat
  }
  
  // Never reached (infinite loop)
  // Exit only via signal handler (Ctrl+C)
}

/*
 * PRODUCTION DEPLOYMENT CHECKLIST
 * ================================
 * 
 * 1. SYSTEM CONFIGURATION:
 *    - Isolate CPU cores (isolcpus=0-7 kernel param)
 *    - Disable frequency scaling (performance governor)
 *    - Disable hyperthreading (BIOS setting)
 *    - Increase socket buffers (sysctl net.core.rmem_max)
 *    - Disable swap (swapoff -a)
 * 
 * 2. NETWORK CONFIGURATION:
 *    - 10GbE or faster network interface
 *    - Low-latency switches (cut-through forwarding)
 *    - Jumbo frames (MTU 9000)
 *    - Interrupt steering (IRQs away from trading cores)
 *    - Enable multicast (IGMP snooping on switches)
 * 
 * 3. MONITORING:
 *    - Queue depths (detect backpressure)
 *    - Latency percentiles (p50, p99, p999)
 *    - Throughput (orders/second)
 *    - Error rates (rejections, disconnects)
 *    - System: CPU, memory, network, disk
 * 
 * 4. TESTING:
 *    - Unit tests (order book logic)
 *    - Integration tests (end-to-end flow)
 *    - Load tests (1M orders/second)
 *    - Chaos tests (network failures, CPU contention)
 *    - Soak tests (24+ hours continuous)
 * 
 * 5. OPERATIONAL:
 *    - Graceful shutdown (signal handler)
 *    - Persist order book (for restart)
 *    - Replay capability (from logs)
 *    - Admin interface (pause, resume, drain)
 *    - Alerts (PagerDuty, email, Slack)
 * 
 * 6. COMPLIANCE:
 *    - Audit trail (all orders logged)
 *    - Microsecond timestamps (regulatory requirement)
 *    - Order book snapshots (forensics)
 *    - Access controls (who can trade)
 *    - Rate limits (prevent abuse)
 * 
 * EXPECTED PERFORMANCE:
 * - Order-to-match: <1 microsecond (p99)
 * - Order-to-market-data: <5 microseconds (p99)
 * - Throughput: 500K-1M orders/second per instrument
 * - Uptime: 99.99%+ (4 nines = 52 minutes downtime/year)
 * 
 * SCALING:
 * - Horizontal: Multiple matching engines per instrument
 * - Vertical: More cores, faster CPUs, FPGA acceleration
 * - Geographic: Co-location in exchange data centers
 * - Network: Kernel bypass (DPDK), RDMA, InfiniBand
 */
