#include "matching_engine.h"

/*
 * MATCHING ENGINE IMPLEMENTATION
 * ===============================
 * 
 * Implementation of MatchingEngine constructor, destructor, start/stop methods.
 * Main run() loop is implemented inline in header (performance optimization).
 * 
 * See matching_engine.h for detailed class documentation and architecture.
 */

namespace Exchange {
  /*
   * CONSTRUCTOR - INITIALIZE MATCHING ENGINE
   * =========================================
   * 
   * Creates matching engine with order books for all instruments.
   * 
   * ALGORITHM:
   * 1. Store queue pointers (communication channels)
   * 2. Initialize logger ("exchange_matching_engine.log")
   * 3. Create order book for each instrument (ticker)
   * 
   * ORDER BOOKS:
   * - One per instrument (ticker_order_book_[ticker_id])
   * - Heap-allocated (new MEOrderBook)
   * - Capacity: ME_MAX_TICKERS (e.g., 256 instruments)
   * - Each order book: Independent state (bids, asks, orders)
   * 
   * LOGGER:
   * - Async logger (lock-free queue internally)
   * - File: "exchange_matching_engine.log"
   * - Off hot path (non-blocking writes)
   * 
   * INITIALIZATION COST:
   * - Create order books: ~1-10 ms (all instruments)
   * - One-time cost at startup (acceptable)
   * - Production: Could lazy-initialize (create on first order)
   * 
   * Parameters:
   * - client_requests: Incoming orders from order server
   * - client_responses: Outgoing execution reports to order server
   * - market_updates: Outgoing market data to publisher
   */
  MatchingEngine::MatchingEngine(ClientRequestLFQueue *client_requests, ClientResponseLFQueue *client_responses,
                                 MEMarketUpdateLFQueue *market_updates)
      : incoming_requests_(client_requests), outgoing_ogw_responses_(client_responses), outgoing_md_updates_(market_updates),
        logger_("exchange_matching_engine.log") {  // Initialize logger with output file
    
    // Create order book for each instrument
    // ticker_order_book_[0] = AAPL, ticker_order_book_[1] = MSFT, etc.
    for(size_t i = 0; i < ticker_order_book_.size(); ++i) {
      // Heap allocate order book (delete in destructor)
      // Parameters: ticker_id, logger, parent matching engine
      ticker_order_book_[i] = new MEOrderBook(i, &logger_, this);
    }
  }

  /*
   * DESTRUCTOR - CLEANUP MATCHING ENGINE
   * =====================================
   * 
   * Cleans up matching engine resources.
   * 
   * ALGORITHM:
   * 1. Stop matching engine thread (set run_ = false)
   * 2. Wait 1 second (allow thread to exit, drain queues)
   * 3. Clear queue pointers (don't delete, owned by caller)
   * 4. Delete all order books (heap cleanup)
   * 
   * SLEEP:
   * - 1 second grace period
   * - Allows: Thread to exit, queues to drain, logging to finish
   * - Production: Could wait for explicit thread join
   * 
   * ORDER BOOK CLEANUP:
   * - Delete each order book (heap allocated in constructor)
   * - Order book destructor cleans up internal resources
   * - Memory pools automatically cleaned up
   */
  ~MatchingEngine() {
    stop();  // Set run_ = false (stop main loop)

    // Wait for thread to exit and queues to drain
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);

    // Clear queue pointers (don't delete, owned by caller)
    incoming_requests_ = nullptr;
    outgoing_ogw_responses_ = nullptr;
    outgoing_md_updates_ = nullptr;

