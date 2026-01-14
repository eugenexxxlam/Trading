#pragma once

/*
 * PERFORMANCE MEASUREMENT UTILITIES - CYCLE-ACCURATE PROFILING FOR HFT
 * =====================================================================
 * 
 * PURPOSE:
 * Ultra-low-overhead performance measurement using CPU's Time Stamp Counter (TSC).
 * Essential for profiling hot paths where even nanoseconds matter.
 * 
 * RDTSC - READ TIME STAMP COUNTER:
 * - CPU instruction that reads internal cycle counter
 * - Latency: ~5-15 nanoseconds (fastest timing method)
 * - Accuracy: Single CPU cycle resolution (~0.3 ns on 3 GHz CPU)
 * - No syscall: Pure CPU instruction (vs 20-50 ns for clock_gettime)
 * 
 * WHY RDTSC FOR HFT?
 * - Overhead: 5-15 ns vs 20-50 ns for getCurrentNanos()
 * - Resolution: Cycle-accurate vs ~100 ns resolution for system clock
 * - Determinism: Direct hardware counter, minimal variance
 * - Hot path: Low enough overhead to use in production
 * 
 * WHEN TO USE:
 * - Profiling hot path (order matching, market data parsing)
 * - Measuring sub-microsecond operations
 * - Production monitoring (acceptable overhead)
 * - Finding performance bottlenecks
 * 
 * WHEN NOT TO USE:
 * - Cross-machine comparison (TSC not synchronized across machines)
 * - Long durations (TSC can overflow, though takes years)
 * - CPU frequency changes (need to handle with care)
 * - Absolute time (use getCurrentNanos() instead)
 * 
 * CAVEATS:
 * - TSC counts CPU cycles, not nanoseconds
 * - Conversion: cycles / CPU_GHz = nanoseconds
 * - Example: 350 cycles on 3.5 GHz CPU = 100 ns
 * - Modern CPUs: "invariant TSC" (constant rate even with freq scaling)
 * 
 * TYPICAL MEASUREMENTS IN HFT:
 * - Order book add: 200-500 cycles (~60-150 ns)
 * - Market data parse: 100-300 cycles (~30-100 ns)
 * - Risk check: 50-150 cycles (~15-45 ns)
 * - Memory copy: 10-50 cycles (~3-15 ns)
 */

namespace Common {
  /*
   * READ TIME STAMP COUNTER (RDTSC)
   * ================================
   * Returns current CPU cycle count from hardware register.
   * 
   * HOW IT WORKS:
   * 1. Executes RDTSC CPU instruction
   * 2. Reads 64-bit counter from CPU
   * 3. Counter increments every CPU cycle
   * 4. Returns as uint64_t
   * 
   * ASSEMBLY EXPLANATION:
   * - "rdtsc": The x86 instruction to read TSC
   * - "=a" (lo): Output to EAX register (low 32 bits)
   * - "=d" (hi): Output to EDX register (high 32 bits)
   * - Combines to 64-bit: (hi << 32) | lo
   * 
   * PERFORMANCE:
   * - Latency: ~5-15 nanoseconds
   * - Serializing: Can use RDTSCP for ordered execution
   * - Overhead: Low enough for production hot path
   * 
   * PORTABILITY:
   * - x86/x86_64 only (Intel, AMD)
   * - ARM alternative: PMCCNTR (Performance Monitor Cycle Counter)
   * - Need #ifdef for cross-platform
   * 
   * USAGE:
   * ```cpp
   * uint64_t start = rdtsc();
   * process_order(order);
   * uint64_t end = rdtsc();
   * uint64_t cycles = end - start;
   * // Convert to ns: cycles / (CPU_GHz)
   * // Example: 350 cycles / 3.5 GHz = 100 ns
   * ```
   */
  inline auto rdtsc() noexcept {  // inline = no function call overhead
                                  // noexcept = no exception overhead
    unsigned int lo, hi;  // Low and high 32-bit parts of 64-bit counter
    
    // Inline assembly: Execute RDTSC instruction
    __asm__ __volatile__ (  // __asm__ = inline assembly block
                            // __volatile__ = prevent compiler optimization (don't reorder/remove)
      "rdtsc"              // x86 instruction: Read Time-Stamp Counter
      : "=a" (lo),         // Output constraints: lo = EAX register (low 32 bits)
        "=d" (hi)          // hi = EDX register (high 32 bits)
    );
    // After RDTSC: EAX has low 32 bits, EDX has high 32 bits
    
    // Combine 32-bit parts into 64-bit counter
    return ((uint64_t) hi << 32) | lo;  // Shift high bits left 32, OR with low bits
                                        // Result: 64-bit cycle count
  }
}

