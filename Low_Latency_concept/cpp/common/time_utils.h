#pragma once

#include <string>
#include <chrono>
#include <ctime>

#include "perf_utils.h"

/*
 * TIME UTILITIES FOR LOW-LATENCY TRADING SYSTEMS
 * ===============================================
 * 
 * PURPOSE:
 * High-precision time measurement and formatting for HFT systems where
 * nanosecond-level accuracy is critical for:
 * - Latency measurement (order processing time)
 * - Timestamp generation (when did event occur)
 * - Performance profiling (hot path analysis)
 * - Regulatory compliance (audit trails with microsecond precision)
 * 
 * WHY NANOSECOND PRECISION?
 * - Modern HFT: Operations complete in 100-1000 nanoseconds
 * - Microseconds too coarse (1000 ns = 1 us - lose granularity)
 * - Nanoseconds: 1 billionth of a second (1e-9 sec)
 * - Allows measuring operations that take <1 microsecond
 * 
 * TIME SOURCES IN TRADING:
 * 1. CLOCK_REALTIME (system_clock): Wall clock time, can jump (NTP adjustments)
 * 2. CLOCK_MONOTONIC: Never goes backward, but can drift
 * 3. RDTSC: CPU cycle counter, fastest but not portable time
 * 4. PTP (Precision Time Protocol): Synchronized across machines (exchange + clients)
 * 
 * PERFORMANCE CONSIDERATIONS:
 * - getCurrentNanos(): ~20-50 ns (syscall overhead)
 * - RDTSC: ~5-10 ns (CPU instruction, no syscall)
 * - String formatting: 1-10 microseconds (SLOW - avoid in hot path!)
 * 
 * USAGE PATTERNS:
 * Hot path: Use getCurrentNanos() for timestamps
 * Cold path: Use getCurrentTimeStr() for human-readable logs
 * Profiling: Use RDTSC (via perf_utils.h) for cycle-accurate measurement
 */

namespace Common {
  /*
   * NANOSECOND TIMESTAMP TYPE
   * =========================
   * Represents time as nanoseconds since epoch (Jan 1, 1970 00:00:00 UTC).
   * 
   * WHY int64_t?
   * - Range: -9.2 quintillion to +9.2 quintillion nanoseconds
   * - In years: ~292 years before/after epoch (1678 to 2262)
   * - Sufficient for any realistic trading system lifetime
   * - Fixed-width: Exactly 64 bits on all platforms (portable)
   * - Signed: Can represent time before epoch (rarely needed)
   * 
   * PRECISION:
   * - 1 nanosecond = 0.000000001 seconds = 1e-9 sec
   * - Resolution: Can represent times to 1 billionth of a second
   * - Reality: System clock usually ~100 ns resolution (hardware dependent)
   * 
   * USAGE:
   * ```cpp
   * Nanos start_time = getCurrentNanos();
   * process_order(order);
   * Nanos end_time = getCurrentNanos();
   * Nanos latency = end_time - start_time;  // Latency in nanoseconds
   * logger.log("Order processing took % ns\n", latency);
   * ```
   */
  typedef int64_t Nanos;  // Nanoseconds since epoch (Jan 1, 1970 00:00:00 UTC)
                          // Typedef for clarity and consistency across codebase

  /*
   * TIME UNIT CONVERSION CONSTANTS
   * ==============================
   * Constants for converting between different time units.
   * 
   * TIME HIERARCHY:
   * 1 second (s) = 1,000 milliseconds (ms)
   * 1 millisecond = 1,000 microseconds (us or μs)
   * 1 microsecond = 1,000 nanoseconds (ns)
   * Therefore: 1 second = 1,000,000,000 nanoseconds
   * 
   * WHY CONSTEXPR?
   * - Compile-time constants (no runtime cost)
   * - Compiler can optimize away divisions/multiplications
   * - Type-safe (unlike #define macros)
   * - Can use in constexpr contexts
   * 
   * USAGE:
   * ```cpp
   * Nanos nanos = 5000;
   * auto micros = nanos / NANOS_TO_MICROS;  // 5000 ns = 5 us
   * auto millis = nanos / NANOS_TO_MILLIS;  // 5000 ns = 0.005 ms
   * auto secs = nanos / NANOS_TO_SECS;      // 5000 ns = 0.000005 s
   * ```
   */
  constexpr Nanos NANOS_TO_MICROS = 1000;    // 1 microsecond = 1,000 nanoseconds
                                             // Divide nanoseconds by this to get microseconds
  
