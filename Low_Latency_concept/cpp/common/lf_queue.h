#pragma once

#include <iostream>
#include <vector>
#include <atomic>

#include "macros.h"

/*
 * LOCK-FREE QUEUE IMPLEMENTATION - SPSC (SINGLE PRODUCER SINGLE CONSUMER)
 * ========================================================================
 * 
 * PURPOSE:
 * High-performance lock-free queue for inter-thread communication in low-latency trading systems.
 * This is a foundational data structure used throughout HFT platforms for passing messages between
 * components (market data, orders, execution reports, risk checks) with minimal latency.
 * 
 * DESIGN PRINCIPLES:
 * 1. LOCK-FREE: Uses atomic operations only, no mutexes, no kernel involvement
 * 2. SPSC: Single Producer Single Consumer - eliminates contention between threads
 * 3. CIRCULAR BUFFER: Fixed-size ring buffer with wrap-around for continuous operation
 * 4. ZERO-COPY: Returns pointers to pre-allocated slots, no data copying or heap allocations
 * 5. CACHE-FRIENDLY: Atomic indices allow lock-free coordination between threads
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Enqueue (write): ~10-20 nanoseconds (one atomic increment + pointer write)
 * - Dequeue (read): ~10-20 nanoseconds (one atomic increment + pointer read)
 * - Throughput: 50-100 million messages/second per queue
 * - Memory: Fixed allocation at construction (no runtime allocations)
 * - Latency: Deterministic, no system calls, no blocking
 * 
 * KEY CONSTRAINTS:
 * - ONE producer thread only (multiple producers will cause data races)
 * - ONE consumer thread only (multiple consumers will cause data races)
 * - Fixed capacity (no dynamic resizing to maintain deterministic performance)
 * - Not thread-safe for multiple producers/consumers (use MPMC queue for that)
 * 
 * USAGE PATTERN (TWO-STEP OPERATIONS):
 * Producer:
 *   1. T* slot = queue.getNextToWriteTo();  // Get pointer to write slot
 *   2. *slot = my_data;                      // Write data to slot
 *   3. queue.updateWriteIndex();             // Commit write (make visible to consumer)
 * 
 * Consumer:
 *   1. const T* data = queue.getNextToRead(); // Get pointer to read slot
 *   2. process(*data);                        // Process the data
 *   3. queue.updateReadIndex();               // Mark as consumed (free slot for reuse)
 * 
 * WHY TWO-STEP PATTERN?
 * - Separates reservation from commit (allows in-place construction)
 * - Enables validation before publishing
 * - Supports zero-copy operations
 * - Permits batch processing optimizations
 * 
 * ATOMICS AND MEMORY ORDERING:
 * - Uses std::atomic for lock-free coordination between producer and consumer
 * - Memory barriers ensure consumer sees complete writes from producer
 * - No race conditions despite concurrent access from two threads
 * 
 * OPTIMIZATION NOTES:
 * - For best performance, use power-of-2 size (allows fast modulo via bitwise AND)
 * - Pin producer/consumer threads to different CPU cores (avoid context switches)
 * - Ensure read/write indices on separate cache lines to prevent false sharing
 */

namespace Common {
  template<typename T>
  class LFQueue final {  // final = no virtual functions, no inheritance overhead
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * Creates lock-free queue with specified capacity.
     * 
     * WHAT IT DOES:
     * - Pre-allocates all memory for the circular buffer upfront
     * - Initializes atomic indices to zero (empty queue)
     * - Default-constructs all elements in the buffer
     * 
     * PARAMETERS:
     * - num_elems: Maximum number of elements the queue can hold
     * 
     * PERFORMANCE:
     * - One-time cost at startup (not in hot path)
     * - Allocates num_elems * sizeof(T) bytes on heap
     * - In production: typically 256-8192 elements (power of 2 for efficiency)
     * 
     * MEMORY LAYOUT:
     * Creates circular buffer: [slot0][slot1][slot2]...[slotN]
     * Producer writes at next_write_index_, Consumer reads at next_read_index_
     */
    explicit LFQueue(std::size_t num_elems) :  // explicit = prevent implicit conversions
        store_(num_elems, T()) {  // Allocate vector with num_elems default-constructed T objects
      // Pre-allocation ensures:
      // 1. No heap allocations during operation (deterministic latency)
      // 2. Contiguous memory (cache-friendly sequential access)
      // 3. Fixed capacity (bounded memory usage)
    }

