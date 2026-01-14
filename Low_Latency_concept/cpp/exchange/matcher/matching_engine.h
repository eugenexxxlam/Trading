#pragma once

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"

#include "order_server/client_request.h"
#include "order_server/client_response.h"
#include "market_data/market_update.h"

#include "me_order_book.h"

/*
 * MATCHING ENGINE - CORE ORDER MATCHING COMPONENT
 * ================================================
 * 
 * PURPOSE:
 * Central component of the trading system that processes orders and executes trades.
 * Maintains order books for all instruments and matches buy/sell orders.
 * 
 * RESPONSIBILITIES:
 * 1. Consume client requests (NEW, CANCEL) from order server
 * 2. Route requests to appropriate order book (per instrument)
 * 3. Execute matching algorithm (time-price priority)
 * 4. Generate execution reports (ACCEPTED, FILLED, CANCELED)
 * 5. Publish market data updates (ADD, TRADE, CANCEL, MODIFY)
 * 
 * ARCHITECTURE:
 * ```
 * Order Server --> [LF Queue] --> Matching Engine
 *                                      |
 *         +----------------------------+----------------------------+
 *         |                                                         |
 *         v                                                         v
 *   [LF Queue] --> Order Server --> TCP --> Clients    [LF Queue] --> Market Data Publisher
 *   (Responses)                                        (Market Updates)
 * ```
 * 
 * THREADING:
 * - Single thread (no locking needed)
 * - Dedicated CPU core (via CPU affinity)
 * - Lock-free queues for communication
 * - Event-driven loop (polling queues)
 * 
 * PERFORMANCE:
 * - Process order: 200-500 ns (add/cancel)
 * - Matching: 50-500 ns per matched order
 * - Throughput: 1-2M orders/second
 * - Latency: 1-5 μs (order receive to response send)
 * 
 * WHY SINGLE THREAD?
 * - Avoids locking (major latency source)
 * - Predictable performance (no contention)
 * - Simpler code (no race conditions)
 * - Sufficient throughput (1M+ orders/second)
 * - Industry standard (most HFT engines single-threaded per instrument)
 * 
 * SCALING:
 * - Per-instrument: One engine per instrument (parallel)
 * - Partitioning: Group instruments by matching engine
 * - This implementation: All instruments, one thread (educational)
 */

namespace Exchange {
  class MatchingEngine final {  // final = no inheritance (optimization)
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - client_requests: Incoming orders from order server
     * - client_responses: Outgoing execution reports to order server
     * - market_updates: Outgoing market data to publisher
     * 
     * Initializes:
     * - Order books for all instruments (one per ticker)
     * - Logger for debugging
     * - Queue pointers (communication channels)
     */
    MatchingEngine(ClientRequestLFQueue *client_requests,
                   ClientResponseLFQueue *client_responses,
                   MEMarketUpdateLFQueue *market_updates);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleans up order books and resources.
     * Called when exchange shuts down.
     */
    ~MatchingEngine();

    /*
     * START - BEGIN MATCHING ENGINE THREAD
     * =====================================
     * 
     * Creates and starts matching engine thread.
     * 
     * THREADING:
     * - Creates dedicated thread (std::thread)
     * - Sets CPU affinity (pins to specific core)
     * - Calls run() method in loop
     * 
     * CPU AFFINITY:
     * - Pins thread to dedicated core (no other threads)
     * - Reduces context switches
     * - Improves cache locality
     * - Lower latency (consistent performance)
     */
    auto start() -> void;

    /*
     * STOP - SHUTDOWN MATCHING ENGINE
     * ================================
     * 
     * Stops matching engine thread gracefully.
     * 
     * ALGORITHM:
     * 1. Set run_ = false (stop loop)
     * 2. Wait for thread to finish (join)
     * 3. Process remaining messages (drain queues)
     */
    auto stop() -> void;