  constexpr Nanos MICROS_TO_MILLIS = 1000;   // 1 millisecond = 1,000 microseconds
                                             // Divide microseconds by this to get milliseconds
  
  constexpr Nanos MILLIS_TO_SECS = 1000;     // 1 second = 1,000 milliseconds
                                             // Divide milliseconds by this to get seconds
  
  constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;  // 1,000,000
                                                                          // 1 millisecond = 1 million nanoseconds
  
  constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;      // 1,000,000,000
                                                                          // 1 second = 1 billion nanoseconds

  /*
   * GET CURRENT TIMESTAMP IN NANOSECONDS
   * =====================================
   * Returns current system time as nanoseconds since Unix epoch.
   * 
   * WHAT IT DOES:
   * 1. Queries system clock (std::chrono::system_clock)
   * 2. Gets time since epoch (Jan 1, 1970 00:00:00 UTC)
   * 3. Converts to nanoseconds
   * 4. Returns as int64_t
   * 
   * TIME SOURCE:
   * - Uses system_clock (CLOCK_REALTIME on Linux)
   * - Wall clock time (what you see on your watch)
   * - Can jump backward/forward if system time adjusted (NTP sync)
   * - Resolution: Typically 1-100 nanoseconds (hardware dependent)
   * 
   * PERFORMANCE:
   * - Latency: ~20-50 nanoseconds (involves syscall on Linux)
   * - On some systems: can be faster if vDSO is used
   * - Alternative: rdtsc() ~5-10 ns but returns CPU cycles, not time
   * 
   * ACCEPTABLE FOR HOT PATH?
   * - Yes: 20-50 ns overhead is acceptable for timestamping
   * - Often necessary: Need to know when events occurred
   * - Use judiciously: Don't call unnecessarily in tight loops
   * 
   * vs. RDTSC:
   * - getCurrentNanos(): Returns actual time, portable, ~20-50 ns
   * - rdtsc(): Returns CPU cycles, not portable, ~5-10 ns but need conversion
   * - Use this for timestamps, rdtsc() for latency measurement
   * 
   * THREAD SAFETY:
   * - Thread-safe: Multiple threads can call simultaneously
   * - No shared state (pure function)
   * 
   * PRECISION vs. ACCURACY:
   * - Precision: Can represent 1 ns differences (int64_t)
   * - Accuracy: System clock typically 100 ns granularity
   * - Example: Two calls 10 ns apart may return same value
   * 
   * USAGE EXAMPLES:
   * ```cpp
   * // Timestamp an event
   * Nanos order_received_time = getCurrentNanos();
   * 
   * // Measure latency
   * Nanos start = getCurrentNanos();
   * process_order(order);
   * Nanos end = getCurrentNanos();
   * Nanos latency = end - start;  // Processing latency in nanoseconds
   * 
   * // Log with timestamp
   * logger.log("% Order received: id=% price=%\n", 
   *            getCurrentNanos(), order_id, price);
   * ```
   * 
   * CAVEATS:
   * - Can go backward if system time adjusted (NTP, manual change)
   * - For monotonic time (never backward): use steady_clock
   * - Across machines: Need PTP (Precision Time Protocol) for sync
   */
  inline auto getCurrentNanos() noexcept {  // inline = suggest inlining (small function)
                                            // noexcept = no exceptions (critical for HFT)
    return std::chrono::duration_cast<std::chrono::nanoseconds>(  // Cast duration to nanoseconds
      std::chrono::system_clock::now()                            // Get current time point
        .time_since_epoch()                                       // Duration since Jan 1, 1970 00:00:00 UTC
    ).count();                                                    // Extract count as int64_t
    
    // Step-by-step breakdown:
    // 1. system_clock::now(): Get current time point (now)
    // 2. .time_since_epoch(): Convert to duration since Unix epoch
    // 3. duration_cast<nanoseconds>(): Convert to nanoseconds (from whatever internal unit)
    // 4. .count(): Extract the numeric value (int64_t)
    // Result: Number of nanoseconds since Jan 1, 1970 00:00:00 UTC
  }