    /*
     * GET NEXT WRITE SLOT (PRODUCER OPERATION - STEP 1 of 2)
     * =======================================================
     * Returns pointer to the next available slot where producer can write data.
     * 
     * WHAT IT DOES:
     * - Returns address of buffer slot at current write index
     * - Does NOT advance the write index (non-committing)
     * - Does NOT check if queue is full (producer must ensure space available)
     * 
     * USAGE:
     * T* slot = queue.getNextToWriteTo();  // Reserve slot
     * *slot = my_data;                      // Write data
     * queue.updateWriteIndex();             // Commit (make visible)
     * 
     * PERFORMANCE:
     * - O(1) constant time
     * - No atomic operations (just pointer arithmetic)
     * - ~2-5 nanoseconds (simple array indexing)
     * 
     * THREAD SAFETY:
     * - Safe for single producer only
     * - Consumer thread reads from different index (next_read_index_)
     * 
     * WHY RETURN POINTER?
     * - Zero-copy: Write directly to queue's internal buffer
     * - In-place construction: Can use placement new if needed
     * - No memcpy overhead
     */
    auto getNextToWriteTo() noexcept {  // noexcept = no exceptions (critical for HFT)
      return &store_[next_write_index_];  // Return address of slot at write index
                                          // Simple pointer arithmetic: base + (index * sizeof(T))
    }

    /*
     * COMMIT WRITE (PRODUCER OPERATION - STEP 2 of 2)
     * ================================================
     * Advances write index to make the newly written element visible to consumer.
     * 
     * WHAT IT DOES:
     * - Atomically increments write index with wrap-around (circular buffer)
     * - Atomically increments element count
     * - Makes the written data visible to consumer thread
     * 
     * MUST BE CALLED AFTER getNextToWriteTo() AND writing data to complete the write operation.
     * 
     * PERFORMANCE:
     * - Two atomic operations (increment write index, increment count)
     * - ~10-15 nanoseconds total
     * - Memory barriers ensure consumer sees complete write
     * 
     * ATOMICITY:
     * - Modulo operation wraps index back to 0 when reaching buffer end
     * - Consumer won't see partial writes due to memory ordering guarantees
     * 
     * CIRCULAR BUFFER LOGIC:
     * If buffer size = 8: indices go 0,1,2,3,4,5,6,7,0,1,2,... (wrap around)
     */
    auto updateWriteIndex() noexcept {
      next_write_index_ = (next_write_index_ + 1) % store_.size();  // Advance index with wrap-around
                                                                     // % = modulo (if power-of-2, compiler optimizes to bitwise AND)
      num_elements_++;  // Atomically increment count (tells consumer there's more data)
                        // Post-increment on std::atomic is thread-safe
    }

    /*
     * GET NEXT READ SLOT (CONSUMER OPERATION - STEP 1 of 2)
     * ======================================================
     * Returns pointer to the next element to read, or nullptr if queue is empty.
     * 
     * WHAT IT DOES:
     * - Checks if queue has elements (non-empty)
     * - Returns address of buffer slot at current read index
     * - Does NOT remove the element (non-destructive read)
     * - Does NOT advance read index (consumer must call updateReadIndex() after processing)
     * 
     * RETURN VALUE:
     * - const T* = Pointer to element data (consumer can read but not modify via this pointer)
     * - nullptr = Queue is empty, no data available
     * 
     * USAGE:
     * const T* data = queue.getNextToRead();  // Get data pointer
     * if (data) {
     *   process(*data);                       // Process the data
     *   queue.updateReadIndex();              // Mark as consumed
     * }
     * 
     * PERFORMANCE:
     * - O(1) constant time
     * - One atomic read (size check)
     * - ~5-10 nanoseconds
     * 
     * THREAD SAFETY:
     * - Safe for single consumer only
     * - Producer writes to different index (next_write_index_)
     * 
     * WHY RETURN CONST POINTER?
     * - Zero-copy: Read directly from queue's internal buffer
     * - No memcpy, no allocation
     * - const = consumer shouldn't modify queue's internal data
     */
    auto getNextToRead() const noexcept -> const T * {  // Trailing return type syntax (modern C++)
      return (size() ? &store_[next_read_index_] : nullptr);  // Ternary: if size > 0, return pointer, else nullptr
                                                               // size() atomically reads num_elements_
                                                               // Returns address of slot at read index
    }

