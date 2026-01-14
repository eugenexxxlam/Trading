/*
 * TRADING MAIN - TRADING FIRM APPLICATION ENTRY POINT
 * ====================================================
 * 
 * PURPOSE:
 * Main entry point for the trading firm's application.
 * Orchestrates all trading components: strategy, order gateway, market data.
 * 
 * ARCHITECTURE:
 * ```
 * Exchange                          Trading Firm (this application)
 * --------                          ----------------------------------
 * Order Server <--TCP--> Order Gateway <--LFQueue--> Trade Engine
 * 
 * Market Data  --Mcast-> Market Data   <--LFQueue--> Trade Engine
 * Publisher              Consumer                         |
 *                                                          v
 *                                                   Trading Strategy
 *                                                   (Market Maker or
 *                                                    Liquidity Taker)
 * ```
 * 
 * COMPONENTS:
 * 
 * 1. Trade Engine:
 *    - Core trading logic
 *    - Manages strategies (market maker, liquidity taker, random)
 *    - Consumes market data, produces orders
 * 
 * 2. Order Gateway:
 *    - TCP connection to exchange order server
 *    - Sends orders, receives execution reports
 *    - Bidirectional communication
 * 
 * 3. Market Data Consumer:
 *    - UDP multicast subscriber
 *    - Receives market updates (order book, trades)
 *    - Handles gap recovery (snapshot synchronization)
 * 
 * LOCK-FREE QUEUES (INTER-THREAD COMMUNICATION):
 * 
 * A) client_requests (Trade Engine -> Order Gateway):
 *    - Orders to send to exchange (NEW, CANCEL)
 *    - SPSC: Single producer (trade engine), single consumer (order gateway)
 * 
 * B) client_responses (Order Gateway -> Trade Engine):
 *    - Execution reports from exchange (ACCEPTED, FILLED, CANCELED)
 *    - SPSC: Single producer (order gateway), single consumer (trade engine)
 * 
 * C) market_updates (Market Data Consumer -> Trade Engine):
 *    - Market data updates (ADD, CANCEL, TRADE)
 *    - SPSC: Single producer (market data), single consumer (trade engine)
 * 
 * THREADING:
 * - 3 dedicated threads (one per component)
 * - Non-blocking: Busy-wait loops (low latency)
 * - Lock-free queues: No synchronization overhead
 * 
 * COMMAND-LINE ARGUMENTS:
 * ```
 * ./trading_main CLIENT_ID ALGO_TYPE [CLIP THRESH MAX_ORDER MAX_POS MAX_LOSS] ...
 * ```
 * 
 * Arguments:
 * - CLIENT_ID: Unique client identifier (integer)
 * - ALGO_TYPE: Trading algorithm (MAKER, TAKER, RANDOM)
 * - Per-ticker config (repeating 5-tuple):
 *   • CLIP: Order size (qty per order)
 *   • THRESH: Trading threshold (signal strength)
 *   • MAX_ORDER: Max order size (risk limit)
 *   • MAX_POS: Max position (risk limit)
 *   • MAX_LOSS: Max loss (stop-out limit)
 * 
 * EXAMPLE:
 * ```
 * ./trading_main 1 MAKER 10 0.25 100 500 -5000.0 20 0.30 200 1000 -10000.0
 * ```
 * - Client ID: 1
 * - Algorithm: Market maker
 * - Ticker 0: clip=10, thresh=0.25, max_order=100, max_pos=500, max_loss=-5000
 * - Ticker 1: clip=20, thresh=0.30, max_order=200, max_pos=1000, max_loss=-10000
 * 
 * INITIALIZATION SEQUENCE:
 * 1. Parse command-line arguments
 * 2. Create lock-free queues (communication channels)
 * 3. Start trade engine (strategy thread)
 * 4. Start order gateway (TCP connection thread)
 * 5. Start market data consumer (UDP multicast thread)
 * 6. Wait for market data (10 seconds initialization)
 * 7. Begin trading
 * 
 * SHUTDOWN SEQUENCE:
 * 1. Detect inactivity (60 seconds silence)
 * 2. Stop all components (signal threads to exit)
 * 3. Wait for threads to finish (drain queues)
 * 4. Delete components (cleanup resources)
 * 5. Exit cleanly
 * 
 * RANDOM TRADING ALGORITHM:
 * - Testing mode: Generates random orders
 * - Purpose: Stress testing, system validation
 * - Orders: Random ticker, price, qty, side
 * - Cancels: Randomly cancel previous orders
 * - Not production: Demo/testing only
 */

