#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "macros.h"

/*
 * MEMORY POOL IMPLEMENTATION - OBJECT POOLING FOR LOW-LATENCY SYSTEMS
 * ====================================================================
 * 
 * PURPOSE:
 * Custom memory allocator that pre-allocates a pool of objects to eliminate heap allocations
 * in the hot path of trading systems. This is critical for achieving deterministic, low-latency
 * performance in HFT applications.
 * 
 * THE PROBLEM WITH HEAP ALLOCATION (malloc/new):
 * - Latency: 100-500+ nanoseconds (unpredictable, involves kernel, fragmentation)
 * - Non-deterministic: Allocation time varies based on heap state
 * - Fragmentation: Memory becomes fragmented over time
 * - Cache unfriendly: Allocated objects scattered across memory
 * - Lock contention: malloc/free use locks internally (multi-threaded slowdown)
 * 
 * THE MEMORY POOL SOLUTION:
 * - Pre-allocation: All memory allocated upfront at initialization
 * - O(1) allocation: Find next free slot (simple index lookup)
 * - O(1) deallocation: Mark slot as free (no actual free() call)
 * - Zero syscalls: No kernel involvement after initialization
 * - Cache friendly: Contiguous memory layout
 * - Deterministic: Every operation takes predictable time
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Allocation: ~5-20 nanoseconds (vs 100-500 ns for malloc)
 * - Deallocation: ~2-10 nanoseconds (vs 50-200 ns for free)
 * - Initialization: One-time cost at startup (pre-allocates all memory)
 * - Memory: Fixed pool size, no fragmentation
 * - Predictability: Constant-time operations (no variance)
 * 
 * REAL-WORLD USAGE IN TRADING SYSTEMS:
 * - Order objects: Pool of Order structs for matching engine
 * - Price level objects: Pool of PriceLevel nodes in order book
 * - Market data messages: Pool of MarketUpdate structs
 * - Risk check objects: Pool of RiskCheck structs
 * - Execution reports: Pool of ExecutionReport structs
 * 
 * KEY FEATURES:
 * 1. PLACEMENT NEW: Constructs objects in-place in pre-allocated memory
 * 2. REUSE: Returns objects to pool for reuse (no actual deallocation)
 * 3. TYPE-SAFE: Template-based, works with any type
 * 4. CACHE-FRIENDLY: Objects stored contiguously in vector
 * 5. BOUNDED: Fixed size prevents runaway memory usage
 * 
 * DESIGN TRADE-OFFS:
 * - Fixed capacity: Pool can be exhausted (must size appropriately)
 * - Memory overhead: bool flag per object (1 byte + padding)
 * - Linear search: Finding free slot is O(n) worst case (can optimize with free list)
 * - No shrinking: Memory never released back to OS (intentional for determinism)
 * 
 * USAGE PATTERN:
 * MemPool<Order> order_pool(10000);     // Pre-allocate 10,000 orders
 * Order* order = order_pool.allocate(order_id, price, qty);  // Fast allocation
 * // ... use order ...
 * order_pool.deallocate(order);         // Return to pool (no actual free)
 */

