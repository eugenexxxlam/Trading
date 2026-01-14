/*
 * MEMORY POOL EXAMPLE - DEMONSTRATING CUSTOM ALLOCATION FOR HFT
 * ==============================================================
 * 
 * PURPOSE:
 * Demonstrates how to use memory pools for fast, deterministic object allocation
 * in low-latency trading systems. Shows allocation/deallocation of both primitive
 * types (double) and complex structs.
 * 
 * WHY MEMORY POOLS IN HFT:
 * - malloc/new: 100-500 ns per allocation (too slow for hot path)
 * - Memory pool: 5-20 ns per allocation (10-100x faster)
 * - Deterministic latency (every allocation takes same time)
 * - No heap fragmentation (fixed pre-allocated buffer)
 * - Cache friendly (contiguous memory layout)
 * 
 * WHAT THIS EXAMPLE SHOWS:
 * 1. Creating pools for primitive types (double) and structs (MyStruct)
 * 2. Allocating objects from pools
 * 3. Using allocated objects
 * 4. Deallocating objects back to pool for reuse
 * 5. Observing memory addresses (shows reuse pattern)
 * 
 * REAL-WORLD ANALOGY IN TRADING:
 * - prim_pool: Could be pool of prices, quantities, timestamps
 * - struct_pool: Could be pool of Orders, Trades, MarketUpdates
 * - Allocation: Creating new order, market data event
 * - Deallocation: Order filled/cancelled, returning to pool
 * 
 * KEY OBSERVATION:
 * Watch the memory addresses - deallocated objects get reused!
 * This demonstrates the pool's recycling behavior.
 */

#include "mem_pool.h"

/*
 * SAMPLE STRUCTURE
 * ================
 * Simple struct with 3 integers to demonstrate struct allocation.
 * 
 * In production trading systems, this would be:
 * - Order: {order_id, price, quantity}
 * - Trade: {trade_id, buy_order_id, sell_order_id}
 * - MarketUpdate: {ticker_id, bid_price, ask_price}
 * - PriceLevel: {price, total_quantity, num_orders}
 */
struct MyStruct {
  int d_[3];  // Three integer fields (conceptually: id, price, quantity)
};

/*
 * MAIN FUNCTION - MEMORY POOL DEMONSTRATION
 * ==========================================
 * 
 * FLOW:
 * 1. Create two memory pools (one for doubles, one for structs)
 * 2. Loop 50 times:
 *    a. Allocate from both pools
 *    b. Use the allocated objects
 *    c. Every 5th iteration: deallocate (return to pool for reuse)
 * 3. Observe memory addresses showing object reuse
 * 
 * KEY CONCEPTS DEMONSTRATED:
 * - Pool creation with fixed capacity
 * - Fast allocation (no malloc calls)
 * - In-place construction with placement new
 * - Deallocation (returning to pool, not actual free)
 * - Memory reuse pattern (same addresses reused)
 */