    /*
     * PROCESS CLIENT REQUEST - ORDER ROUTING
     * =======================================
     * 
     * Routes client request to appropriate order book.
     * 
     * ALGORITHM:
     * 1. Lookup order book by ticker_id (hash map)
     * 2. Switch on request type:
     *    - NEW: Add order to book (may match)
     *    - CANCEL: Remove order from book
     * 3. Order book handles matching, responses, updates
     * 
     * PERFORMANCE MEASUREMENT:
     * - START_MEASURE / END_MEASURE: Records latency
     * - Separate measurements for add vs cancel
     * - Logged for analysis (microsecond precision)
     * 
     * ERROR HANDLING:
     * - INVALID request type: FATAL (abort)
     * - Invalid ticker: Order book will be nullptr (crash)
     * - Production: Validate and reject invalid requests
     * 
     * NOEXCEPT:
     * - No exception handling overhead
     * - Critical path optimization
     * - Errors logged and system continues
     * 
     * Parameters:
     * - client_request: NEW or CANCEL order
     */
    auto processClientRequest(const MEClientRequest *client_request) noexcept {
      // Lookup order book for this instrument (O(1) array access)
      auto order_book = ticker_order_book_[client_request->ticker_id_];
      
      // Route to appropriate handler
      switch (client_request->type_) {
        case ClientRequestType::NEW: {
          // Add new order (may match existing orders)
          START_MEASURE(Exchange_MEOrderBook_add);  // Begin latency measurement
          order_book->add(client_request->client_id_, client_request->order_id_, client_request->ticker_id_,
                           client_request->side_, client_request->price_, client_request->qty_);
          END_MEASURE(Exchange_MEOrderBook_add, logger_);  // End latency measurement
        }
          break;

        case ClientRequestType::CANCEL: {
          // Cancel existing order
          START_MEASURE(Exchange_MEOrderBook_cancel);
          order_book->cancel(client_request->client_id_, client_request->order_id_, client_request->ticker_id_);
          END_MEASURE(Exchange_MEOrderBook_cancel, logger_);
        }
          break;

        default: {
          // Invalid request type (should never happen)
          FATAL("Received invalid client-request-type:" + clientRequestTypeToString(client_request->type_));
        }
          break;
      }
    }

    /*
     * SEND CLIENT RESPONSE - PUBLISH EXECUTION REPORT
     * ================================================
     * 
     * Publishes execution report to order server via lock-free queue.
     * 
     * ALGORITHM:
     * 1. Log response (debugging)
     * 2. Get next write slot in queue (two-step write)
     * 3. Copy response to slot
     * 4. Update write index (commit, publish to consumer)
     * 5. Record timestamp (T4t measurement point)
     * 
     * LOCK-FREE QUEUE:
     * - Two-step write (reserve slot, commit)
     * - Single producer (this thread) / single consumer (order server)
     * - No locking, no atomic operations (just indices)
     * - 10-20 ns latency
     * 
     * PERFORMANCE:
     * - std::move: Efficient copy (avoid allocation)
     * - TTT_MEASURE: Timestamp for latency tracking
     * - Log: Async (off critical path)
     * 
     * TTT_MEASURE:
     * - T4t = Matching engine to order server queue write
     * - Timestamp recorded for end-to-end latency analysis
     * 
     * Called by: Order book (after order processing)
     * 
     * Parameters:
     * - client_response: Execution report (ACCEPTED, FILLED, CANCELED)
     */
    auto sendClientResponse(const MEClientResponse *client_response) noexcept {
      // Log response (async, off critical path)
      logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), client_response->toString());
      
      // Write to lock-free queue (two-step: get slot, update index)
      auto next_write = outgoing_ogw_responses_->getNextToWriteTo();  // Reserve slot
      *next_write = std::move(*client_response);                       // Write response
      outgoing_ogw_responses_->updateWriteIndex();                     // Commit (publish)
      