namespace Common {
  template<typename T>
  class MemPool final {  // final = no inheritance, enables compiler optimizations
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * Creates memory pool with specified capacity and pre-allocates all memory.
     * 
     * WHAT IT DOES:
     * - Allocates vector with num_elems ObjectBlock structs
     * - Each ObjectBlock contains: T object (default-constructed) + bool is_free flag (true)
     * - Validates memory layout (object must be first member for pointer arithmetic)
     * 
     * PARAMETERS:
     * - num_elems: Maximum number of objects the pool can hold
     * 
     * MEMORY LAYOUT:
     * [ObjectBlock0: {T obj, bool free=true}]
     * [ObjectBlock1: {T obj, bool free=true}]
     * ...
     * [ObjectBlockN: {T obj, bool free=true}]
     * 
     * PERFORMANCE:
     * - One-time cost at initialization (not in hot path)
     * - Allocates num_elems * sizeof(ObjectBlock) bytes
     * - In production: Pool sizes typically 1K-100K objects depending on usage
     * 
     * VALIDATION:
     * - Asserts that T object_ is first member of ObjectBlock
     * - This allows safe pointer casting: T* -> ObjectBlock* for deallocation
     */
    explicit MemPool(std::size_t num_elems) :  // explicit = prevent implicit conversions
        store_(num_elems, {T(), true}) {  // Initialize vector with num_elems copies of {default T, free=true}
                                          // {T(), true} uses aggregate initialization for ObjectBlock
                                          // All objects start as "free" and available for allocation
      
      // CRITICAL VALIDATION: Ensure memory layout allows pointer casting
      // We need to cast T* back to ObjectBlock* in deallocate()
      // This only works if T is the first member (no padding before it)
      ASSERT(reinterpret_cast<const ObjectBlock *>(&(store_[0].object_)) == &(store_[0]),  
             "T object should be first member of ObjectBlock.");
      // If this fails, memory layout is wrong and pointer arithmetic will break
      // &(store_[0].object_) = address of T within first ObjectBlock
      // &(store_[0]) = address of first ObjectBlock
      // These must be equal for our deallocate() pointer arithmetic to work
    }

    /*
     * ALLOCATE OBJECT FROM POOL
     * =========================
     * Allocates and constructs an object from the pool using placement new.
     * 
     * WHAT IT DOES:
     * 1. Gets pointer to next free ObjectBlock
     * 2. Validates it's actually free (assertion)
     * 3. Uses placement new to construct T in-place with provided arguments
     * 4. Marks block as in-use
     * 5. Updates next_free_index_ to point to next free slot
     * 6. Returns pointer to the constructed object
     * 
     * PARAMETERS:
     * - args: Variadic template arguments forwarded to T's constructor
     *         Example: allocate(order_id, price, qty) calls T(order_id, price, qty)
     * 
     * RETURN VALUE:
     * - T*: Pointer to newly allocated and constructed object
     * - Never returns nullptr (will assert/crash if pool exhausted)
     * 
     * PLACEMENT NEW EXPLANATION:
     * - new(ptr) T(args...): Constructs T at memory address ptr (doesn't allocate)
     * - Memory already allocated in constructor, just constructing object in-place
     * - Allows custom memory management while using C++ constructors
     * 
     * PERFORMANCE:
     * - O(1) when free block is at next_free_index_ (typical case)
     * - O(n) worst case if need to search for free block
     * - ~5-20 nanoseconds typical (vs 100-500 ns for malloc)
     * 
     * THREAD SAFETY:
     * - NOT thread-safe (single-threaded use only)
     * - For multi-threaded: need per-thread pools or lock-free free-list
     * 
     * ERROR HANDLING:
     * - ASSERT if block not free (programming error - double allocation)
     * - ASSERT if pool exhausted (need larger pool or investigate leak)
     */
    template<typename... Args>  // Variadic template: accepts any number of arguments of any types
    T *allocate(Args... args) noexcept {  // noexcept = critical for HFT (no exception overhead)
      
      // STEP 1: Get pointer to the ObjectBlock at next free index
      auto obj_block = &(store_[next_free_index_]);  // Address of ObjectBlock in vector
                                                      // next_free_index_ points to (likely) free slot
      
      // STEP 2: Validate this block is actually free
      ASSERT(obj_block->is_free_, "Expected free ObjectBlock at index:" + std::to_string(next_free_index_));
      // If this fails: programming error (allocated same block twice without deallocate)
      // or pool exhausted and wrapped around to in-use block
      
      // STEP 3: Get pointer to the T object within the ObjectBlock
      T *ret = &(obj_block->object_);  // Address of T member within ObjectBlock
                                       // This is the pointer we'll return to caller
      
      // STEP 4: Use PLACEMENT NEW to construct T in-place with forwarded arguments
      ret = new(ret) T(args...);  // Placement new syntax: new(address) Type(constructor_args)
                                  // Memory already exists (pre-allocated), just calling constructor
                                  // args... = parameter pack expansion, forwards all arguments to T's constructor
                                  // Example: allocate(1, 100, 50) calls T(1, 100, 50)
      
      // STEP 5: Mark this block as in-use
      obj_block->is_free_ = false;  // Now occupied, not available for allocation
                                    // Consumer must call deallocate() to return to pool

      // STEP 6: Find and update next free index
      updateNextFreeIndex();  // Search for next available free block (may wrap around)
                              // This prepares for next allocate() call

      // STEP 7: Return pointer to constructed object
      return ret;  // Caller now owns this object until they deallocate it
    }