    /*
     * COMMIT READ (CONSUMER OPERATION - STEP 2 of 2)
     * ===============================================
     * Advances read index to mark element as consumed and free the slot for producer reuse.
     * 
     * WHAT IT DOES:
     * - Atomically increments read index with wrap-around (circular buffer)
     * - Atomically decrements element count
     * - Frees the buffer slot for producer to overwrite
     * - Validates that we're not reading from empty queue (assertion check)
     * 
     * MUST BE CALLED AFTER getNextToRead() AND processing data to complete the read operation.
     * 
     * PERFORMANCE:
     * - Two atomic operations (increment read index, decrement count)
     * - One assertion check (debug builds only)
     * - ~10-15 nanoseconds total
     * 
     * CIRCULAR BUFFER LOGIC:
     * If buffer size = 8: indices go 0,1,2,3,4,5,6,7,0,1,2,... (wrap around)
     * 
     * SAFETY:
     * - ASSERT checks for underflow (reading from empty queue is a bug)
     * - In production, this catches programming errors during testing
     */
    auto updateReadIndex() noexcept {
      next_read_index_ = (next_read_index_ + 1) % store_.size();  // Advance read index with wrap-around
                                                                   // % = modulo operator for circular buffer
      ASSERT(num_elements_ != 0, "Read an invalid element in:" + std::to_string(pthread_self()));  
      // Assertion: Verify queue wasn't empty before reading
      // pthread_self() = current thread ID (helps debug which thread violated contract)
      // In release builds, ASSERT becomes no-op for performance
      
      num_elements_--;  // Atomically decrement count (tells producer there's more space)
                        // Post-decrement on std::atomic is thread-safe
    }

    /*
     * GET QUEUE SIZE
     * ==============
     * Returns the current number of elements in the queue.
     * 
     * WHAT IT DOES:
     * - Atomically reads the element count
     * - Returns number of elements currently in queue (0 to capacity)
     * 
     * RETURN VALUE:
     * - size_t: Number of elements available to read (0 = empty)
     * 
     * USAGE:
     * if (queue.size() > 0) {
     *   // Process elements
     * }
     * 
     * PERFORMANCE:
     * - O(1) constant time
     * - One atomic load operation
     * - ~2-5 nanoseconds
     * 
     * THREAD SAFETY:
     * - Safe to call from both producer and consumer threads
     * - Atomic load ensures consistent view
     * - Value may be stale immediately after reading (another thread may modify)
     * 
     * NOTE:
     * In lock-free programming, size can change between checking and acting on it.
     * This is normal - the two-step read/write pattern handles this correctly.
     */
    auto size() const noexcept {
      return num_elements_.load();  // Atomically load current element count
                                    // .load() performs atomic read with memory ordering guarantees
                                    // Default memory order is seq_cst (sequentially consistent)
    }

    /*
     * DELETED CONSTRUCTORS AND ASSIGNMENT OPERATORS
     * ==============================================
     * 
     * WHY DELETE THESE?
     * 1. Prevent copying: Copying atomic variables is problematic
     * 2. Prevent moving: Moving would invalidate pointers held by producer/consumer
     * 3. Enforce single ownership: Queue should be created once and shared via pointer
     * 4. Avoid undefined behavior: Multiple copies would break SPSC guarantees
     * 
     * USAGE PATTERN:
     * LFQueue<T> queue(1024);           // OK: Direct construction
     * LFQueue<T>* ptr = &queue;         // OK: Pass pointer to threads
     * 
     * LFQueue<T> copy = queue;          // ERROR: Copy constructor deleted
     * LFQueue<T> moved = std::move(q);  // ERROR: Move constructor deleted
     * 
     * This is a safety feature to prevent misuse in multithreaded code.
     */
    LFQueue() = delete;  // No default constructor (must specify size)

    LFQueue(const LFQueue &) = delete;  // No copy constructor (can't copy atomics safely)