#include <csignal>

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"

#include "common/logging.h"

/*
 * GLOBAL COMPONENT POINTERS
 * ==========================
 * 
 * Main components of the trading system.
 * Global for signal handler access (graceful shutdown on Ctrl+C).
 * 
 * Initialized in main(), deleted at shutdown.
 */

// Async logger (per-client log file)
Common::Logger *logger = nullptr;

// Core trading engine (strategies, order management)
Trading::TradeEngine *trade_engine = nullptr;

// Market data consumer (UDP multicast subscriber)
Trading::MarketDataConsumer *market_data_consumer = nullptr;

// Order gateway (TCP connection to exchange)
Trading::OrderGateway *order_gateway = nullptr;

/*
 * MAIN - TRADING SYSTEM ENTRY POINT
 * ==================================
 * 
 * Initializes and runs the trading system.
 * Parses config, creates components, manages lifecycle.
 * 
 * COMMAND-LINE USAGE:
 * ```
 * ./trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 ...] ...
 * ```
 * 
 * Parameters:
 * - argc: Argument count (must be >= 3)
 * - argv: Argument vector
 *   [0]: Program name
 *   [1]: CLIENT_ID (integer, unique per client)
 *   [2]: ALGO_TYPE (MAKER, TAKER, RANDOM)
 *   [3+]: Per-ticker configuration (groups of 5)
 * 
 * Returns:
 * - EXIT_SUCCESS (0): Normal termination
 * - Does not return on error (FATAL aborts)
 */