    /*
     * DEALLOCATE OBJECT BACK TO POOL
     * ===============================
     * Returns object to pool by marking its block as free (does NOT call destructor).
     * 
     * WHAT IT DOES:
     * 1. Converts T* pointer back to ObjectBlock* using pointer arithmetic
     * 2. Validates pointer belongs to this pool
     * 3. Validates block is currently in-use (not already free)
     * 4. Marks block as free (available for reuse)
     * 
     * PARAMETERS:
     * - elem: Pointer to object previously allocated from this pool
     * 
     * CRITICAL NOTE - DESTRUCTOR NOT CALLED:
     * - This function does NOT call ~T() destructor
     * - Object memory is reused as-is in next allocate()
     * - Caller must ensure object is in valid state for reuse
     * - If T has resources (file handles, memory), caller must clean up manually
     * - For POD types (int, double, structs): this is fine
     * - For RAII types: may need explicit cleanup before deallocate()
     * 
     * POINTER ARITHMETIC EXPLANATION:
     * - elem points to T within an ObjectBlock
     * - Cast elem to ObjectBlock* to get containing block
     * - Subtract pool base address to get index
     * - This works because object_ is first member of ObjectBlock
     * 
     * PERFORMANCE:
     * - O(1) constant time
     * - ~2-10 nanoseconds (simple pointer arithmetic + flag update)
     * - Much faster than free() which may involve kernel
     * 
     * THREAD SAFETY:
     * - NOT thread-safe (single-threaded use only)
     * 
     * ERROR HANDLING:
     * - ASSERT if pointer doesn't belong to pool (programming error)
     * - ASSERT if block already free (double-free bug)
     */
    auto deallocate(const T *elem) noexcept {
      
      // STEP 1: Convert T* to ObjectBlock* using pointer casting
      const auto elem_index = (reinterpret_cast<const ObjectBlock *>(elem) - &store_[0]);
      // Explanation of pointer arithmetic:
      // - reinterpret_cast<const ObjectBlock *>(elem): Cast T* to ObjectBlock*
      //   This works because T is first member of ObjectBlock (validated in constructor)
      // - &store_[0]: Base address of vector (pointer to first ObjectBlock)
      // - Subtraction of two pointers gives array index (number of elements between them)
      // - Result: Index of this ObjectBlock in the vector
      
      // STEP 2: Validate pointer belongs to this pool
      ASSERT(elem_index >= 0 && static_cast<size_t>(elem_index) < store_.size(), 
             "Element being deallocated does not belong to this Memory pool.");
      // If this fails: pointer from different pool, stack, or invalid memory
      // elem_index < 0: pointer is before pool start (invalid)
      // elem_index >= size: pointer is after pool end (invalid)
      
      // STEP 3: Validate block is currently in-use (not already free)
      ASSERT(!store_[elem_index].is_free_, "Expected in-use ObjectBlock at index:" + std::to_string(elem_index));
      // If this fails: double-free bug (deallocated same object twice)
      // This is a programming error that would corrupt the pool state
      
      // STEP 4: Mark block as free (available for reuse)
      store_[elem_index].is_free_ = true;  // Now available for next allocate() call
                                           // Object memory still contains old data (not wiped)
                                           // Next allocate() will overwrite with placement new
    }

    /*
     * DELETED CONSTRUCTORS AND ASSIGNMENT OPERATORS
     * ==============================================
     * Prevents copying/moving pool which would invalidate pointers held by clients.
     * 
     * WHY DELETE THESE?
     * 1. Outstanding pointers: Clients hold T* pointers into pool's vector
     * 2. Copy would duplicate: But pointers still point to original, causing use-after-free
     * 3. Move would invalidate: Vector relocation would invalidate all outstanding pointers
     * 4. Enforce single ownership: Pool should be created once, passed by reference
     * 
     * USAGE:
     * MemPool<Order> pool(10000);         // OK: Create pool
     * Order* o1 = pool.allocate(...);     // OK: Allocate from pool
     * MemPool<Order> pool2 = pool;        // ERROR: Can't copy
     */
    MemPool() = delete;  // No default constructor (must specify size)