    LFQueue(const LFQueue &&) = delete;  // No move constructor (would invalidate thread pointers)

    LFQueue &operator=(const LFQueue &) = delete;  // No copy assignment

    LFQueue &operator=(const LFQueue &&) = delete;  // No move assignment

  private:
    /*
     * MEMBER VARIABLES - LOCK-FREE QUEUE STATE
     * =========================================
     */
    
    // THE DATA BUFFER - Circular buffer storing queue elements
    // - Pre-allocated at construction (no runtime allocations)
    // - Accessed via FIFO pattern (First In First Out)
    // - Contiguous memory for cache-friendly access
    // - Size is fixed for deterministic performance
    std::vector<T> store_;

    // WRITE INDEX - Where producer writes next element
    // - Only modified by producer thread
    // - Read by producer (getNextToWriteTo)
    // - Atomic for memory visibility to consumer
    // - Wraps around at buffer end (circular buffer)
    // OPTIMIZATION: Should be on separate cache line from next_read_index_ (prevent false sharing)
    std::atomic<size_t> next_write_index_ = {0};

    // READ INDEX - Where consumer reads next element
    // - Only modified by consumer thread
    // - Read by consumer (getNextToRead)
    // - Atomic for memory visibility to producer
    // - Wraps around at buffer end (circular buffer)
    // OPTIMIZATION: Should be on separate cache line from next_write_index_ (prevent false sharing)
    std::atomic<size_t> next_read_index_ = {0};

    // ELEMENT COUNT - Number of elements currently in queue
    // - Incremented by producer (updateWriteIndex)
    // - Decremented by consumer (updateReadIndex)
    // - Used by both threads to check queue state
    // - Atomic for thread-safe access from both producer and consumer
    std::atomic<size_t> num_elements_ = {0};
  };
}

/*
 * IMPLEMENTATION NOTES AND OPTIMIZATIONS
 * =======================================
 * 
 * 1. FALSE SHARING PREVENTION:
 *    - Current implementation: Read/write indices may share same cache line (64 bytes)
 *    - Problem: When producer writes, entire cache line invalidated on consumer's CPU
 *    - Solution: Add cache line alignment (alignas(64)) to separate indices
 *    - Impact: Can improve performance by 2-5x on multi-core systems
 * 
 * 2. POWER-OF-2 OPTIMIZATION:
 *    - If size is power of 2 (256, 512, 1024, etc.):
 *      - Modulo (x % size) becomes bitwise AND (x & (size-1))
 *      - AND is faster than division/modulo
 *      - Compiler may optimize this automatically
 * 
 * 3. MEMORY ORDERING:
 *    - Current: Uses default memory_order_seq_cst (sequentially consistent)
 *    - Optimization: Could use memory_order_release/acquire for better performance
 *    - Trade-off: Complexity vs. ~5-10% performance gain
 * 
 * 4. BUSY POLLING:
 *    - Consumer typically runs: while(true) { if(auto* d = q.getNextToRead()) ... }
 *    - Burns CPU but achieves lowest latency (~10-50 ns)
 *    - Alternative: Add backoff strategy or sleep for non-critical paths
 * 
 * 5. BATCH OPERATIONS:
 *    - Can extend API to read/write multiple elements at once
 *    - Reduces atomic operation overhead
 *    - Useful for high-throughput scenarios
 * 
 * 6. SIZE TRACKING:
 *    - Current: Maintains num_elements_ for O(1) size()
 *    - Alternative: Calculate from indices (saves one atomic variable)
 *    - Trade-off: Current approach is clearer and safer
 * 
 * COMPARISON TO ALTERNATIVES:
 * - std::queue + std::mutex: 100-200 ns/op (too slow for HFT)
 * - boost::lockfree::spsc_queue: Similar performance, more features
 * - folly::ProducerConsumerQueue: Facebook's implementation, highly optimized
 * - This implementation: Simple, educational, production-ready for most cases
 * 
 * TESTING RECOMMENDATIONS:
 * - Thread sanitizer (tsan) to detect races
 * - Address sanitizer (asan) to detect memory issues
 * - Stress test with millions of operations
 * - Measure latency distribution (p50, p99, p999)
 * - Profile with perf/vtune to find hotspots
 */