int main(int, char **) {
  using namespace Common;

  /*
   * STEP 1: CREATE MEMORY POOLS
   * ============================
   * Pre-allocate two pools: one for doubles, one for MyStruct objects.
   * 
   * Pool capacity: 50 elements each
   * - Allocates 50 * sizeof(double) + overhead for primitive pool
   * - Allocates 50 * sizeof(MyStruct) + overhead for struct pool
   * - This happens ONCE at initialization (not in hot path)
   * - No further heap allocations during operation
   */
  MemPool<double> prim_pool(50);     // Pool of 50 doubles (primitive type)
                                     // Size: 50 * (8 bytes + 1 byte flag + padding) ≈ 600-800 bytes
  
  MemPool<MyStruct> struct_pool(50); // Pool of 50 MyStruct objects (compound type)
                                     // Size: 50 * (12 bytes + 1 byte flag + padding) ≈ 800-1000 bytes
  
  // TOTAL MEMORY: ~1.5 KB pre-allocated (versus on-demand malloc for each allocation)
  // In production: pools typically 1K-100K objects (1-100 MB pre-allocated)

  /*
   * STEP 2: ALLOCATION AND DEALLOCATION LOOP
   * =========================================
   * Allocate objects in a loop, deallocating every 5th one to demonstrate reuse.
   */
  for(auto i = 0; i < 50; ++i) {  // Loop 50 times (will fill pool exactly)
    
    /*
     * ALLOCATE FROM PRIMITIVE POOL
     * ============================
     * Allocate a double from prim_pool and initialize it with value i.
     * 
     * What happens internally:
     * 1. prim_pool finds next free slot in its buffer
     * 2. Uses placement new to construct double with value i
     * 3. Marks slot as in-use
     * 4. Returns pointer to the double
     * 
     * Performance: ~5-20 nanoseconds (vs 100-500 ns for malloc)
     */
    auto p_ret = prim_pool.allocate(i);  // Allocate double initialized to value i
                                         // Returns: double* pointing to pooled memory
                                         // Type: auto deduces to double*
    
    /*
     * ALLOCATE FROM STRUCT POOL
     * =========================
     * Allocate a MyStruct from struct_pool and initialize with {i, i+1, i+2}.
     * 
     * What happens internally:
     * 1. struct_pool finds next free slot in its buffer
     * 2. Uses placement new with aggregate initialization: MyStruct{i, i+1, i+2}
     * 3. Marks slot as in-use
     * 4. Returns pointer to the MyStruct
     * 
     * Performance: ~5-20 nanoseconds (vs 100-500 ns for malloc)
     * Note: No heap allocation, no fragmentation, deterministic time
     */
    auto s_ret = struct_pool.allocate(MyStruct{i, i+1, i+2});  // Allocate MyStruct with values
                                                                // MyStruct{i, i+1, i+2} = aggregate initialization
                                                                // Returns: MyStruct* pointing to pooled memory
                                                                // Type: auto deduces to MyStruct*

    /*
     * USE THE ALLOCATED OBJECTS
     * =========================
     * Access and display the allocated data and memory addresses.
     */
    
    // Print primitive (double) value and its memory address
    std::cout << "prim elem:" << *p_ret << " allocated at:" << p_ret << std::endl;
    // *p_ret = dereference pointer to get double value
    // p_ret = memory address (pointer value)
    // Example output: "prim elem:5 allocated at:0x7ffeea3c5000"
    
    // Print struct fields and its memory address
    std::cout << "struct elem:" << s_ret->d_[0] << "," << s_ret->d_[1] << "," << s_ret->d_[2] 
              << " allocated at:" << s_ret << std::endl;
    // s_ret->d_[0] = access first field of struct via pointer
    // s_ret = memory address of struct
    // Example output: "struct elem:5,6,7 allocated at:0x7ffeea3c6000"

    /*
     * CONDITIONAL DEALLOCATION
     * ========================
     * Every 5th iteration (i=0,5,10,15,...), deallocate the objects back to the pool.
     * 
     * Purpose: Demonstrates object reuse pattern
     * - Deallocated objects returned to pool
     * - Next allocations can reuse these slots
     * - Watch memory addresses: deallocated addresses will be reused!
     * 
     * In production: Objects deallocated when:
     * - Order filled/cancelled (return Order object to pool)
     * - Market data processed (return MarketUpdate to pool)
     * - Trade completed (return Trade object to pool)
     */
    if(i % 5 == 0) {  // Modulo operator: true when i is divisible by 5 (0, 5, 10, 15, ...)
      
      // Log what we're about to deallocate
      std::cout << "deallocating prim elem:" << *p_ret << " from:" << p_ret << std::endl;
      // Shows: value being deallocated and its address
      
      std::cout << "deallocating struct elem:" << s_ret->d_[0] << "," << s_ret->d_[1] << "," 
                << s_ret->d_[2] << " from:" << s_ret << std::endl;
      // Shows: struct contents and its address

      /*
       * DEALLOCATE - RETURN TO POOL
       * ===========================
       * Mark these objects as free for reuse.
       * 
       * What happens internally:
       * 1. Pool validates pointer belongs to it
       * 2. Marks the slot as free
       * 3. Does NOT call destructor (object memory unchanged)
       * 4. Slot available for next allocate() call
       * 
       * Performance: ~2-10 nanoseconds (vs 50-200 ns for free())
       * 
       * CRITICAL: Object memory is NOT wiped
       * - Data still exists in memory
       * - Next allocate() will overwrite with new data
       * - For sensitive data, would need explicit zeroing
       */
      prim_pool.deallocate(p_ret);     // Return double to pool (mark slot as free)
                                       // Slot now available for reuse
                                       // p_ret pointer now INVALID (don't use it!)
      
      struct_pool.deallocate(s_ret);   // Return MyStruct to pool (mark slot as free)
                                       // Slot now available for reuse
                                       // s_ret pointer now INVALID (don't use it!)
      
      // OBSERVE: In next iterations, watch for these SAME ADDRESSES being allocated again!
      // This proves objects are being reused from the pool.
    }
  }
  
  // After loop: 40 objects still allocated (50 total, 10 deallocated)
  // If we allocated more, we'd run out of space (pool capacity = 50)
  // In production: would size pools based on expected peak usage

  return 0;  // Program exits (pools destroyed, memory freed back to OS)
}