/*
 * START_MEASURE MACRO
 * ===================
 * Marks the beginning of a timed section.
 * 
 * WHAT IT DOES:
 * - Reads TSC and stores in variable named TAG
 * - Creates variable in local scope
 * - No runtime overhead (macro expansion)
 * 
 * USAGE:
 * ```cpp
 * START_MEASURE(order_processing);
 * process_order(order);
 * END_MEASURE(order_processing, logger);
 * // Logs: "order_processing took 347 cycles"
 * ```
 * 
 * MACRO EXPANSION:
 * START_MEASURE(foo) expands to:
 * const auto foo = Common::rdtsc();
 */
#define START_MEASURE(TAG) const auto TAG = Common::rdtsc()  // Create variable TAG with current cycle count

/*
 * END_MEASURE MACRO
 * =================
 * Marks the end of a timed section and logs the elapsed cycles.
 * 
 * WHAT IT DOES:
 * 1. Reads TSC again (end time)
 * 2. Calculates difference: end - start
 * 3. Logs result with timestamp and tag name
 * 
 * PARAMETERS:
 * - TAG: Name of variable created by START_MEASURE
 * - LOGGER: Logger instance to write to
 * 
 * DO-WHILE(FALSE) PATTERN:
 * - Allows macro to act like single statement
 * - Can use in if statements without braces
 * - Common C/C++ idiom for multi-line macros
 * 
 * STRING CONCATENATION:
 * - #TAG converts macro argument to string literal
 * - "RDTSC "#TAG" %\n" becomes "RDTSC order_processing %\n"
 * 
 * USAGE:
 * ```cpp
 * START_MEASURE(matching);
 * match_order(order);
 * END_MEASURE(matching, logger);
 * // Output: "2026-01-14 14:23:47.123456789 RDTSC matching 347"
 * // 347 = CPU cycles elapsed
 * ```
 * 
 * CONVERTING CYCLES TO TIME:
 * - Cycles alone not meaningful (depends on CPU speed)
 * - Need CPU frequency: cycles / GHz = nanoseconds
 * - 3.5 GHz CPU: 350 cycles / 3.5 = 100 nanoseconds
 * - Or use perf tools to get CPU_CLK_UNHALTED.THREAD
 */