      // Record timestamp (T4t = matching engine output to order server)
      TTT_MEASURE(T4t_MatchingEngine_LFQueue_write, logger_);
    }

    /*
     * SEND MARKET UPDATE - PUBLISH ORDER BOOK CHANGE
     * ===============================================
     * 
     * Publishes market data update to market data publisher via lock-free queue.
     * 
     * ALGORITHM:
     * 1. Log update (debugging)
     * 2. Get next write slot in queue
     * 3. Copy update to slot
     * 4. Update write index (commit)
     * 5. Record timestamp (T4 measurement point)
     * 
     * MARKET UPDATES:
     * - ADD: New order added to book
     * - CANCEL: Order removed
     * - MODIFY: Order quantity changed (partial fill)
     * - TRADE: Order matched (execution)
     * - CLEAR: Order book cleared
     * - SNAPSHOT_START/END: Snapshot markers
     * 
     * MULTICAST PUBLISHING:
     * - Market data publisher consumes from queue
     * - Adds sequence number
     * - Broadcasts via UDP multicast
     * - Many subscribers receive simultaneously
     * 
     * TTT_MEASURE:
     * - T4 = Matching engine to market data publisher queue write
     * - Timestamp for latency analysis
     * 
     * Called by: Order book (after order book change)
     * 
     * Parameters:
     * - market_update: Order book change notification
     */
    auto sendMarketUpdate(const MEMarketUpdate *market_update) noexcept {
      // Log update (async logging)
      logger_.log("%:% %() % Sending %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), market_update->toString());
      
      // Write to lock-free queue
      auto next_write = outgoing_md_updates_->getNextToWriteTo();  // Reserve slot
      *next_write = *market_update;                                 // Write update
      outgoing_md_updates_->updateWriteIndex();                     // Commit
      
      // Record timestamp (T4 = matching engine output to market data)
      TTT_MEASURE(T4_MatchingEngine_LFQueue_write, logger_);
    }

    /*
     * RUN - MAIN EVENT LOOP
     * =====================
     * 
     * Main loop for matching engine thread.
     * Continuously polls incoming queue for client requests.
     * 
     * ALGORITHM:
     * 1. Check for incoming client request (non-blocking poll)
     * 2. If request available:
     *    a. Record timestamp (T3)
     *    b. Log request
     *    c. Process request (route to order book)
     *    d. Update read index (consume from queue)
     * 3. Repeat until stopped (run_ = false)
     * 
     * POLLING:
     * - Non-blocking: getNextToRead() returns nullptr if empty
     * - Busy-wait: Continuously checks queue (low latency)
     * - No sleep: Wastes CPU but minimizes latency
     * - 100% CPU utilization (acceptable for HFT)
     * 
     * LIKELY MACRO:
     * - Branch prediction hint: Request usually available
     * - Optimizes hot path (request processing)
     * - Reduces branch mispredictions
     * 
     * PERFORMANCE:
     * - Idle: ~10-20 ns per loop iteration (just queue check)
     * - Active: 200-500 ns per request (matching + publishing)
     * - Throughput: 1-2M requests/second
     * 
     * TTT_MEASURE:
     * - T3 = Matching engine reads from queue
     * - Timestamp for order server -> matching engine latency
     * 
     * GRACEFUL SHUTDOWN:
     * - stop() sets run_ = false
     * - Loop exits naturally
     * - Thread terminates cleanly
     */
    auto run() noexcept {
      // Log thread start
      logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_));
      
      // Main event loop (runs until stopped)
      while (run_) {
        // Poll for incoming client request (non-blocking)
        const auto me_client_request = incoming_requests_->getNextToRead();
        
        // If request available (LIKELY = branch prediction hint)
        if (LIKELY(me_client_request)) {
          // Record timestamp (T3 = matching engine queue read)
          TTT_MEASURE(T3_MatchingEngine_LFQueue_read, logger_);

          // Log incoming request (async)
          logger_.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                      me_client_request->toString());
          
          // Process request (route to order book, execute matching)
          START_MEASURE(Exchange_MatchingEngine_processClientRequest);
          processClientRequest(me_client_request);
          END_MEASURE(Exchange_MatchingEngine_processClientRequest, logger_);
          
          // Consume from queue (advance read index)
          incoming_requests_->updateReadIndex();
        }
      }
    }

    // Deleted constructors (prevent accidental copies)
    MatchingEngine() = delete;
    MatchingEngine(const MatchingEngine &) = delete;
    MatchingEngine(const MatchingEngine &&) = delete;
    MatchingEngine &operator=(const MatchingEngine &) = delete;
    MatchingEngine &operator=(const MatchingEngine &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Hash map: TickerId -> MEOrderBook*
    // One order book per instrument (0=AAPL, 1=MSFT, etc.)
    // O(1) lookup by ticker ID (array indexing)
    OrderBookHashMap ticker_order_book_;

    // Lock-free queues for inter-component communication
    // SPSC (single producer, single consumer) - no locking needed
    
    // Input: Orders from order server
    ClientRequestLFQueue *incoming_requests_ = nullptr;
    
    // Output: Execution reports to order server
    ClientResponseLFQueue *outgoing_ogw_responses_ = nullptr;
    
    // Output: Market data to publisher
    MEMarketUpdateLFQueue *outgoing_md_updates_ = nullptr;

    // Thread control flag (volatile for visibility across threads)
    volatile bool run_ = false;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (lock-free queue internally)
    Logger logger_;
  };
}

