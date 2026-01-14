#pragma once

#include <cstring>
#include <iostream>

/*
 * COMPILER MACROS AND UTILITIES FOR LOW-LATENCY TRADING SYSTEMS
 * ==============================================================
 * 
 * PURPOSE:
 * Provides compiler hints and debugging utilities to optimize performance
 * and catch errors early in HFT systems.
 * 
 * KEY FEATURES:
 * 1. Branch prediction hints (LIKELY/UNLIKELY)
 * 2. Runtime assertions (ASSERT)
 * 3. Fatal error handling (FATAL)
 * 
 * WHY THESE MATTER IN HFT:
 * - Branch mispredictions cost 10-20 CPU cycles (~3-6 nanoseconds)
 * - In hot path processing billions of ops, this adds up
 * - Proper hints can improve performance by 5-15%
 * - Assertions catch bugs in development without production overhead
 */

/*
 * BRANCH PREDICTION HINTS
 * =======================
 * 
 * WHAT ARE THESE?
 * Modern CPUs use "speculative execution" - they predict which branch of an
 * if statement will be taken and start executing it before knowing for sure.
 * If the prediction is wrong, the CPU must discard the work and restart,
 * costing 10-20 cycles (~3-6 nanoseconds).
 * 
 * HOW DO THESE MACROS HELP?
 * LIKELY(x) tells compiler: "This condition is usually true"
 * UNLIKELY(x) tells compiler: "This condition is usually false"
 * Compiler arranges code so the common path is faster (better instruction cache usage).
 * CPU's branch predictor also learns from how code is laid out.
 * 
 * IMPLEMENTATION:
 * __builtin_expect() is GCC/Clang built-in function
 * - __builtin_expect(expr, expected_value) returns expr
 * - But tells compiler what value to expect (0 or 1)
 * - !!(x) converts x to boolean (0 or 1)
 * 
 * WHEN TO USE:
 * LIKELY: Error checks that rarely fail, normal path conditions
 * UNLIKELY: Error conditions, exceptional cases, bounds checks
 * 
 * PERFORMANCE IMPACT:
 * - Correct hints: 5-15% faster (fewer branch mispredictions)
 * - Incorrect hints: 5-10% slower (misleads branch predictor)
 * - Rule: Only use if you're 95%+ certain of the common case
 * 
 * EXAMPLES:
 * ```cpp
 * // Good use: Error check (usually passes)
 * if (LIKELY(order->price > 0)) {
 *   process_order(order);  // Common path - fast
 * }
 * 
 * // Good use: Exceptional condition (rarely happens)
 * if (UNLIKELY(queue.is_full())) {
 *   handle_overflow();     // Rare path - can be slow
 * }
 * 
 * // Bad use: 50/50 condition (don't hint!)
 * if (LIKELY(side == BUY)) {  // BAD: Buy/Sell are equally common
 *   ...
 * }
 * ```
 */

// LIKELY macro: Condition is expected to be TRUE most of the time
// Use for normal/success paths that execute 95%+ of the time
#define LIKELY(x) __builtin_expect(!!(x), 1)
// Breakdown:
// - x: The condition to evaluate
// - !!(x): Double negation converts any value to boolean (0 or 1)
// - __builtin_expect(..., 1): Tell compiler to expect value of 1 (true)
// - Compiler optimizes assuming this branch is taken

// UNLIKELY macro: Condition is expected to be FALSE most of the time  
// Use for error paths, edge cases that execute <5% of the time
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
// Breakdown:
// - x: The condition to evaluate
// - !!(x): Double negation converts any value to boolean (0 or 1)
// - __builtin_expect(..., 0): Tell compiler to expect value of 0 (false)
// - Compiler optimizes assuming this branch is NOT taken

/*
 * RUNTIME ASSERTION - DEVELOPMENT ERROR CHECKING
 * ===============================================
 * 
 * PURPOSE:
 * Validates invariants and assumptions during development/testing.
 * Crashes immediately if condition is false, preventing silent corruption.
 * 
 * WHAT IT DOES:
 * - Checks if condition is true
 * - If false: Prints error message and terminates program
 * - If true: No-op (continues execution)
 * 
 * WHEN TO USE:
 * - Validate preconditions (e.g., pointer not null)
 * - Check invariants (e.g., queue not empty before read)
 * - Catch programming errors early
 * - Document assumptions in code
 * 
 * WHEN NOT TO USE:
 * - Don't check user input (use proper error handling)
 * - Don't check external conditions (network, file system)
 * - Don't use in hot path of production code (can disable in release builds)
 * 
 * vs. STANDARD ASSERT:
 * - Standard assert: Disabled in release builds (no cost)
 * - This ASSERT: Always enabled (helps catch bugs in production)
 * - Trade-off: Safety vs. Performance
 * - Can modify to disable in release: #ifdef NDEBUG ... #endif
 * 
 * PERFORMANCE:
 * - If condition true: ~1-2 ns (one branch + UNLIKELY hint)
 * - If condition false: Program terminates (doesn't matter)
 * - UNLIKELY hint: Optimizes for success path (assertion not firing)
 * 
 * EXAMPLES:
 * ```cpp
 * // Validate pointer not null
 * ASSERT(order != nullptr, "Order pointer is null");
 * 
 * // Check array bounds
 * ASSERT(index < array_size, "Index out of bounds: " + std::to_string(index));
 * 
 * // Verify state invariant
 * ASSERT(queue.size() > 0, "Attempting to read from empty queue");
 * 
 * // Document precondition
 * ASSERT(price > 0, "Price must be positive");
 * ```
 */