    MemPool(const MemPool &) = delete;  // No copy constructor

    MemPool(const MemPool &&) = delete;  // No move constructor

    MemPool &operator=(const MemPool &) = delete;  // No copy assignment

    MemPool &operator=(const MemPool &&) = delete;  // No move assignment

  private:
    /*
     * UPDATE NEXT FREE INDEX
     * ======================
     * Finds the next available free block in the pool (circular search with wrap-around).
     * 
     * WHAT IT DOES:
     * - Searches linearly from current next_free_index_
     * - Wraps around to beginning if reaches end
     * - Stops when finds free block
     * - Asserts if full loop without finding free block (pool exhausted)
     * 
     * ALGORITHM:
     * 1. Remember starting index
     * 2. While current block is not free:
     *    a. Advance to next index
     *    b. If reached end, wrap to beginning
     *    c. If back to start, pool is full (assert/crash)
     * 
     * PERFORMANCE:
     * - Best case: O(1) - next block is free (common case)
     * - Average case: O(k) - k occupied blocks before free one
     * - Worst case: O(n) - must search entire pool
     * - Typically fast when pool not near capacity
     * 
     * OPTIMIZATION OPPORTUNITY:
     * - Current: Linear search from next_free_index_
     * - Better: Maintain free-list (linked list of free blocks)
     * - Free-list: O(1) allocation always, small memory overhead
     * - Trade-off: Simplicity vs guaranteed O(1) performance
     * 
     * BRANCH PREDICTION:
     * - UNLIKELY hints help CPU predict common path (not wrapping, not full)
     * - Improves performance by reducing pipeline stalls
     */
    auto updateNextFreeIndex() noexcept {
      
      // STEP 1: Save starting index to detect full loop (pool exhausted)
      const auto initial_free_index = next_free_index_;  // Remember where we started
      
      // STEP 2: Linear search for next free block
      while (!store_[next_free_index_].is_free_) {  // While current block is occupied
        
        // STEP 3: Advance to next index
        ++next_free_index_;  // Move to next block
        
        // STEP 4: Wrap around if reached end (circular buffer behavior)
        if (UNLIKELY(next_free_index_ == store_.size())) {  // Reached past last element?
          // UNLIKELY hint: Usually not at end, helps branch predictor
          // Tells CPU: this branch rarely taken, optimize for the other path
          next_free_index_ = 0;  // Wrap to beginning
        }
        
        // STEP 5: Check if we've searched entire pool (exhausted)
        if (UNLIKELY(initial_free_index == next_free_index_)) {  // Back to start?
          // UNLIKELY hint: Pool rarely full, this should almost never trigger
          // If we've looped completely, no free blocks available
          ASSERT(initial_free_index != next_free_index_, "Memory Pool out of space.");
          // This assert will fail (initial == next is true)
          // Causes program termination with error message
          // In production: would log error and gracefully shutdown or increase pool size
        }
      }
      // Loop exits when store_[next_free_index_].is_free_ == true
      // next_free_index_ now points to available block for next allocate()
    }

    /*
     * OBJECT BLOCK STRUCTURE
     * ======================
     * Container for pooled object plus metadata (free flag).
     * 
     * LAYOUT:
     * struct ObjectBlock {
     *   T object_;       // The actual object (must be first member!)
     *   bool is_free_;   // Availability flag: true = available, false = in-use
     * };
     * 
     * WHY STRUCT-OF-ARRAYS VS ARRAY-OF-STRUCTS?
     * - Alternative: Separate vectors: vector<T> objects_, vector<bool> flags_
     * - Current approach: vector<ObjectBlock> combines them
     * - Benefit: Better cache locality (object + flag accessed together)
     * - Drawback: Slightly more memory (padding between T and bool)
     * 
     * MEMORY LAYOUT IMPORTANCE:
     * - T object_ MUST be first member (no padding before it)
     * - This allows pointer casting: T* -> ObjectBlock* in deallocate()
     * - If bool came first, pointer arithmetic would break
     * - Constructor validates this with assertion
     * 
     * CACHE PERFORMANCE:
     * - Accessing object and flag together is cache-friendly
     * - Both likely in same cache line (64 bytes)
     * - Reduces memory accesses vs separate arrays
     */
    struct ObjectBlock {
      T object_;             // The pooled object (MUST be first member for pointer casting)
      bool is_free_ = true;  // Availability flag: true = available for allocation, false = in-use
    };

