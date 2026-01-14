/*
 * ASYNC LOGGER EXAMPLE - LOW-LATENCY LOGGING FOR HFT SYSTEMS
 * ===========================================================
 * 
 * PURPOSE:
 * Demonstrates asynchronous lock-free logging for trading systems where
 * I/O operations (like writing to files) cannot block the hot path.
 * 
 * THE LOGGING PROBLEM IN HFT:
 * - printf/cout: 1-10 microseconds per call (involves syscalls, buffering, kernel)
 * - File writes: 10-1000 microseconds (disk I/O is very slow)
 * - Synchronous logging: Blocks trading thread while writing to disk
 * - In HFT: 1 microsecond delay can mean losing a trade
 * 
 * THE ASYNC LOGGING SOLUTION:
 * - Hot path: Enqueue log message to lock-free queue (~10-20 ns)
 * - Background thread: Dequeues and writes to file (no blocking of hot path)
 * - Trading thread returns immediately after enqueue
 * - File I/O happens asynchronously in separate thread
 * 
 * PERFORMANCE COMPARISON:
 * - Synchronous printf: 1-10 microseconds (1000-10000 ns) - TOO SLOW
 * - Async logger enqueue: 10-20 nanoseconds - ACCEPTABLE FOR HOT PATH
 * - 500-1000x faster than synchronous logging!
 * 
 * WHAT THIS EXAMPLE SHOWS:
 * 1. Creating async logger with output file
 * 2. Logging different data types (char, int, float, double, strings)
 * 3. Type-safe variadic template formatting (no printf vulnerabilities)
 * 4. Automatic background thread handling
 * 
 * REAL-WORLD USAGE:
 * - Trade execution logs: "Order 12345 filled at 100.50 qty 1000"
 * - Performance metrics: "Order processing took 347 nanoseconds"
 * - Risk checks: "Position limit exceeded: current=5000 max=10000"
 * - Debug information: "Market data received: ticker=AAPL bid=150.25 ask=150.26"
 */

#include "logging.h"

/*
 * MAIN FUNCTION - ASYNC LOGGER DEMONSTRATION
 * ===========================================
 * 
 * FLOW:
 * 1. Create sample data of different types
 * 2. Create logger instance (starts background thread)
 * 3. Log messages with different data types
 * 4. Logger destructor flushes remaining logs and stops background thread
 * 
 * KEY CONCEPTS:
 * - Lock-free enqueue (fast, non-blocking)
 * - Type-safe formatting (compiler checks types)
 * - Automatic background thread management
 * - Graceful shutdown on destruction
 */
int main(int, char **) {
  using namespace Common;

  /*
   * STEP 1: CREATE SAMPLE DATA
   * ==========================
   * Various data types to demonstrate logger's type-safe formatting.
   * 
   * In production, these would be:
   * - Order IDs, prices, quantities
   * - Timestamps, latencies
   * - Ticker symbols, market data
   * - Status codes, error messages
   */
  char c = 'd';                      // Single character (could be order side: 'B'=buy, 'S'=sell)
  int i = 3;                         // Integer (could be order count, position)
  unsigned long ul = 65;             // Large integer (could be order ID, timestamp)
  float f = 3.4;                     // Single precision (rarely used in finance)
  double d = 34.56;                  // Double precision (typical for prices in trading)
  const char* s = "test C-string";   // C-style string (efficient, no allocation)
  std::string ss = "test string";    // C++ string (convenient but has allocation overhead)

  /*
   * STEP 2: CREATE ASYNC LOGGER
   * ============================
   * Instantiate logger with output filename.
   * 
   * What happens internally:
   * 1. Opens file "logging_example.log" for writing
   * 2. Creates lock-free queue for log messages
   * 3. Spawns background thread that:
   *    - Continuously reads from queue
   *    - Writes messages to file
   *    - Flushes periodically
   * 4. Returns immediately (background thread runs asynchronously)
   * 
   * Performance: Initialization happens once at startup (not in hot path)
   * 
   * In production:
   * - One logger per component (OrderGateway.log, MatchingEngine.log, etc.)
   * - Log rotation (new file daily or when size limit reached)
   * - Multiple log levels (DEBUG, INFO, WARN, ERROR)
   */
  Logger logger("logging_example.log");  // Create logger writing to "logging_example.log"
                                         // Background thread starts immediately
                                         // File created/truncated if exists

  /*
   * STEP 3: LOG MESSAGES WITH DIFFERENT TYPES
   * ==========================================
   * Demonstrate type-safe variadic template logging.
   * 
   * FORMAT SYNTAX:
   * - % = placeholder for next argument (similar to printf's %s/%d/%f but type-safe)
   * - \n = newline (explicit, not added automatically)
   * - No format specifiers needed (type deduced automatically)
   * 
   * ADVANTAGES OVER printf:
   * - Type-safe: Compiler checks argument types match placeholders
   * - No format string vulnerabilities (no %n attacks)
   * - Async: Doesn't block for I/O
   * - Lock-free: Multiple threads can log concurrently (with proper design)
   */
  
  // Log multiple primitive types in one message
  logger.log("Logging a char:% an int:% and an unsigned:%\n", c, i, ul);
  // Template expands to: format string + arguments
  // Background thread will write: "Logging a char:d an int:3 and an unsigned:65\n"
  // Hot path cost: ~10-20 ns (just enqueue to lock-free queue)
  // Actual file write: happens in background thread (doesn't block us)
  
  // Log floating point types
  logger.log("Logging a float:% and a double:%\n", f, d);
  // Will write: "Logging a float:3.4 and a double:34.56\n"
  // Note: In production trading, avoid floats (use fixed-point or doubles)
  // float has insufficient precision for financial calculations
  
  // Log C-style string
  logger.log("Logging a C-string:'%'\n", s);
  // Will write: "Logging a C-string:'test C-string'\n"
  // C-strings: Efficient, no allocation, but less safe
  // Preferred in hot path for performance
  
  // Log C++ string
  logger.log("Logging a string:'%'\n", ss);
  // Will write: "Logging a string:'test string'\n"
  // std::string: More convenient but has allocation overhead
  // Acceptable in non-hot-path code

  /*
   * STEP 4: IMPLICIT CLEANUP
   * ========================
   * When logger goes out of scope (end of main):
   * 1. Logger destructor called
   * 2. Signals background thread to stop
   * 3. Waits for thread to flush remaining messages
   * 4. Closes log file
   * 5. Joins background thread
   * 
   * This ensures all log messages are written before program exits.
   * No messages lost even if program terminates quickly.
   */

  return 0;  // Logger destructor runs here, flushes all logs, stops thread
}