  /*
   * FORMAT TIMESTAMP AS HUMAN-READABLE STRING
   * ==========================================
   * Converts current time to human-readable string format: "HH:MM:SS.nnnnnnnnn"
   * 
   * PURPOSE:
   * For logging and debugging - humans can't read nanosecond timestamps!
   * Example output: "14:23:47.123456789" (HH:MM:SS.nanoseconds)
   * 
   * FORMAT:
   * - HH:MM:SS: Hours:Minutes:Seconds (24-hour format)
   * - .nnnnnnnnn: Nanoseconds (9 digits, zero-padded)
   * - Total: 18 characters + null terminator
   * 
   * PERFORMANCE WARNING:
   * - Latency: 1-10 MICROSECONDS (very slow!)
   * - Why slow? String formatting, sprintf, ctime parsing
   * - 50-500x slower than getCurrentNanos()
   * - NEVER use in hot path!
   * - Only for cold path logging (setup, shutdown, errors)
   * 
   * WHEN TO USE:
   * - Debug logs (not hot path)
   * - Error messages
   * - Startup/shutdown logs
   * - Human-readable audit trails
   * 
   * WHEN NOT TO USE:
   * - Hot path (order processing, market data)
   * - High-frequency logging
   * - Performance-critical sections
   * - Anywhere latency matters
   * 
   * ALTERNATIVE FOR HOT PATH:
   * - Log raw nanosecond timestamps
   * - Convert to human-readable offline (post-processing)
   * - Use async logger (background thread does formatting)
   * 
   * PARAMETERS:
   * - time_str: Pointer to string to write result into (modified in-place)
   * 
   * RETURN VALUE:
   * - Reference to the modified string (for chaining)
   * 
   * THREAD SAFETY:
   * - ctime() is NOT thread-safe (uses static buffer)
   * - sprintf() is thread-safe (writes to our buffer)
   * - Overall: Not thread-safe due to ctime()
   * - Each thread should have own time_str to avoid races
   * 
   * USAGE:
   * ```cpp
   * std::string time_str;
   * getCurrentTimeStr(&time_str);
   * logger.log("Event occurred at: %\n", time_str);
   * // Output: "Event occurred at: 14:23:47.123456789"
   * ```
   */
  inline auto& getCurrentTimeStr(std::string* time_str) {  // Returns reference to input string
                                                           // inline = suggest inlining
                                                           // NOT noexcept (string operations may throw)
    
    // STEP 1: Get current time point
    const auto clock = std::chrono::system_clock::now();  // Current time point (high resolution)
                                                          // Same source as getCurrentNanos()
    
    // STEP 2: Convert to time_t (seconds since epoch)
    const auto time = std::chrono::system_clock::to_time_t(clock);  // Convert to time_t (for ctime)
                                                                     // time_t = seconds since epoch
                                                                     // Loses sub-second precision temporarily
    
    // STEP 3: Format the time string
    char nanos_str[24];  // Buffer for formatted string (stack allocated)
                         // 24 bytes: "HH:MM:SS.nnnnnnnnn" + some extra
    
    sprintf(nanos_str,              // Write formatted string to buffer
            "%.8s.%09ld",           // Format string (explained below)
            ctime(&time) + 11,      // Extract "HH:MM:SS" from ctime output (skip date)
            std::chrono::duration_cast<std::chrono::nanoseconds>(  // Get nanoseconds part
              clock.time_since_epoch()  // Duration since epoch
            ).count() % NANOS_TO_SECS);  // Modulo to get sub-second nanoseconds (0-999999999)
    
    // Format string breakdown:
    // "%.8s.%09ld"
    // - %.8s: Take first 8 characters of string (HH:MM:SS from ctime)
    // - .: Literal dot separator
    // - %09ld: Format long int with 9 digits, zero-padded (nanoseconds)
    //
    // ctime(&time) returns: "Tue Jan 14 14:23:47 2026\n"
    // +11 skips to position 11: "14:23:47 2026\n"
    // %.8s takes first 8 chars: "14:23:47"
    //
    // clock.time_since_epoch().count() returns total nanoseconds
    // % NANOS_TO_SECS gets remainder after dividing by 1 billion
    // This gives 0-999999999 (the sub-second nanoseconds)
    // %09ld formats as 9-digit zero-padded number: "000000123" to "999999999"
    
    // STEP 4: Copy formatted C-string into std::string
    time_str->assign(nanos_str);  // Copy C-string to std::string
                                  // assign() replaces entire string contents
                                  // More efficient than operator= for C-strings

    // STEP 5: Return reference to the string
    return *time_str;  // Dereference pointer to return reference
                       // Allows chaining: auto& s = getCurrentTimeStr(&str);
  }
}

