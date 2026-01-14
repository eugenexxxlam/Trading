/*
 * LOCK-FREE QUEUE EXAMPLE - SPSC PATTERN FOR HFT TRADING SYSTEMS
 * ================================================================
 * 
 * PURPOSE:
 * Demonstrates the Single-Producer Single-Consumer (SPSC) lock-free queue pattern,
 * which is the foundation of inter-thread communication in low-latency trading systems.
 * 
 * WHY LOCK-FREE FOR HFT:
 * - Mutex-based queues: 100-200 nanoseconds per operation (kernel involvement, unpredictable)
 * - Lock-free queues: 10-20 nanoseconds per operation (pure CPU atomics, deterministic)
 * - This 10x improvement directly translates to winning trades vs competitors
 * 
 * REAL-WORLD USAGE IN TRADING:
 * - Market data feed -> Strategy engine (tick updates, order book snapshots)
 * - Strategy engine -> Order gateway (new orders, cancels, modifications)
 * - Exchange gateway -> Risk manager (execution reports, fills)
 * - Matching engine -> Market data publisher (trade events, book changes)
 * 
 * KEY DESIGN PRINCIPLES:
 * 1. SPSC: Exactly ONE producer thread, ONE consumer thread (eliminates contention)
 * 2. Lock-free: Uses atomic operations, no mutexes, no blocking syscalls
 * 3. Zero-copy: Direct pointer access to pre-allocated memory, no data copying
 * 4. Pre-allocated: Fixed-size circular buffer, no heap allocations in hot path
 * 5. Cache-friendly: Separate cache lines for read/write indices (prevents false sharing)
 */

#include "thread_utils.h"
#include "lf_queue.h"

/*
 * DATA STRUCTURE
 * ==============
 * Simple struct with 3 integers to simulate trading messages.
 * In production systems, this would be:
 * - OrderRequest: {order_id, ticker_id, side, price, quantity}
 * - ExecutionReport: {order_id, fill_price, fill_qty, status}
 * - MarketUpdate: {ticker_id, bid_price, ask_price, bid_qty, ask_qty}
 */
struct MyStruct {
  int d_[3];  // d_[0]=order_id, d_[1]=price, d_[2]=quantity (conceptually)
};

using namespace Common;

/*
 * CONSUMER FUNCTION - THE READER THREAD
 * ======================================
 * 
 * WHAT IT DOES:
 * This function runs in a separate thread and continuously reads messages from the queue.
 * It represents components like: matching engine, trading strategy, risk manager, order gateway.
 * 
 * THE TWO-STEP READ PATTERN:
 * 1. getNextToRead() - Returns pointer to next message (without removing it)
 * 2. updateReadIndex() - Atomically advances the read index (marks message as consumed)
 * 
 * WHY TWO STEPS?
 * - Allows processing the message without holding any locks
 * - Enables retry logic if processing fails
 * - Permits batch processing optimizations
 * - Zero-copy: we read directly from the queue's internal buffer
 * 
 * PERFORMANCE:
 * In production, this loop processes each message in 100-500 nanoseconds total.
 */
auto consumeFunction(LFQueue<MyStruct>* lfq) {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(5s);  // Simulate startup delay - wait for producer to begin sending messages

  // Main consumer loop - keeps reading until queue is empty
  // In production: while(running_) with busy-polling for minimal latency
  while(lfq->size()) {  // Check if queue has any messages (lock-free atomic read)
    
    // STEP 1: Get pointer to the next message in the queue
    // - This is a ZERO-COPY operation (no memcpy, no allocation)
    // - Returns pointer directly into the queue's circular buffer
    // - Returns nullptr if queue is empty (won't happen here due to while condition)
    // - Does NOT remove the message yet (non-destructive read)
    const auto d = lfq->getNextToRead();
    
    // STEP 2: Mark this message as consumed
    // - Atomically increments the read index (std::atomic operation)
    // - Makes this buffer slot available for the producer to reuse
    // - Memory barriers ensure producer sees our read before overwriting
    // - This is when the message is "removed" from the queue
    lfq->updateReadIndex();

    // PROCESS THE MESSAGE
    // In production HFT systems, this is where we would:
    // - Match orders in the order book
    // - Execute trading strategy logic
    // - Send orders to exchange
    // - Update risk calculations
    // - Publish market data updates
    std::cout << "consumeFunction read elem:" << d->d_[0] << "," 
              << d->d_[1] << "," << d->d_[2] 
              << " lfq-size:" << lfq->size() << std::endl;  // Log message contents and current queue size

    std::this_thread::sleep_for(1s);  // Simulate processing time (in HFT: this would be nanoseconds, not seconds)
  }

  std::cout << "consumeFunction exiting." << std::endl;  // Consumer thread finished
}