#define END_MEASURE(TAG, LOGGER)                                                              \
      do {                                                                                    \
        const auto end = Common::rdtsc();                                                     \
        LOGGER.log("% RDTSC "#TAG" %\n", Common::getCurrentTimeStr(&time_str_), (end - TAG)); \
      } while(false)
// Breakdown:
// 1. const auto end = Common::rdtsc(): Read TSC at end
// 2. (end - TAG): Calculate elapsed cycles (TAG from START_MEASURE)
// 3. LOGGER.log(...): Write to log with timestamp and cycle count
// 4. #TAG: Stringify TAG (e.g., "matching")
// 5. do { ... } while(false): Macro safety pattern

/*
 * TTT_MEASURE MACRO (TIMESTAMP MACRO)
 * ====================================
 * Logs current nanosecond timestamp (not cycle count).
 * 
 * WHAT IT DOES:
 * - Reads system clock (getCurrentNanos)
 * - Logs absolute timestamp
 * - Used for event marking, not duration measurement
 * 
 * TTT = "Time Tagged Trace" or similar naming convention
 * 
 * USAGE:
 * ```cpp
 * TTT_MEASURE(order_received, logger);
 * // Output: "2026-01-14 14:23:47.123456789 TTT order_received 1705240567123456789"
 * // Second number is absolute nanoseconds since epoch
 * ```
 * 
 * vs. START/END_MEASURE:
 * - START/END: Measures duration in cycles (relative)
 * - TTT: Logs absolute timestamp in nanoseconds
 * - Use START/END for "how long did this take?"
 * - Use TTT for "when did this happen?"
 * 
 * PERFORMANCE TRACING:
 * ```cpp
 * TTT_MEASURE(order_received, logger);
 * START_MEASURE(parse);
 * parse_order(data);
 * END_MEASURE(parse, logger);
 * TTT_MEASURE(order_processed, logger);
 * 
 * // Logs:
 * // TTT order_received 1705240567000000000
 * // RDTSC parse 234 cycles
 * // TTT order_processed 1705240567000000500
 * // Analysis: Total latency = 500 ns, parsing = 234 cycles (~67 ns @ 3.5 GHz)
 * ```
 */
#define TTT_MEASURE(TAG, LOGGER)                                                              \
      do {                                                                                    \
        const auto TAG = Common::getCurrentNanos();                                           \
        LOGGER.log("% TTT "#TAG" %\n", Common::getCurrentTimeStr(&time_str_), TAG);           \
      } while(false)
// Breakdown:
// 1. const auto TAG = Common::getCurrentNanos(): Get nanosecond timestamp
// 2. LOGGER.log(...): Write timestamp to log
// 3. #TAG: Stringify TAG (e.g., "order_received")
// 4. TAG (second %): Actual nanosecond value

/*
 * USAGE PATTERNS AND BEST PRACTICES
 * ==================================
 * 
 * 1. HOT PATH PROFILING:
 * ```cpp
 * START_MEASURE(hot_path);
 * critical_operation();
 * END_MEASURE(hot_path, logger);
 * // Overhead: ~10-30 ns (rdtsc + log enqueue)
 * ```
 * 
 * 2. NESTED MEASUREMENTS:
 * ```cpp
 * START_MEASURE(total);
 * START_MEASURE(parse);
 * parse_message();
 * END_MEASURE(parse, logger);
 * 
 * START_MEASURE(process);
 * process_message();
 * END_MEASURE(process, logger);
 * END_MEASURE(total, logger);
 * // Can see breakdown: total vs individual components
 * ```
 * 
 * 3. CONDITIONAL MEASUREMENT:
 * ```cpp
 * #ifdef ENABLE_PROFILING
 * START_MEASURE(operation);
 * #endif
 * 
 * do_operation();
 * 
 * #ifdef ENABLE_PROFILING
 * END_MEASURE(operation, logger);
 * #endif
 * // Zero overhead when profiling disabled
 * ```
 * 
 * 4. CONVERTING CYCLES TO NANOSECONDS:
 * ```cpp
 * uint64_t cycles = rdtsc_end - rdtsc_start;
 * constexpr double CPU_GHZ = 3.5;  // Your CPU frequency
 * double nanoseconds = cycles / CPU_GHZ;
 * // Example: 350 cycles / 3.5 GHz = 100 ns
 * ```
 * 
 * 5. FINDING CPU FREQUENCY:
 * ```bash
 * # Linux
 * cat /proc/cpuinfo | grep "MHz"
 * lscpu | grep "MHz"
 * 
 * # macOS
 * sysctl -a | grep freq
 * ```
 * 
 * ANALYSIS WORKFLOW:
 * 1. Add START/END_MEASURE to suspect code
 * 2. Run system, collect logs
 * 3. Parse logs: extract cycle counts
 * 4. Statistical analysis: mean, median, p99
 * 5. Identify outliers and hotspots
 * 6. Optimize and re-measure
 * 
 * RDTSC ACCURACY CONSIDERATIONS:
 * - Out-of-order execution: May see negative cycles (use RDTSCP/LFENCE)
 * - Context switches: Huge cycle counts (filter outliers)
 * - Frequency scaling: Use invariant TSC (modern CPUs)
 * - Multicore: TSC may not be synchronized (pin threads to cores)
 * 
 * ALTERNATIVES:
 * - clock_gettime(CLOCK_MONOTONIC): 20-50 ns, portable
 * - getCurrentNanos(): Wrapper around clock_gettime
 * - perf stat: External profiling tool
 * - Intel VTune: Commercial profiler
 * - gprof/callgrind: Function-level profiling
 * 
 * PRODUCTION RECOMMENDATIONS:
 * - Use in development/staging for profiling
 * - Keep minimal measurements in production (key points only)
 * - Aggregate statistics (don't log every operation)
 * - Use separate profiling builds if overhead matters
 * - Monitor percentiles (p50, p99, p999) not just averages
 */