    // Delete all order books (heap cleanup)
    for(auto& order_book : ticker_order_book_) {
      delete order_book;       // Deallocate heap memory
      order_book = nullptr;    // Nullify pointer (good practice)
    }
  }

  /*
   * START - BEGIN MATCHING ENGINE THREAD
   * =====================================
   * 
   * Creates and starts matching engine thread.
   * 
   * ALGORITHM:
   * 1. Set run_ = true (enable main loop)
   * 2. Create thread with run() method
   * 3. Set CPU affinity to core -1 (no specific core, let OS decide)
   *    - Production: Pin to dedicated core (e.g., core 2)
   * 4. Assert thread creation succeeded
   * 
   * THREAD NAME:
   * - "Exchange/MatchingEngine"
   * - Helpful for debugging (ps, top, htop show thread names)
   * - Profiling tools use names (perf, VTune)
   * 
   * CPU AFFINITY:
   * - -1: No specific core (OS decides)
   * - Production: Specific core (e.g., 2) for consistent performance
   * - Benefits: No context switches, better cache locality
   * 
   * LAMBDA:
   * - [this]() { run(); }: Captures this pointer
   * - Calls run() method in new thread
   * - Thread loops until run_ = false
   */
  auto MatchingEngine::start() -> void {
    run_ = true;  // Enable main loop
    
    // Create and start thread
    // Parameters: CPU core (-1 = any), thread name, lambda function
    ASSERT(Common::createAndStartThread(-1, "Exchange/MatchingEngine", [this]() { run(); }) != nullptr, 
           "Failed to start MatchingEngine thread.");
  }

  /*
   * STOP - SHUTDOWN MATCHING ENGINE
   * ================================
   * 
   * Stops matching engine thread gracefully.
   * 
   * ALGORITHM:
   * - Set run_ = false
   * - Main loop (run()) checks run_ flag and exits
   * - Thread terminates naturally
   * 
   * GRACEFUL SHUTDOWN:
   * - No forced termination (no pthread_cancel)
   * - Current order completes (no partial state)
   * - Destructor waits 1 second (drain queues)
   * 
   * THREAD SAFETY:
   * - run_ is volatile (visible across threads)
   * - No locking needed (simple flag)
   * - C++20: Could use std::atomic<bool> (more explicit)
   */
  auto MatchingEngine::stop() -> void {
    run_ = false;  // Disable main loop (thread will exit)
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. INLINE RUN METHOD:
 *    - run() is implemented in header (inline)
 *    - Reason: Performance optimization (avoid call overhead)
 *    - Hot path: Called millions of times per second
 *    - Compiler can better optimize (inlining, loop unrolling)
 * 
 * 2. HEAP vs STACK ALLOCATION:
 *    - Order books: Heap allocated (new/delete)
 *    - Reason: Large objects (~1 MB each with memory pools)
 *    - Stack: Limited (~8 MB default), would overflow
 *    - Alternative: std::unique_ptr (modern C++, automatic cleanup)
 * 
 * 3. LOGGER INITIALIZATION:
 *    - logger_("exchange_matching_engine.log")
 *    - Constructor initializer list (efficient)
 *    - Logger creates file, opens, ready for use
 * 
 * 4. QUEUE OWNERSHIP:
 *    - Queues owned by caller (main.cpp)
 *    - Matching engine only stores pointers
 *    - Don't delete in destructor (would double-free)
 *    - Nullify pointers (good practice, prevent dangling)
 * 
 * 5. THREAD LIFECYCLE:
 *    - start(): Create thread, begin processing
 *    - run(): Main loop (implemented in header)
 *    - stop(): Signal thread to exit (set flag)
 *    - ~MatchingEngine(): Wait and cleanup
 * 
 * 6. ERROR HANDLING:
 *    - ASSERT: Thread creation must succeed
 *    - Fatal if fails: Cannot operate without thread
 *    - Production: More graceful error handling (return error code)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Lazy Order Book Initialization:
 *    - Don't create all order books at startup
 *    - Create on first order for that ticker
 *    - Advantage: Faster startup, less memory
 *    - Disadvantage: First order per ticker slower
 *    - Not suitable: Latency spike unpredictable
 * 
 * B) std::unique_ptr for Order Books:
 *    - std::array<std::unique_ptr<MEOrderBook>, ME_MAX_TICKERS>
 *    - Advantage: Automatic cleanup (RAII)
 *    - Advantage: Move semantics (no manual delete)
 *    - Modern C++ best practice
 * 
 * C) Thread Pool:
 *    - Multiple matching engine threads
 *    - Work-stealing queue
 *    - Advantage: Higher throughput potential
 *    - Disadvantage: Complexity, locking, non-deterministic
 *    - Not suitable for HFT (predictability critical)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Thread affinity: Pin to dedicated core (not -1)
 * - Priority: Set real-time priority (SCHED_FIFO)
 * - Monitoring: Expose metrics (orders/sec, latency percentiles)
 * - Graceful shutdown: Wait for thread join (not just sleep)
 * - Error recovery: Handle order book creation failures
 * - Dynamic instruments: Add/remove tickers at runtime
 */