/*
 * KEY TAKEAWAYS - ASYNC LOGGING IN HFT
 * =====================================
 * 
 * 1. WHY ASYNC LOGGING?
 *    - File I/O is slow (10-1000 microseconds)
 *    - Hot path cannot wait for I/O
 *    - Solution: Enqueue log (~20 ns), background thread writes
 *    - 500-1000x faster than synchronous logging
 * 
 * 2. ARCHITECTURE:
 *    - Producer (hot path): Enqueues log message to lock-free queue
 *    - Consumer (background): Dequeues and writes to file
 *    - SPSC pattern: One logger per thread for lock-free operation
 * 
 * 3. TYPE SAFETY:
 *    - Variadic templates: Compile-time type checking
 *    - No printf format string bugs
 *    - % placeholder works with any type
 *    - Compiler ensures arguments match placeholders
 * 
 * 4. PERFORMANCE:
 *    - Enqueue: ~10-20 nanoseconds (acceptable for hot path)
 *    - Write: Happens in background (doesn't block hot path)
 *    - Total hot path impact: Negligible
 *    - Can log millions of messages/second
 * 
 * 5. PRODUCTION CONSIDERATIONS:
 *    - Log rotation: Prevent log files from growing unbounded
 *    - Log levels: DEBUG, INFO, WARN, ERROR (filter by severity)
 *    - Timestamps: Add microsecond timestamps to each log
 *    - Per-thread loggers: Avoid contention between threads
 *    - Monitoring: Track queue depth (if full, logs being dropped)
 * 
 * PRODUCTION USAGE PATTERNS:
 * 
 * Trading System Example:
 * ```cpp
 * Logger order_logger("orders.log");
 * Logger md_logger("market_data.log");
 * Logger perf_logger("performance.log");
 * 
 * // Hot path: Order processing
 * START_MEASURE(process_order);
 * process_order(order);
 * END_MEASURE(process_order, perf_logger);  // Logs: "process_order took 347 ns"
 * 
 * // Hot path: Market data
 * md_logger.log("%:% MD Update ticker:% bid:% ask:% qty:%\n",
 *               __FILE__, __LINE__, ticker_id, bid_price, ask_price, quantity);
 * 
 * // Hot path: Order execution
 * order_logger.log("Order % filled: price=% qty=% latency=%ns\n",
 *                  order_id, fill_price, fill_qty, latency);
 * ```
 * 
 * COMPARISON TO ALTERNATIVES:
 * 
 * printf/cout:
 * - Latency: 1-10 microseconds
 * - Hot path: NO (too slow)
 * - Thread-safe: cout is (with mutex), printf mostly
 * - Verdict: Unusable for HFT hot path
 * 
 * fprintf/fwrite:
 * - Latency: 10-1000 microseconds (disk I/O)
 * - Hot path: NO (even slower)
 * - Buffering: Helps but still slow
 * - Verdict: Unusable for HFT hot path
 * 
 * spdlog (async mode):
 * - Latency: ~50-100 ns enqueue
 * - Hot path: Maybe (slower than custom)
 * - Features: Rich (rotation, formatting)
 * - Verdict: Good general-purpose, but custom is faster
 * 
 * Custom async logger (this):
 * - Latency: ~10-20 ns enqueue
 * - Hot path: YES (acceptable)
 * - Features: Basic but sufficient
 * - Verdict: Optimal for HFT hot path
 * 
 * BEST PRACTICES:
 * - Keep log messages short (less data to enqueue)
 * - Use C-strings in hot path (no allocation)
 * - Add timestamps in background thread (not hot path)
 * - Monitor queue depth (detect if falling behind)
 * - Size queue appropriately (based on burst rate)
 * - Use separate loggers per thread (no contention)
 * - Flush before shutdown (ensure no lost messages)
 * 
 * DEBUGGING WITH LOGS:
 * - Trace execution flow without debugger
 * - Measure latencies in production
 * - Audit trail for compliance
 * - Post-mortem analysis of issues
 * - Performance profiling over time
 * - But remember: Logging adds latency, use judiciously in hot path!
 */