/*
 * MATCHING ENGINE DESIGN CONSIDERATIONS
 * ======================================
 * 
 * 1. SINGLE-THREADED DESIGN:
 *    - Advantages:
 *      • No locking (major latency source eliminated)
 *      • Predictable performance (no contention)
 *      • Simpler code (no race conditions)
 *      • Better cache utilization
 *    - Disadvantages:
 *      • Limited by single core performance (~1-2M orders/sec)
 *      • Cannot utilize multiple cores per instrument
 *    - Mitigation:
 *      • Scale by instrument (separate engine per ticker)
 *      • Sufficient for most use cases
 * 
 * 2. LOCK-FREE QUEUES:
 *    - Why: Locking adds 10-100 μs latency
 *    - SPSC: Single producer, single consumer (simplest, fastest)
 *    - Circular buffer: Pre-allocated (no heap)
 *    - Atomic indices: Only synchronization needed
 *    - Latency: 10-20 ns (vs 10-100 μs for mutex)
 * 
 * 3. CPU AFFINITY:
 *    - Pin thread to dedicated core
 *    - Benefits:
 *      • No context switches (200-2000 ns each)
 *      • Better cache locality (L1/L2 stays warm)
 *      • Consistent performance (no CPU migration)
 *      • Lower tail latency (99th percentile)
 *    - Setup: sched_setaffinity (Linux), pthread_setaffinity_np
 * 
 * 4. BUSY-WAIT POLLING:
 *    - Alternative: Event-driven (epoll, select)
 *    - Why busy-wait:
 *      • Lower latency (no syscall overhead)
 *      • Predictable (no kernel scheduling)
 *      • Acceptable: Dedicated core, HFT priority
 *    - Drawback: 100% CPU usage (acceptable trade-off)
 * 
 * 5. PERFORMANCE MEASUREMENT:
 *    - START_MEASURE / END_MEASURE: Inline latency tracking
 *    - TTT_MEASURE: Timestamp tracking points
 *    - rdtsc: CPU cycle counter (nanosecond precision)
 *    - Logged async (off critical path)
 *    - Analysis: Identify bottlenecks, optimize
 * 
 * 6. ERROR HANDLING:
 *    - noexcept: No exception overhead
 *    - FATAL: Critical errors abort (better than undefined behavior)
 *    - Logging: Debug builds
 *    - Production: More graceful error handling
 * 
 * LATENCY BREAKDOWN (ORDER PROCESSING):
 * - Queue read: 10-20 ns (lock-free)
 * - Route to order book: 5-10 ns (array lookup)
 * - Order book add: 20-50 ns (no match)
 * - Order book match: 50-500 ns (1-10 orders)
 * - Generate responses: 20-50 ns per response
 * - Queue write: 10-20 ns per output
 * - TOTAL: 100-600 ns typical (1-5 μs end-to-end)
 * 
 * THROUGHPUT CAPACITY:
 * - Single engine: 1-2M orders/second
 * - Per instrument: 100K-500K orders/second (typical peak)
 * - Scaling: 10+ engines = 10M+ orders/second (system-wide)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Risk checks: Pre-trade validation (position limits, credit)
 * - Order priority: Market orders before limit orders
 * - Advanced order types: Stop, IOC, FOK, MOC, MOO
 * - Circuit breakers: Halt trading on extreme moves
 * - Kill switch: Emergency shutdown
 * - Audit trail: Log every order with μs timestamps
 * - Self-trade prevention: Block client from trading with itself
 * - Graceful degradation: Continue on non-critical errors
 * 
 * ALTERNATIVE ARCHITECTURES:
 * 
 * A) Multi-threaded per Instrument:
 *    - One engine per instrument (parallel)
 *    - Scales linearly with instruments
 *    - Used in production (NASDAQ, CME)
 *    - Complexity: Inter-engine coordination (cross-instrument orders)
 * 
 * B) Lock-based Shared Order Book:
 *    - Multiple threads, shared order book
 *    - Fine-grained locking (per price level)
 *    - Higher throughput potential
 *    - Much higher latency (10-100x slower)
 *    - Not suitable for HFT
 * 
 * C) Deterministic Lock-free:
 *    - Multiple threads, lock-free data structures
 *    - Very complex (CAS loops, ABA problem)
 *    - Marginal benefit over single-threaded
 *    - Academic interest, rarely used in production
 * 
 * INDUSTRY PRACTICES:
 * - Most HFT matching engines: Single-threaded per instrument
 * - Examples: NASDAQ ITCH, CME iLink, LSE UTP
 * - Why: Simplicity, predictability, sufficient throughput
 * - When to scale: Add more instruments per engine, or more engines
 */
