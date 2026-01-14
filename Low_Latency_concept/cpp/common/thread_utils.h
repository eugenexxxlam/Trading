#pragma once

#include <iostream>
#include <atomic>
#include <thread>
#include <unistd.h>

#include <sys/syscall.h>

/*
 * THREAD UTILITIES - CPU AFFINITY AND THREAD MANAGEMENT FOR HFT
 * ==============================================================
 * 
 * PURPOSE:
 * Thread creation with CPU pinning (affinity) for deterministic performance.
 * In HFT, pinning threads to specific CPU cores prevents context switches
 * and cache invalidation, reducing latency jitter.
 * 
 * WHY CPU AFFINITY IN HFT?
 * - Context switches: 1-10 microseconds of latency (unacceptable)
 * - Cache invalidation: L1/L2/L3 caches flushed on core migration
 * - NUMA: Keep thread + memory on same socket (faster access)
 * - Predictability: Same core = consistent performance
 * - Isolation: Critical threads never interrupted
 * 
 * TYPICAL HFT CORE ALLOCATION:
 * - Core 0: Matching engine (most critical)
 * - Core 1: Market data publisher
 * - Core 2: Order gateway
 * - Core 3: Risk manager
 * - Core 4+: Trading strategies
 * - Last cores: Background (logging, monitoring)
 * 
 * LATENCY IMPACT:
 * - Pinned threads: p99 latency = 500 ns (consistent)
 * - Unpinned: p99 = 5-50 microseconds (context switches)
 * - 10-100x improvement from pinning alone!
 */

namespace Common {
  /*
   * SET THREAD CPU AFFINITY
   * =======================
   * Pins calling thread to specific CPU core.
   * 
   * WHAT IT DOES:
   * - Uses pthread_setaffinity_np (Linux-specific)
   * - Sets CPU affinity mask to single core
   * - Thread will only run on specified core
   * - OS won't migrate thread to other cores
   * 
   * PARAMETERS:
   * - core_id: CPU core number (0-N where N = number of cores - 1)
   * 
   * RETURN:
   * - true: Successfully pinned
   * - false: Failed (invalid core, permissions, etc.)
   * 
   * CPU TOPOLOGY:
   * - Core 0-7: Physical cores on socket 0
   * - Core 8-15: Physical cores on socket 1 (NUMA)
   * - Core 16-31: Hyperthreads (virtual cores)
   * - Best: Use physical cores, avoid hyperthreads
   * 
   * NUMA CONSIDERATIONS:
   * - Keep thread + memory on same NUMA node
   * - Cross-socket memory: 2-3x slower than local
   * - Check: numactl --hardware
   */
  inline auto setThreadCore(int core_id) noexcept {
    cpu_set_t cpuset;  // CPU affinity mask (bitset of cores)
    
    CPU_ZERO(&cpuset);  // Clear all bits (no cores selected)
    CPU_SET(core_id, &cpuset);  // Set bit for core_id (select that core)
    
    // Set affinity for current thread (pthread_self)
    return (pthread_setaffinity_np(pthread_self(),  // Current thread
                                   sizeof(cpu_set_t),  // Size of mask
                                   &cpuset) == 0);  // Affinity mask (single core)
    // Returns: 0 on success, non-zero on error
  }

  /*
   * CREATE AND START THREAD WITH AFFINITY
   * ======================================
   * Creates thread, pins to core, names it, and runs function.
   * 
   * TEMPLATE PARAMETERS:
   * - T: Function type
   * - A...: Argument types (variadic)
   * 
   * PARAMETERS:
   * - core_id: CPU core (-1 = don't pin, >=0 = pin to that core)
   * - name: Thread name for debugging
   * - func: Function to run
   * - args: Arguments to pass to function
   * 
   * RETURN:
   * - std::thread* pointer (caller must join/detach)
   * 
   * THREAD LIFECYCLE:
   * 1. Create thread object
   * 2. Lambda captures arguments
   * 3. Set CPU affinity (if core_id >= 0)
   * 4. Log affinity result
   * 5. Forward arguments and call function
   * 6. Sleep 1 second (let thread initialize)
   * 7. Return thread pointer
   * 
   * PRODUCTION USAGE:
   * ```cpp
   * auto* thread = createAndStartThread(
   *     0,  // Pin to core 0
   *     "MatchingEngine",
   *     &MatchingEngine::run,
   *     &engine
   * );
   * // ... let it run ...
   * thread->join();
   * ```
   */
  template<typename T, typename... A>
  inline auto createAndStartThread(int core_id, const std::string &name, T &&func, A &&... args) noexcept {
    auto t = new std::thread([&]() {  // Create thread with lambda
      // Lambda body runs in new thread
      
      // Set CPU affinity if requested
      if (core_id >= 0 && !setThreadCore(core_id)) {  // core_id >= 0 means pin
        std::cerr << "Failed to set core affinity for " << name << " " 
                  << pthread_self() << " to " << core_id << std::endl;
        exit(EXIT_FAILURE);  // Fatal: can't guarantee performance without pinning
      }
      std::cerr << "Set core affinity for " << name << " " 
                << pthread_self() << " to " << core_id << std::endl;

      // Forward and call function with arguments
      std::forward<T>(func)((std::forward<A>(args))...);  // Perfect forwarding
      // std::forward: Preserves lvalue/rvalue nature of arguments
      // Allows move semantics when possible
    });

    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);  // Wait for thread to initialize
                                      // Ensures affinity is set before returning

    return t;  // Return thread pointer
  }
}

/*
 * CPU AFFINITY BEST PRACTICES
 * ============================
 * 
 * 1. CORE SELECTION:
 *    - Use physical cores (not hyperthreads)
 *    - Isolate critical cores (kernel boot param: isolcpus=0-7)
 *    - Check topology: lscpu, numactl --hardware
 * 
 * 2. NUMA AWARENESS:
 *    - Pin thread + allocate memory on same socket
 *    - Cross-socket: 2-3x slower
 *    - Use: numactl --membind=0 --cpunodebind=0 ./program
 * 
 * 3. INTERRUPT STEERING:
 *    - Move IRQs away from trading cores
 *    - /proc/irq/N/smp_affinity (where N is IRQ number)
 *    - Keep cores 0-7 for trading, 8+ for interrupts
 * 
 * 4. MONITORING:
 *    - Check actual core: taskset -cp <pid>
 *    - Migration count: /proc/<pid>/status | grep voluntary_ctxt_switches
 *    - Zero migrations = perfect affinity
 * 
 * 5. TESTING:
 *    - Measure latency with/without pinning
 *    - Expect: 10-100x lower p99 with pinning
 *    - Use: perf stat -e context-switches ./program
 */