/*
 * KEY TAKEAWAYS - MEMORY POOL USAGE
 * ==================================
 * 
 * 1. INITIALIZATION:
 *    - Create pool once at startup with fixed capacity
 *    - Size based on expected maximum concurrent objects
 *    - Pre-allocates all memory upfront (one-time cost)
 * 
 * 2. ALLOCATION:
 *    - allocate() returns pointer to pooled object
 *    - 5-20 ns vs 100-500 ns for malloc (10-100x faster)
 *    - Deterministic latency (same time every call)
 *    - No heap fragmentation
 * 
 * 3. USAGE:
 *    - Use returned pointer like any other pointer
 *    - Dereference with * or -> to access data
 *    - Object fully constructed and ready to use
 * 
 * 4. DEALLOCATION:
 *    - deallocate() returns object to pool
 *    - Does NOT call destructor
 *    - Pointer becomes invalid (don't use after!)
 *    - Slot available for next allocation
 * 
 * 5. MEMORY REUSE:
 *    - Deallocated slots reused by subsequent allocations
 *    - Watch addresses: same addresses appear multiple times
 *    - Zero system calls after initialization
 * 
 * 6. CAPACITY LIMITS:
 *    - Pool has fixed capacity (50 in this example)
 *    - Exceeding capacity causes assertion/crash
 *    - Must size appropriately for workload
 *    - Monitor usage in production
 * 
 * PRODUCTION USAGE PATTERNS:
 * 
 * Order Book Example:
 * ```cpp
 * MemPool<Order> order_pool(100000);        // 100K orders
 * MemPool<PriceLevel> level_pool(10000);    // 10K price levels
 * 
 * // Hot path: Add new order
 * Order* order = order_pool.allocate(order_id, price, qty);
 * order_book.add(order);
 * 
 * // Hot path: Order filled, return to pool
 * order_pool.deallocate(order);
 * ```
 * 
 * Market Data Example:
 * ```cpp
 * MemPool<MarketUpdate> md_pool(10000);     // 10K market updates
 * 
 * // Receive market data
 * auto* update = md_pool.allocate(ticker, bid, ask);
 * process_update(update);
 * md_pool.deallocate(update);               // Return to pool after processing
 * ```
 * 
 * PERFORMANCE COMPARISON:
 * Traditional malloc/new:
 * - Allocation: 100-500 ns (unpredictable)
 * - Deallocation: 50-200 ns (unpredictable)
 * - Fragmentation: Increases over time
 * - 1M allocations: 100-500 milliseconds
 * 
 * Memory pool:
 * - Allocation: 5-20 ns (deterministic)
 * - Deallocation: 2-10 ns (deterministic)
 * - Fragmentation: None (fixed buffer)
 * - 1M allocations: 5-20 milliseconds (20-100x faster!)
 * 
 * At HFT scale (millions of operations/second), memory pools are essential
 * for achieving sub-microsecond latency targets.
 */