inline auto ASSERT(bool cond, const std::string &msg) noexcept {
  // Check if condition is false (assertion failed)
  if (UNLIKELY(!cond)) {  // UNLIKELY: Assertions should almost never fail
                          // Optimizes for common case (cond is true)
    
    // Print assertion failure message to stderr
    std::cerr << "ASSERT : " << msg << std::endl;
    // stderr: Unbuffered output, appears immediately
    // Format: "ASSERT : <your message>"
    // Visible in console/logs for debugging

    // Terminate program immediately with failure code
    exit(EXIT_FAILURE);  // EXIT_FAILURE = 1 (indicates error)
                         // No cleanup, no destructors (fast crash)
                         // Core dump may be generated (if enabled)
                         // In production: Would log to file before exit
  }
  // If cond is true, function returns immediately (no-op)
  // noexcept: Never throws exceptions (critical for HFT)
}

/*
 * FATAL ERROR - UNRECOVERABLE ERROR HANDLING
 * ===========================================
 * 
 * PURPOSE:
 * Handles unrecoverable errors that require immediate program termination.
 * Used when continuing would be dangerous or impossible.
 * 
 * WHAT IT DOES:
 * - Prints error message to stderr
 * - Terminates program with failure code
 * - No cleanup, no exception handling (immediate exit)
 * 
 * WHEN TO USE:
 * - Unrecoverable initialization failures (can't open critical file)
 * - Corrupted data structures detected
 * - Out of memory in critical allocation
 * - Hardware/system failure detected
 * - Conditions that should "never happen"
 * 
 * vs. ASSERT:
 * - ASSERT: Checks condition, continues if true
 * - FATAL: Always terminates (no condition check)
 * - Use FATAL when continuing is unsafe/impossible
 * 
 * vs. EXCEPTIONS:
 * - Exceptions: Can be caught and handled
 * - FATAL: Cannot be caught, terminates immediately
 * - HFT systems often avoid exceptions (non-deterministic overhead)
 * - FATAL preferred for critical errors in hot path
 * 
 * PERFORMANCE:
 * - Doesn't matter - program is terminating!
 * - If never called: Zero overhead (just a function definition)
 * 
 * EXAMPLES:
 * ```cpp
 * // Initialization failure
 * if (!tcp_socket.connect(host, port)) {
 *   FATAL("Failed to connect to exchange at " + host);
 * }
 * 
 * // Memory allocation failure in critical path
 * void* mem = malloc(size);
 * if (mem == nullptr) {
 *   FATAL("Out of memory allocating " + std::to_string(size) + " bytes");
 * }
 * 
 * // Corrupted data structure detected
 * if (order_book.validate() == false) {
 *   FATAL("Order book corruption detected, cannot continue safely");
 * }
 * 
 * // Hardware failure
 * if (rdtsc() == 0) {  // Time stamp counter not working
 *   FATAL("CPU time stamp counter not available");
 * }
 * ```
 */
inline auto FATAL(const std::string &msg) noexcept {
  // Print fatal error message to stderr
  std::cerr << "FATAL : " << msg << std::endl;
  // stderr: Unbuffered, appears immediately
  // Format: "FATAL : <your message>"
  // Last message before program dies (crucial for debugging)

  // Terminate program immediately with failure code
  exit(EXIT_FAILURE);  // EXIT_FAILURE = 1 (non-zero exit code = error)
                       // OS knows program failed
                       // Monitoring systems can detect and alert
                       // No cleanup - immediate termination
  
  // Function never returns (always terminates)
  // noexcept: Never throws exceptions
}

/*
 * ADDITIONAL USEFUL MACROS (NOT IMPLEMENTED HERE BUT COMMON IN HFT)
 * ==================================================================
 * 
 * FORCE_INLINE:
 * #define FORCE_INLINE __attribute__((always_inline))
 * Forces compiler to inline function (no function call overhead)
 * Use for tiny hot-path functions called millions of times
 * 
 * NO_INLINE:
 * #define NO_INLINE __attribute__((noinline))
 * Prevents inlining (useful for cold paths to reduce code size)
 * Helps instruction cache performance
 * 
 * CACHE_LINE_ALIGNED:
 * #define CACHE_LINE_ALIGNED alignas(64)
 * Aligns data structure on cache line boundary (64 bytes)
 * Prevents false sharing between CPU cores
 * Critical for atomics shared between threads
 * 
 * PACKED:
 * #define PACKED __attribute__((packed))
 * Removes padding from structs (minimizes size)
 * Useful for network protocols, binary file formats
 * Trade-off: Smaller size vs slower unaligned access
 * 
 * PREFETCH:
 * #define PREFETCH(addr) __builtin_prefetch(addr)
 * Hints CPU to load data into cache before needed
 * Can hide memory latency in tight loops
 * Use when you know future memory access pattern
 * 
 * RESTRICT:
 * #define RESTRICT __restrict__
 * Tells compiler pointers don't alias (don't overlap)
 * Enables better optimization (reordering, vectorization)
 * Use when you guarantee pointers point to different objects
 * 
 * PERFORMANCE MEASUREMENT:
 * #define START_MEASURE(name) auto start_##name = rdtsc();
 * #define END_MEASURE(name, logger) { \
 *   auto end_##name = rdtsc(); \
 *   logger.log(#name " took % cycles\n", end_##name - start_##name); \
 * }
 * Wraps code with cycle-accurate timing
 * Uses CPU time stamp counter (rdtsc instruction)
 * Essential for profiling hot path performance
 * 
 * COMPILATION:
 * These macros work with GCC/Clang. For MSVC, use equivalent:
 * - __builtin_expect: No direct equivalent (use PGO instead)
 * - __attribute__((always_inline)): __forceinline
 * - __attribute__((noinline)): __declspec(noinline)
 * - alignas(64): __declspec(align(64))
 * - __attribute__((packed)): #pragma pack(1)
 */