/*
 * MAIN FUNCTION - THE PRODUCER THREAD
 * ====================================
 * 
 * WHAT IT DOES:
 * This is the producer thread that writes messages to the queue.
 * It represents components like: market data feed, trading strategy, exchange gateway.
 * 
 * THE TWO-STEP WRITE PATTERN:
 * 1. getNextToWriteTo() - Returns pointer to next available slot (without committing)
 * 2. updateWriteIndex() - Atomically advances write index (makes message visible)
 * 
 * WHY TWO STEPS?
 * - Allows constructing complex messages in-place (zero-copy)
 * - Enables atomic publication (consumer never sees partial writes)
 * - Permits validation before committing
 * - No heap allocations or memcpy operations
 * 
 * FLOW:
 * 1. Create lock-free queue
 * 2. Spawn consumer thread
 * 3. Produce 50 messages
 * 4. Wait for consumer to finish
 */
int main(int, char **) {
  
  // STEP 1: CREATE THE LOCK-FREE QUEUE
  // Allocate queue with capacity of 20 elements
  // - Pre-allocates all memory upfront (no allocations during operation)
  // - Creates circular buffer with 20 slots
  // - In production: typical sizes are 256, 512, 1024, 4096, 8192 (powers of 2)
  // - Power of 2 size allows fast modulo using bitwise AND (x % size becomes x & (size-1))
  LFQueue<MyStruct> lfq(20);

  // STEP 2: CREATE CONSUMER THREAD
  // Spawn a new thread that will read from the queue
  // Parameters:
  // - CPU affinity: -1 means "don't pin" (in production: pin to specific isolated core)
  // - Thread name: "" (in production: use descriptive names like "MD_Consumer", "OrderMatcher")
  // - Function: consumeFunction (the function to run in the thread)
  // - Argument: &lfq (pointer to the queue - shared between producer and consumer)
  // Production optimization: Pin consumer to dedicated CPU core (no context switches = consistent latency)
  auto ct = createAndStartThread(-1, "", consumeFunction, &lfq);

  // STEP 3: PRODUCER LOOP - SEND 50 MESSAGES
  // This loop simulates a stream of incoming messages (market data, orders, etc.)
  for(auto i = 0; i < 50; ++i) {  // Loop counter from 0 to 49
    
    // Create a message with sample data
    // In production: this would be OrderRequest{order_id=i, price=i*10, qty=i*100}
    const MyStruct d{i, i * 10, i * 100};  // Initialize struct with 3 values
    
    // TWO-STEP WRITE PATTERN BEGINS HERE
    
    // STEP 3a: Get pointer to the next available write slot
    // - Returns pointer directly into queue's circular buffer
    // - Points to pre-allocated memory (no heap allocation)
    // - This slot is "reserved" for us but not yet visible to consumer
    // - Zero-copy: we write directly to the queue's internal buffer
    *(lfq.getNextToWriteTo()) = d;  // Dereference pointer and copy our data into the slot
    
    // STEP 3b: Commit the write by advancing the write index
    // - Atomically increments write index (std::atomic operation)
    // - This makes the message visible to the consumer
    // - Memory barriers ensure consumer sees complete write (not partial data)
    // - Consumer can now read this message in getNextToRead()
    lfq.updateWriteIndex();

    // LOG THE WRITE OPERATION
    // Note: In production hot path, logging is done via separate async logger
    // (printf/cout involves system calls which take 1-10 microseconds - too slow!)
    std::cout << "main constructed elem:" << d.d_[0] << "," 
              << d.d_[1] << "," << d.d_[2] 
              << " lfq-size:" << lfq.size() << std::endl;  // Print message contents and queue size

    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);  // Slow down to observe behavior (HFT: microsecond intervals)
  }

  // STEP 4: GRACEFUL SHUTDOWN
  // Wait for consumer thread to finish processing all messages
  // - Consumer will exit when queue is empty (size() returns 0)
  // - join() blocks until consumer thread completes
  // - Ensures no messages are lost during shutdown
  ct->join();

  std::cout << "main exiting." << std::endl;  // Producer finished

  return 0;  // Program exits successfully
}