    /*
     * MEMBER VARIABLES - POOL STATE
     * ==============================
     */
    
    // STORAGE: Vector of ObjectBlocks (each contains T + free flag)
    // - Pre-allocated in constructor (fixed size, no resizing)
    // - Contiguous memory (cache-friendly sequential access)
    // - Alternative: std::array for stack allocation (good for small pools)
    // - Why vector? Heap allocation scales better for large pools (10K+ objects)
    std::vector<ObjectBlock> store_;

    // NEXT FREE INDEX: Hint for where to find next available block
    // - Points to (likely) free block for fast allocation
    // - Updated by updateNextFreeIndex() after each allocate()
    // - Not guaranteed to be free (updateNextFreeIndex searches if not)
    // - Enables O(1) allocation in common case (next block is free)
    size_t next_free_index_ = 0;  // Start at index 0
  };
}

/*
 * MEMORY POOL OPTIMIZATIONS AND ALTERNATIVES
 * ===========================================
 * 
 * 1. FREE LIST OPTIMIZATION:
 *    Current: Linear search for free block (O(n) worst case)
 *    Better: Maintain linked list of free blocks
 *    - ObjectBlock adds: ObjectBlock* next_free_
 *    - Keep head pointer to first free block
 *    - allocate(): Pop from free list (O(1) always)
 *    - deallocate(): Push to free list (O(1) always)
 *    - Cost: Extra pointer per object (~8 bytes overhead)
 *    - Benefit: Guaranteed O(1) allocation, no search needed
 * 
 * 2. SLAB ALLOCATOR:
 *    - Allocate large "slabs" of memory
 *    - Each slab contains multiple objects
 *    - Reduces memory overhead vs per-object tracking
 *    - Used in kernel memory allocators
 * 
 * 3. THREAD-LOCAL POOLS:
 *    - Per-thread memory pools (no synchronization needed)
 *    - Each thread has dedicated pool
 *    - No contention, perfect for SPSC pattern
 *    - Matches threading model of trading systems
 * 
 * 4. OBJECT RECYCLING:
 *    - Current: deallocate() doesn't call destructor
 *    - Alternative: Call destructor, re-construct on allocate()
 *    - Trade-off: Cleaner semantics vs performance overhead
 *    - For POD types: current approach is fine
 *    - For complex objects: may want destructor calls
 * 
 * 5. POOL MONITORING:
 *    - Track allocation statistics (count, high-water mark)
 *    - Log warnings when pool nearing capacity
 *    - Helps tune pool sizes for production
 *    - Detect memory leaks (never-deallocated objects)
 * 
 * COMPARISON TO ALTERNATIVES:
 * - malloc/new: 100-500 ns, unpredictable, fragmentation
 * - tcmalloc/jemalloc: 20-50 ns, better but still not deterministic
 * - Custom pool (this): 5-20 ns, deterministic, no fragmentation
 * - boost::pool: Similar performance, more features, larger codebase
 * - folly::ThreadCachedArena: Facebook's implementation, very fast
 * 
 * WHEN TO USE MEMORY POOLS:
 * - Hot path allocations (order book, market data)
 * - Fixed-size objects (Order, PriceLevel, MarketUpdate)
 * - Known maximum count (can size pool appropriately)
 * - Latency-critical code (every nanosecond matters)
 * 
 * WHEN NOT TO USE:
 * - Variable-size objects (use custom allocators)
 * - Unknown/unbounded allocation count (risk exhaustion)
 * - Cold path (initialization, config) where malloc is fine
 * - Objects requiring destructor calls for cleanup
 */