int main(int argc, char **argv) {
  /*
   * STEP 1: VALIDATE COMMAND-LINE ARGUMENTS
   * ========================================
   * 
   * Minimum arguments: program name, client ID, algo type
   * Total: 3+ arguments required
   * 
   * Example:
   * ./trading_main 1 MAKER 10 0.25 100 500 -5000
   * argv[0] = "./trading_main"
   * argv[1] = "1"           (CLIENT_ID)
   * argv[2] = "MAKER"       (ALGO_TYPE)
   * argv[3-7] = ticker config (5 values)
   */
  if(argc < 3) {
    FATAL("USAGE trading_main CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...");
  }

  /*
   * STEP 2: PARSE CLIENT ID
   * ========================
   * 
   * Client ID: Unique identifier for this trading client
   * - Range: Typically 0-255 (uint8_t)
   * - Used for: Order IDs, logging, client identification
   * - Example: Client 1, 2, 3 (different traders/strategies)
   */
  const Common::ClientId client_id = atoi(argv[1]);
  
  // Seed random number generator (for RANDOM algo)
  // Different seed per client (deterministic but different)
  srand(client_id);

  /*
   * STEP 3: PARSE ALGORITHM TYPE
   * =============================
   * 
   * Algorithm type: Trading strategy to run
   * - MAKER: Market maker (passive quotes)
   * - TAKER: Liquidity taker (aggressive orders)
   * - RANDOM: Random trading (testing only)
   * 
   * Determines: Which strategy logic to use
   */
  const auto algo_type = stringToAlgoType(argv[2]);

  /*
   * STEP 4: INITIALIZE LOGGER
   * ==========================
   * 
   * Create async logger for this client.
   * 
   * Log file: "trading_main_<CLIENT_ID>.log"
   * - Example: "trading_main_1.log" for client 1
   * - Async: Non-blocking writes (off critical path)
   * - Per-client: Separate logs for each client
   */
  logger = new Common::Logger("trading_main_" + std::to_string(client_id) + ".log");

  /*
   * CONFIGURATION CONSTANTS
   * =======================
   */
  
  // Sleep time between random orders (microseconds)
  // 20 * 1000 = 20,000 μs = 20 ms
  // Used for: RANDOM algo pacing
  const int sleep_time = 20 * 1000;

  /*
   * STEP 5: CREATE LOCK-FREE QUEUES
   * ================================
   * 
   * Inter-thread communication channels (SPSC queues).
   * 
   * QUEUE 1: client_requests (Trade Engine -> Order Gateway)
   * - Type: ClientRequestLFQueue
   * - Capacity: ME_MAX_CLIENT_UPDATES (e.g., 256K)
   * - Contents: Orders to send (NEW, CANCEL)
   * - Flow: Strategy generates -> Order gateway sends to exchange
   * 
   * QUEUE 2: client_responses (Order Gateway -> Trade Engine)
   * - Type: ClientResponseLFQueue
   * - Capacity: ME_MAX_CLIENT_UPDATES
   * - Contents: Execution reports (ACCEPTED, FILLED, CANCELED)
   * - Flow: Exchange sends -> Order gateway receives -> Strategy processes
   * 
   * QUEUE 3: market_updates (Market Data Consumer -> Trade Engine)
   * - Type: MEMarketUpdateLFQueue
   * - Capacity: ME_MAX_MARKET_UPDATES (e.g., 256K)
   * - Contents: Market data updates (ADD, CANCEL, TRADE)
   * - Flow: Exchange publishes -> Market data receives -> Strategy processes
   * 
   * WHY LOCK-FREE:
   * - No mutex overhead: 10-100x faster than locking
   * - Predictable latency: No lock contention
   * - Single-threaded access: SPSC (safe without locks)
   */
  Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
  Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);
  Exchange::MEMarketUpdateLFQueue market_updates(ME_MAX_MARKET_UPDATES);

  // Temporary string for logging (reused)
  std::string time_str;

  /*
   * STEP 6: PARSE PER-TICKER CONFIGURATION
   * =======================================
   * 
   * Configuration map: Ticker ID -> Trading config
   * 
   * CONFIG STRUCTURE (per ticker):
   * - clip: Order size (qty per order)
   * - threshold: Trading signal threshold
   * - risk_cfg:
   *   • max_order_size: Max qty per order (risk)
   *   • max_position: Max net position (risk)
   *   • max_loss: Max loss before stop-out (risk)
   * 
   * COMMAND-LINE FORMAT:
   * [CLIP THRESH MAX_ORDER MAX_POS MAX_LOSS] repeated per ticker
   * 
   * Example:
   * argv[3] = "10"      (CLIP_1)
   * argv[4] = "0.25"    (THRESH_1)
   * argv[5] = "100"     (MAX_ORDER_SIZE_1)
   * argv[6] = "500"     (MAX_POS_1)
   * argv[7] = "-5000.0" (MAX_LOSS_1)
   * ... repeat for ticker 2, 3, etc.
   * 
   * PARSING LOOP:
   * - Start at argv[3] (after CLIENT_ID and ALGO_TYPE)
   * - Step by 5 (one config group = 5 values)
   * - Increment ticker ID for each group
   * - Store in ticker_cfg array
   */
  TradeEngineCfgHashMap ticker_cfg;

  size_t next_ticker_id = 0;  // Start with ticker 0
  
  // Parse config groups (i = start of each 5-value group)
  for (int i = 3; i < argc; i += 5, ++next_ticker_id) {
    // Parse 5 values for this ticker
    ticker_cfg.at(next_ticker_id) = {
      static_cast<Qty>(std::atoi(argv[i])),      // clip (order size)
      std::atof(argv[i + 1]),                     // threshold (signal strength)
      {
        static_cast<Qty>(std::atoi(argv[i + 2])), // max_order_size (risk)
        static_cast<Qty>(std::atoi(argv[i + 3])), // max_position (risk)
        std::atof(argv[i + 4])                     // max_loss (risk)
      }
    };
  }

  /*
   * STEP 7: START TRADE ENGINE
   * ===========================
   * 
   * Create and start core trading engine.
   * 
   * Constructor parameters:
   * - client_id: Client identifier
   * - algo_type: Strategy (MAKER, TAKER, RANDOM)
   * - ticker_cfg: Per-ticker configuration
   * - client_requests: Queue to order gateway (output)
   * - client_responses: Queue from order gateway (input)
   * - market_updates: Queue from market data (input)
   * 
   * Initialization:
   * - Creates strategy instance (market maker or liquidity taker)
   * - Initializes order manager, position keeper, risk manager
   * - Creates order books for all tickers
   * - Ready to process market data and generate orders
   * 
   * start():
   * - Creates dedicated thread for trade engine
   * - Begins consuming market data and execution reports
   * - Generates orders based on strategy logic
   */
  logger->log("%:% %() % Starting Trade Engine...\n", __FILE__, __LINE__, __FUNCTION__, 
              Common::getCurrentTimeStr(&time_str));
  
  trade_engine = new Trading::TradeEngine(client_id, algo_type,
                                          ticker_cfg,
                                          &client_requests,
                                          &client_responses,
                                          &market_updates);
  trade_engine->start();  // Start trade engine thread

  /*
   * STEP 8: START ORDER GATEWAY
   * ============================
   * 
   * Create and start order gateway (TCP connection to exchange).
   * 
   * CONNECTION PARAMETERS:
   * - IP: "127.0.0.1" (localhost, same machine)
   * - Interface: "lo" (loopback interface)
   * - Port: 12345 (exchange order server port)
   * 
   * Production:
   * - Different IP: Actual exchange server (co-located)
   * - Different interface: "eth0", "eth1" (dedicated network)
   * - Different port: Exchange-specific (often 12345, 8080, etc.)
   * 
   * Constructor parameters:
   * - client_id: Client identifier
   * - client_requests: Queue from trade engine (input, orders to send)
   * - client_responses: Queue to trade engine (output, exec reports)
   * - order_gw_ip: Exchange server IP
   * - order_gw_iface: Network interface
   * - order_gw_port: Exchange server port
   * 
   * start():
   * - Connects TCP socket to exchange order server
   * - Creates dedicated thread for order gateway
   * - Begins sending orders and receiving execution reports
   */
  const std::string order_gw_ip = "127.0.0.1";      // Localhost (testing)
  const std::string order_gw_iface = "lo";          // Loopback interface
  const int order_gw_port = 12345;                  // Exchange order server port

  logger->log("%:% %() % Starting Order Gateway...\n", __FILE__, __LINE__, __FUNCTION__, 
              Common::getCurrentTimeStr(&time_str));
  
  order_gateway = new Trading::OrderGateway(client_id, &client_requests, &client_responses, 
                                            order_gw_ip, order_gw_iface, order_gw_port);
  order_gateway->start();  // Start order gateway thread

  /*
   * STEP 9: START MARKET DATA CONSUMER
   * ===================================
   * 
   * Create and start market data consumer (UDP multicast subscriber).
   * 
   * CONNECTION PARAMETERS:
   * - Interface: "lo" (loopback, for local testing)
   * - Snapshot IP: "233.252.14.1" (multicast group for snapshots)
   * - Snapshot port: 20000
   * - Incremental IP: "233.252.14.3" (multicast group for incremental updates)
   * - Incremental port: 20001
   * 
   * Production:
   * - Different interface: "eth0" (dedicated network)
   * - Exchange-specific IPs: Provided by exchange
   * - Different ports: Exchange-specific
   * 
   * Constructor parameters:
   * - client_id: Client identifier (for logging)
   * - market_updates: Queue to trade engine (output, market data)
   * - mkt_data_iface: Network interface
   * - snapshot_ip: Snapshot multicast group
   * - snapshot_port: Snapshot port
   * - incremental_ip: Incremental multicast group
   * - incremental_port: Incremental port
   * 
   * start():
   * - Subscribes to incremental multicast stream (always)
   * - Creates dedicated thread for market data consumer
   * - Begins receiving market updates (order book, trades)
   * - Handles gap recovery (subscribes to snapshot on packet loss)
   */
  const std::string mkt_data_iface = "lo";          // Loopback interface (testing)
  const std::string snapshot_ip = "233.252.14.1";   // Snapshot multicast group
  const int snapshot_port = 20000;                  // Snapshot port
  const std::string incremental_ip = "233.252.14.3"; // Incremental multicast group
  const int incremental_port = 20001;               // Incremental port

  logger->log("%:% %() % Starting Market Data Consumer...\n", __FILE__, __LINE__, __FUNCTION__, 
              Common::getCurrentTimeStr(&time_str));
  
  market_data_consumer = new Trading::MarketDataConsumer(client_id, &market_updates, mkt_data_iface, 
                                                          snapshot_ip, snapshot_port, 
                                                          incremental_ip, incremental_port);
  market_data_consumer->start();  // Start market data consumer thread

  /*
   * STEP 10: INITIALIZATION WAIT
   * =============================
   * 
   * Wait for system to initialize and receive market data.
   * 
   * Sleep: 10 seconds
   * - Purpose: Allow time for:
   *   • TCP connection to establish
   *   • Multicast subscriptions to activate
   *   • Initial market data to arrive
   *   • Order books to populate
   * 
   * Production:
   * - Could wait for specific condition (order book ready)
   * - Could subscribe to "ready" events
   * - Could use condition variables (signal when ready)
   */
  usleep(10 * 1000 * 1000);  // 10 seconds = 10,000,000 microseconds

  /*
   * STEP 11: INITIALIZE ACTIVITY TIMER
   * ===================================
   * 
   * Record current time as last activity time.
   * Used for: Detecting when trading has gone silent (no market data/orders).
   * 
   * Purpose:
   * - Auto-shutdown: Exit if no activity for 60 seconds
   * - Prevents: Hanging indefinitely if exchange disconnects
   */
  trade_engine->initLastEventTime();

  /*
   * STEP 12: RANDOM TRADING ALGORITHM
   * ==================================
   * 
   * Special case: If algo type is RANDOM, generate random orders.
   * 
   * PURPOSE:
   * - Testing: Stress test the system
   * - Validation: Verify order flow end-to-end
   * - Demo: Show system working
   * - NOT PRODUCTION: Random orders are not real trading
   * 
   * ALGORITHM:
   * 1. Initialize order ID (client_id * 1000)
   * 2. Generate random base prices per ticker
   * 3. Loop 10,000 times (or until 60 seconds silence):
   *    a. Pick random ticker
   *    b. Generate random price (base + [1-10])
   *    c. Generate random qty (1-100)
   *    d. Generate random side (BUY or SELL)
   *    e. Send NEW order
   *    f. Sleep 20 ms
   *    g. Pick random previous order
   *    h. Send CANCEL for that order
   *    i. Sleep 20 ms
   * 4. Check for 60-second silence (auto-stop)
   * 
   * RANDOM ORDER GENERATION:
   * - Ticker: 0 to ME_MAX_TICKERS-1
   * - Price: base_price + random offset (e.g., 100-110)
   * - Qty: 1-100 shares
   * - Side: 50% BUY, 50% SELL
   * 
   * ORDER TRACKING:
   * - client_requests_vec: Store all sent orders
   * - Used for: Randomly canceling previous orders
   * 
   * PACING:
   * - 20 ms between orders (50 orders/second)
   * - Prevents: Overwhelming system
   * - Realistic: Human trading pace (not HFT)
   * 
   * SILENCE DETECTION:
   * - Check every iteration: silentSeconds() >= 60
   * - Stop early: If no market data/responses for 60 seconds
   * - Reason: Exchange may have stopped or disconnected
   */
  if (algo_type == AlgoType::RANDOM) {
    // Initialize order ID (unique per client)
    // Example: Client 1 -> order IDs 1000, 1001, 1002, ...
    Common::OrderId order_id = client_id * 1000;
    
    // Storage for sent orders (for random cancels)
    std::vector<Exchange::MEClientRequest> client_requests_vec;
    
    // Random base price per ticker (100-200)
    std::array<Price, ME_MAX_TICKERS> ticker_base_price;
    for (size_t i = 0; i < ME_MAX_TICKERS; ++i)
      ticker_base_price[i] = (rand() % 100) + 100;  // 100 to 199
    
    // Generate 10,000 random orders (or stop early if silent)
    for (size_t i = 0; i < 10000; ++i) {
      // Generate random order parameters
      const Common::TickerId ticker_id = rand() % Common::ME_MAX_TICKERS;  // Random ticker
      const Price price = ticker_base_price[ticker_id] + (rand() % 10) + 1;  // base + [1-10]
      const Qty qty = 1 + (rand() % 100) + 1;  // 1 to 100 shares
      const Side side = (rand() % 2 ? Common::Side::BUY : Common::Side::SELL);  // 50/50

      // Create NEW order request
      Exchange::MEClientRequest new_request{Exchange::ClientRequestType::NEW, client_id, ticker_id, order_id++, side,
                                            price, qty};
      
      // Send order to trade engine (enqueues to order gateway)
      trade_engine->sendClientRequest(&new_request);
      
      // Sleep 20 ms (pace orders)
      usleep(sleep_time);

      // Store order (for random cancels)
      client_requests_vec.push_back(new_request);
      
      // Randomly cancel a previous order
      const auto cxl_index = rand() % client_requests_vec.size();  // Pick random previous order
      auto cxl_request = client_requests_vec[cxl_index];           // Copy order
      cxl_request.type_ = Exchange::ClientRequestType::CANCEL;     // Change to CANCEL
      
      // Send cancel to trade engine
      trade_engine->sendClientRequest(&cxl_request);
      
      // Sleep 20 ms (pace cancels)
      usleep(sleep_time);

      // Check for 60-second silence (auto-stop)
      if (trade_engine->silentSeconds() >= 60) {
        logger->log("%:% %() % Stopping early because been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());

        break;  // Exit random trading loop
      }
    }
  }

  /*
   * STEP 13: WAIT FOR TRADING TO COMPLETE
   * ======================================
   * 
   * Wait for trading to go silent (no market data or orders for 60 seconds).
   * 
   * SILENCE DETECTION:
   * - silentSeconds(): Time since last market data or execution report
   * - Threshold: 60 seconds
   * - Reason: Exchange closed, market quiet, or disconnected
   * 
   * POLLING LOOP:
   * - Check every 30 seconds
   * - Log: How long been silent
   * - Continue: Until 60-second threshold
   * 
   * PURPOSE:
   * - Automatic shutdown: Don't run forever
   * - Graceful exit: Wait for pending orders to complete
   * - Production: Could have manual stop (signal handler)
   */
  while (trade_engine->silentSeconds() < 60) {
    logger->log("%:% %() % Waiting till no activity, been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());

    // Sleep 30 seconds between checks
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(30s);
  }

  /*
   * STEP 14: GRACEFUL SHUTDOWN - STOP COMPONENTS
   * =============================================
   * 
   * Signal all components to stop (set run_ = false).
   * 
   * STOP ORDER:
   * 1. Trade engine: Stop generating orders
   * 2. Market data consumer: Stop processing market data
   * 3. Order gateway: Stop sending orders and receiving responses
   * 
   * STOP BEHAVIOR:
   * - stop(): Sets run_ = false
   * - Threads: Check run_ flag and exit main loop
   * - Graceful: Allow current processing to complete
   * - No force: Threads exit naturally (not killed)
   * 
   * Trade Engine stop():
   * - Wait for queues to drain (incoming_ogw_responses_, incoming_md_updates_)
   * - Log final positions
   * - Then set run_ = false
   * 
   * Market Data Consumer stop():
   * - Simply set run_ = false
   * - Thread exits main loop
   * 
   * Order Gateway stop():
   * - Simply set run_ = false
   * - Thread exits main loop
   */
  trade_engine->stop();           // Stop trade engine thread
  market_data_consumer->stop();   // Stop market data consumer thread
  order_gateway->stop();          // Stop order gateway thread

  /*
   * STEP 15: WAIT FOR THREADS TO FINISH
   * ====================================
   * 
   * Wait for all threads to complete and exit.
   * 
   * Sleep: 10 seconds
   * - Purpose: Allow threads to:
   *   • Exit main loops
   *   • Drain queues
   *   • Close connections
   *   • Log final state
   * 
   * Thread lifecycle:
   * 1. stop() called -> run_ = false
   * 2. Thread checks run_ flag
   * 3. Thread exits main loop
   * 4. Thread returns from run() function
   * 5. Thread terminates
   * 
   * Production:
   * - Could use pthread_join() (wait for specific thread)
   * - Could use condition variables (signal when done)
   * - Could check thread status (verify all exited)
   */
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(10s);  // Wait for threads to exit

  /*
   * STEP 16: CLEANUP - DELETE COMPONENTS
   * =====================================
   * 
   * Delete all components and free resources.
   * 
   * DELETION ORDER:
   * 1. Logger: Stop logging
   * 2. Trade engine: Free strategy, order books, etc.
   * 3. Market data consumer: Free multicast sockets
   * 4. Order gateway: Free TCP sockets
   * 
   * DESTRUCTOR BEHAVIOR:
   * - Trade engine: Deletes strategy, order books, memory pools
   * - Market data consumer: Leaves multicast groups, closes sockets
   * - Order gateway: Closes TCP connection
   * - Logger: Flushes and closes log file
   * 
   * nullptr ASSIGNMENT:
   * - Good practice: Prevent use-after-free
   * - Safety: If accidentally referenced, will crash (not corrupt)
   */
  delete logger;
  logger = nullptr;
  
  delete trade_engine;
  trade_engine = nullptr;
  
  delete market_data_consumer;
  market_data_consumer = nullptr;
  
  delete order_gateway;
  order_gateway = nullptr;

  /*
   * STEP 17: FINAL WAIT
   * ===================
   * 
   * Final wait before exit (allow async operations to complete).
   * 
   * Sleep: 10 seconds
   * - Purpose: Allow:
   *   • Async logging to flush
   *   • Network connections to close cleanly
   *   • Operating system to clean up resources
   * 
   * May be excessive: Could reduce to 1-2 seconds
   */
  std::this_thread::sleep_for(10s);

  /*
   * STEP 18: EXIT
   * =============
   * 
   * Exit the program successfully.
   * 
   * EXIT_SUCCESS (0): Normal termination
   * - All components stopped cleanly
   * - Resources freed
   * - Logs flushed
   */
  exit(EXIT_SUCCESS);
}

/*
 * TRADING MAIN DESIGN CONSIDERATIONS
 * ===================================
 * 
 * 1. INITIALIZATION ORDER:
 *    - Queues first: Communication channels must exist
 *    - Trade engine: Core logic, consumes from queues
 *    - Order gateway: Sends orders, produces to queues
 *    - Market data: Produces market updates
 *    - Important: Dependencies (trade engine needs queues)
 * 
 * 2. THREADING MODEL:
 *    - 3 dedicated threads: One per major component
 *    - Advantage: Parallelism, isolation
 *    - Lock-free queues: Inter-thread communication
 *    - Busy-wait: Low latency (high CPU usage)
 * 
 * 3. CONFIGURATION:
 *    - Command-line: Simple, flexible
 *    - Per-ticker: Different configs for different instruments
 *    - Scalable: Add more tickers by adding config groups
 *    - Alternative: Config file (JSON, YAML)
 * 
 * 4. GRACEFUL SHUTDOWN:
 *    - Detect silence: Auto-stop after 60 seconds
 *    - Stop components: Signal threads to exit
 *    - Wait: Allow threads to finish
 *    - Cleanup: Delete components, free resources
 * 
 * 5. RANDOM TRADING:
 *    - Testing: Validate system end-to-end
 *    - Stress test: Generate high order volume
 *    - Not production: Real strategies are sophisticated
 * 
 * PRODUCTION ENHANCEMENTS:
 * 
 * A) Signal Handling:
 *    - SIGINT (Ctrl+C): Graceful shutdown
 *    - SIGTERM: Graceful shutdown
 *    - SIGUSR1: Dump statistics
 *    - Implementation:
 *    ```cpp
 *    void signal_handler(int signal) {
 *      if (signal == SIGINT || signal == SIGTERM) {
 *        // Graceful shutdown
 *        if (trade_engine) trade_engine->stop();
 *        if (market_data_consumer) market_data_consumer->stop();
 *        if (order_gateway) order_gateway->stop();
 *      }
 *    }
 *    signal(SIGINT, signal_handler);
 *    signal(SIGTERM, signal_handler);
 *    ```
 * 
 * B) Configuration File:
 *    - JSON, YAML, TOML: Structured configuration
 *    - Advantage: More readable, validatable
 *    - Example (JSON):
 *    ```json
 *    {
 *      "client_id": 1,
 *      "algo_type": "MAKER",
 *      "tickers": [
 *        {
 *          "clip": 10,
 *          "threshold": 0.25,
 *          "max_order_size": 100,
 *          "max_position": 500,
 *          "max_loss": -5000.0
 *        }
 *      ]
 *    }
 *    ```
 * 
 * C) Component Health Checks:
 *    - Heartbeats: Each component sends periodic "alive" signal
 *    - Watchdog: Monitor component health
 *    - Auto-restart: If component crashes
 *    - Alerts: Notify if component unhealthy
 * 
 * D) Metrics and Monitoring:
 *    - Prometheus: Export metrics
 *    - Grafana: Visualize metrics
 *    - Metrics:
 *      • Orders sent/second
 *      • Market data updates/second
 *      • Latency (p50, p99, p999)
 *      • Position, PnL
 *      • Queue depths
 * 
 * E) Multiple Strategies:
 *    - Run multiple strategies simultaneously
 *    - Different algorithms per ticker
 *    - Portfolio-level risk management
 *    - Aggregation: Combine orders from multiple strategies
 * 
 * F) Backtesting Mode:
 *    - Replay historical market data
 *    - Simulate order execution
 *    - Validate strategy performance
 *    - No live connections (file-based)
 * 
 * G) Paper Trading Mode:
 *    - Connect to live market data
 *    - Don't send real orders
 *    - Simulate execution (against market)
 *    - Validate strategy in live market
 * 
 * H) Disaster Recovery:
 *    - Checkpointing: Save state periodically
 *    - Crash recovery: Restore from checkpoint
 *    - Reconciliation: Compare with exchange
 *    - Failover: Switch to backup system
 * 
 * DEBUGGING:
 * - Log everything: Initialization, shutdown, errors
 * - Separate logs: Per component (trade_engine, order_gw, market_data)
 * - Timestamps: Microsecond precision
 * - Thread IDs: Track which thread logged
 * - Core dumps: Enable for crash analysis
 * 
 * PERFORMANCE OPTIMIZATION:
 * - CPU affinity: Pin threads to dedicated cores
 * - NUMA: Place memory close to CPU
 * - Huge pages: Reduce TLB misses
 * - Kernel bypass: DPDK, Solarflare (advanced)
 * - Profiling: perf, VTune (find bottlenecks)
 * 
 * COMMON ISSUES:
 * - Port conflicts: Multiple instances, port already in use
 * - Network issues: Interface down, firewall blocking
 * - Resource exhaustion: Too many orders, queue overflow
 * - Race conditions: Improper shutdown, dangling pointers
 * - Memory leaks: Components not deleted
 * - Zombie threads: Threads not joining
 * 
 * TESTING:
 * - Unit tests: Each component independently
 * - Integration tests: Full system end-to-end
 * - Stress tests: High order volume, extreme scenarios
 * - Network tests: Disconnects, reconnects, packet loss
 * - Chaos testing: Random failures, kill components
 */