/*
 * KEY TAKEAWAYS - UNDERSTANDING LOCK-FREE QUEUES
 * ===============================================
 * 
 * 1. SPSC CONSTRAINT:
 *    - This design ONLY works with Single Producer + Single Consumer
 *    - Multiple producers OR consumers require different design (MPMC queue with CAS operations)
 *    - SPSC is optimal for point-to-point communication
 * 
 * 2. LOCK-FREE VS LOCK-BASED:
 *    - Lock-based (mutex): 100-200 ns, unpredictable, priority inversion, deadlock risk
 *    - Lock-free (atomics): 10-20 ns, deterministic, no contention, no deadlocks
 *    - 10x performance improvement is critical in HFT where nanoseconds = profit
 * 
 * 3. ZERO-COPY DESIGN:
 *    - No malloc/free in hot path (pre-allocated circular buffer)
 *    - No memcpy operations (direct pointer access)
 *    - No heap fragmentation (fixed size buffer)
 *    - Deterministic latency (every operation takes same time)
 * 
 * 4. MEMORY ORDERING:
 *    - Producer uses memory_order_release on write (ensures writes visible)
 *    - Consumer uses memory_order_acquire on read (ensures reads see all writes)
 *    - Prevents CPU/compiler reordering that could break lock-free guarantees
 * 
 * 5. FALSE SHARING PREVENTION:
 *    - Read and write indices should be on separate cache lines (64 bytes apart)
 *    - Prevents cache line ping-pong between producer/consumer CPU cores
 *    - Can improve performance by 2-5x in multi-core systems
 * 
 * 6. PRODUCTION OPTIMIZATIONS:
 *    - CPU pinning: Pin producer/consumer to specific cores (avoid context switches)
 *    - Power-of-2 size: Fast modulo using bitwise AND (x % size = x & (size-1))
 *    - Cache alignment: alignas(64) for atomic indices
 *    - Batch operations: Process multiple messages per iteration
 *    - Busy polling: while(true) loop with minimal sleep for lowest latency
 *    - Monitoring: Track queue depth, throughput, latency percentiles (p50, p99, p999)
 * 
 * PERFORMANCE NUMBERS (TYPICAL PRODUCTION SYSTEM):
 * - Enqueue latency: 10-20 nanoseconds
 * - Dequeue latency: 10-20 nanoseconds
 * - Throughput: 50-100 million messages/second per queue
 * - Latency jitter: <5 nanoseconds (very predictable)
 * - CPU usage: <1% when idle (efficient)
 * 
 * WHEN TO USE LOCK-FREE QUEUES:
 * - Hot path communication (order processing, market data)
 * - Latency-critical systems (HFT, low-latency trading)
 * - High throughput scenarios (millions of messages/second)
 * - Point-to-point communication (one sender, one receiver)
 * 
 * WHEN NOT TO USE:
 * - Multiple producers or consumers (use MPMC queue or locks)
 * - Cold path operations (configuration, logging, monitoring)
 * - Complex synchronization (use condition variables or semaphores)
 * - When simplicity matters more than performance
 */