/*
 * TIME MEASUREMENT BEST PRACTICES IN HFT
 * =======================================
 * 
 * 1. CHOOSE RIGHT TIME SOURCE:
 *    - Event timestamps: getCurrentNanos() (need actual time)
 *    - Latency measurement: rdtsc() (fastest, cycle-accurate)
 *    - Cross-machine sync: PTP (Precision Time Protocol)
 *    - Monotonic intervals: std::chrono::steady_clock (never backward)
 * 
 * 2. MINIMIZE TIMING OVERHEAD:
 *    - Don't call getCurrentNanos() unnecessarily in tight loops
 *    - Cache timestamp at loop start if all iterations need same time
 *    - Use rdtsc() for profiling (lower overhead)
 *    - Consider overhead of timing itself (~20-50 ns per call)
 * 
 * 3. TIMESTAMPING STRATEGY:
 *    - Timestamp at key points: Order received, matched, sent
 *    - Don't timestamp every minor operation (adds up)
 *    - Log raw nanoseconds in hot path (defer formatting)
 *    - Use getCurrentTimeStr() only in cold path (slow!)
 * 
 * 4. CLOCK SYNCHRONIZATION:
 *    - Within machine: All clocks synchronized
 *    - Across machines: Requires PTP or NTP
 *    - Trading systems: PTP mandatory (microsecond accuracy across network)
 *    - NTP: Only millisecond accuracy (insufficient for HFT)
 * 
 * 5. HANDLING CLOCK ADJUSTMENTS:
 *    - system_clock can jump (NTP sync, manual adjustment)
 *    - For intervals: Use steady_clock (monotonic, never backward)
 *    - Detect backward jumps: Compare consecutive timestamps
 *    - Log warnings: Alert operators to time anomalies
 * 
 * 6. LATENCY MEASUREMENT EXAMPLE:
 * ```cpp
 * // Method 1: Using getCurrentNanos() (portable, actual time)
 * Nanos start = getCurrentNanos();
 * process_order(order);
 * Nanos end = getCurrentNanos();
 * Nanos latency_ns = end - start;
 * 
 * // Method 2: Using rdtsc() (faster, cycle count)
 * uint64_t start_cycles = rdtsc();
 * process_order(order);
 * uint64_t end_cycles = rdtsc();
 * uint64_t cycles = end_cycles - start_cycles;
 * // Convert cycles to nanoseconds: ns = cycles / (CPU_GHz)
 * // Example: 3.5 GHz CPU -> 350 cycles ≈ 100 ns
 * ```
 * 
 * 7. LOGGING WITH TIMESTAMPS:
 * ```cpp
 * // Hot path: Log raw nanoseconds
 * logger.log("% OrderReceived id=% price=%\n", 
 *            getCurrentNanos(), order_id, price);
 * 
 * // Cold path: Human-readable
 * std::string time_str;
 * logger.log("% System startup complete\n", getCurrentTimeStr(&time_str));
 * ```
 * 
 * 8. PERFORMANCE PROFILING:
 * ```cpp
 * // Macro for cycle-accurate measurement (from perf_utils.h)
 * START_MEASURE(process_order);
 * process_order(order);
 * END_MEASURE(process_order, logger);
 * // Logs: "process_order took 347 cycles" or "process_order took 99 ns"
 * ```
 * 
 * 9. REGULATORY COMPLIANCE:
 * - MiFID II: Requires microsecond timestamps (this provides nanoseconds)
 * - SEC CAT: Requires clock synchronization within 50 ms
 * - Audit trails: Must timestamp all orders, cancels, trades
 * - This utility provides precision needed for compliance
 * 
 * 10. COMMON PITFALLS:
 * - Using getCurrentTimeStr() in hot path (too slow!)
 * - Not accounting for clock adjustments (backward time jumps)
 * - Comparing timestamps across unsynchronized machines
 * - Ignoring overhead of time calls themselves
 * - Using millisecond precision for HFT (too coarse)
 * 
 * ALTERNATIVE TIME SOURCES:
 * - CLOCK_MONOTONIC: Never goes backward (std::chrono::steady_clock)
 * - CLOCK_MONOTONIC_RAW: Like monotonic but unaffected by NTP
 * - TSC (Time Stamp Counter): CPU cycle counter (rdtsc instruction)
 * - HPET (High Precision Event Timer): Hardware timer (legacy)
 * - PTP (IEEE 1588): Network time sync protocol (sub-microsecond across LAN)
 */
