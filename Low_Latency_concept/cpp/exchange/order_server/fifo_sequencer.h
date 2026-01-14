#pragma once

#include "common/thread_utils.h"
#include "common/macros.h"

#include "order_server/client_request.h"

/*
 * FIFO SEQUENCER - TIME-PRIORITY ORDER SEQUENCING
 * ================================================
 * 
 * PURPOSE:
 * Ensures orders are processed by the matching engine in strict timestamp order,
 * guaranteeing time-priority fairness across all TCP connections.
 * 
 * PROBLEM:
 * Order server accepts orders from multiple TCP connections (one per client).
 * Without sequencing, processing order is non-deterministic (depends on thread scheduling).
 * This violates time-priority principle: orders should be matched by arrival time.
 * 
 * SOLUTION:
 * 1. Batch orders from all connections (addClientRequest)
 * 2. Sort by receive timestamp (sequenceAndPublish)
 * 3. Publish to matching engine in timestamp order
 * 
 * EXAMPLE:
 * ```
 * Connection 1: Order A (timestamp 1000 ns)
 * Connection 2: Order B (timestamp 999 ns)
 * Connection 1: Order C (timestamp 1001 ns)
 * 
 * Without sequencing: Process A, B, C (connection order)
 * With sequencing: Process B (999), A (1000), C (1001) 
 * ```
 * 
 * FAIRNESS:
 * - All clients treated equally
 * - Earlier orders get priority
 * - Prevents connection-based advantage
 * - Regulatory requirement (MiFID II, Reg NMS)
 * 
 * BATCHING:
 * - Accumulate orders for 10-50 μs
 * - Sort once (O(N log N))
 * - Publish sorted batch
 * - Trade-off: Slightly higher latency for fairness
 * 
 * PERFORMANCE:
 * - Batch size: 1-100 orders (typical)
 * - Sort time: 0.1-1 μs (small batch)
 * - Added latency: 10-50 μs (batching window)
 * - Acceptable: Fairness more important than nanosecond latency
 * 
 * ALTERNATIVE: Lock-step sequencing (no batching)
 * - Process one order at a time in strict timestamp order
 * - Lower latency but lower throughput
 * - More complex (requires priority queue)
 */

namespace Exchange {
  // Maximum number of pending orders across all connections
  // Typical: 10-100 orders per batch
  // Conservative: 1024 (handle burst traffic)
  constexpr size_t ME_MAX_PENDING_REQUESTS = 1024;

  class FIFOSequencer {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - client_requests: Lock-free queue to matching engine
     * - logger: Async logger for debugging
     * 
     * Initializes pending request buffer (fixed-size array).
     */
    FIFOSequencer(ClientRequestLFQueue *client_requests, Logger *logger)
        : incoming_requests_(client_requests), logger_(logger) {
    }

    ~FIFOSequencer() {
    }

    /*
     * ADD CLIENT REQUEST - BATCH ACCUMULATION
     * ========================================
     * 
     * Called by order server when TCP receives an order.
     * Adds to pending buffer WITHOUT sorting yet (wait for batch to fill).
     * 
     * Parameters:
     * - rx_time: Hardware/software timestamp (nanoseconds)
     * - request: Client order (NEW or CANCEL)
     * 
     * TIMESTAMP SOURCE:
     * - Ideal: Hardware timestamp (NIC timestamp)
     * - Fallback: Software timestamp (kernel or user space)
     * - Consistency: Same clock source for all connections
     * 
     * OVERFLOW:
     * - Fatal if buffer full (1024 orders)
     * - Rare: Would need 1024 simultaneous orders
     * - Production: Reject new orders, return error to client
     * 
     * THREAD SAFETY:
     * - Called from order server thread (single producer)
     * - No locking needed (single-threaded access)
     */
    auto addClientRequest(Nanos rx_time, const MEClientRequest &request) {
      // Check buffer capacity (should never overflow in practice)
      if (pending_size_ >= pending_client_requests_.size()) {
        FATAL("Too many pending requests");  // Abort if overflow
      }
      
      // Add to pending buffer (not sorted yet)
      // pending_size_ tracks how many pending
      pending_client_requests_.at(pending_size_++) = std::move(RecvTimeClientRequest{rx_time, request});
    }

    /*
     * SEQUENCE AND PUBLISH - TIME-PRIORITY SORTING
     * =============================================
     * 
     * Called periodically (e.g., every 10-50 μs) to flush pending batch.
     * Sorts by timestamp and publishes to matching engine.
     * 
     * STEPS:
     * 1. Check if any pending orders (skip if empty)
     * 2. Sort by rx_time ascending (earliest first)
     * 3. Write to lock-free queue (to matching engine)
     * 4. Clear pending buffer
     * 
     * SORT ALGORITHM:
     * - std::sort: O(N log N) comparison sort
     * - Small N (10-100): ~100-500 ns
     * - operator< in RecvTimeClientRequest compares timestamps
     * 
     * TIMING:
     * - FIFO batch window: 10-50 μs
     * - Sort time: 0.1-1 μs
     * - Queue write: 0.01-0.02 μs per order
     * - Total overhead: ~10-50 μs (dominated by batching window)
     * 
     * TTT_MEASURE:
     * - Performance measurement macro (T2 = Order server -> Matching engine)
     * - Records timestamp for latency analysis
     */
    auto sequenceAndPublish() {
      // Early exit if no pending orders (common case)
      if (UNLIKELY(!pending_size_))  // UNLIKELY = branch prediction hint
        return;

      // Log batch processing (debug builds)
      logger_->log("%:% %() % Processing % requests.\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_), pending_size_);

      // Sort pending orders by receive timestamp (ascending)
      // Uses operator< defined in RecvTimeClientRequest
      // O(N log N) where N = pending_size_ (typically 10-100)
      std::sort(pending_client_requests_.begin(), pending_client_requests_.begin() + pending_size_);

      // Publish sorted orders to matching engine
      for (size_t i = 0; i < pending_size_; ++i) {
        const auto &client_request = pending_client_requests_.at(i);  // Get next order

        // Log order being published (with timestamp)
        logger_->log("%:% %() % Writing RX:% Req:% to FIFO.\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                     client_request.recv_time_, client_request.request_.toString());

        // Write to lock-free queue (two-step: get slot, update index)
        auto next_write = incoming_requests_->getNextToWriteTo();  // Reserve slot
        *next_write = std::move(client_request.request_);          // Write order
        incoming_requests_->updateWriteIndex();                     // Commit (publish to consumer)
        
        // Performance measurement (T2 = order server output)
        TTT_MEASURE(T2_OrderServer_LFQueue_write, (*logger_));
      }

      // Reset pending buffer (ready for next batch)
      pending_size_ = 0;
    }

    // Deleted constructors (prevent accidental copies)
    FIFOSequencer() = delete;
    FIFOSequencer(const FIFOSequencer &) = delete;
    FIFOSequencer(const FIFOSequencer &&) = delete;
    FIFOSequencer &operator=(const FIFOSequencer &) = delete;
    FIFOSequencer &operator=(const FIFOSequencer &&) = delete;

  private:
    // Lock-free queue to matching engine (output)
    ClientRequestLFQueue *incoming_requests_ = nullptr;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (off critical path)
    Logger *logger_ = nullptr;

    /*
     * RECEIVE TIME + CLIENT REQUEST PAIR
     * ===================================
     * 
     * Stores timestamp and order together for sorting.
     * 
     * Fields:
     * - recv_time_: When order arrived (nanoseconds)
     * - request_: Actual order (NEW/CANCEL)
     * 
     * operator<:
     * - Comparison for std::sort
     * - Sorts by recv_time_ ascending (earliest first)
     * - Guarantees time-priority fairness
     */
    struct RecvTimeClientRequest {
      Nanos recv_time_ = 0;        // Receive timestamp (ns)
      MEClientRequest request_;    // Client order

      // Comparison operator (for std::sort)
      // Returns true if this order is earlier than rhs
      auto operator<(const RecvTimeClientRequest &rhs) const {
        return (recv_time_ < rhs.recv_time_);
      }
    };

    // Pending request buffer (fixed-size array for cache locality)
    // std::array: Stack-allocated, no heap, no indirection
    std::array<RecvTimeClientRequest, ME_MAX_PENDING_REQUESTS> pending_client_requests_;
    
    // Number of pending orders (0 to ME_MAX_PENDING_REQUESTS)
    size_t pending_size_ = 0;
  };
}

/*
 * FIFO SEQUENCER DESIGN CONSIDERATIONS
 * =====================================
 * 
 * 1. TIMESTAMP SOURCE:
 *    - Hardware (NIC): Most accurate, requires driver support
 *    - Kernel (SO_TIMESTAMP): Good, ~1 μs precision
 *    - User space (rdtsc): Fast, requires calibration
 *    - Choice: Trade-off between accuracy and overhead
 * 
 * 2. BATCHING WINDOW:
 *    - Smaller window: Lower latency, more frequent sorts
 *    - Larger window: Higher throughput, higher latency
 *    - Typical: 10-50 μs (balance fairness and latency)
 *    - Adaptive: Adjust based on load
 * 
 * 3. FAIRNESS vs LATENCY:
 *    - No sequencing: Lowest latency, unfair
 *    - Per-order sequencing: Fair, lowest throughput
 *    - Batch sequencing: Balance (this implementation)
 *    - Acceptable: 10-50 μs added latency for fairness
 * 
 * 4. PRODUCTION ENHANCEMENTS:
 *    - Priority queue: O(log N) insert, O(1) get-min
 *    - Bounded latency: Process after X μs or N orders
 *    - Per-instrument sequencing: Higher parallelism
 *    - Lockstep mode: Strict order, no batching
 * 
 * 5. REGULATORY REQUIREMENTS:
 *    - MiFID II: Time-priority must be enforced
 *    - Reg NMS: Order protection rule
 *    - Audit trail: Timestamp every order (microsecond)
 *    - Proof: Must demonstrate fair sequencing
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Batch size (N): 1-100 orders
 * - Sort time: O(N log N) = 0.1-1 μs
 * - Queue write: O(N) = N × 10-20 ns
 * - Total: 0.5-2 μs (sort + write)
 * - Amortized per order: 5-20 ns
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Priority Queue (per-order):
 *    - Insert: O(log N) into min-heap
 *    - Extract: O(log N) get minimum
 *    - Advantage: Lower latency (no batching)
 *    - Disadvantage: More complex, more overhead
 * 
 * B) Lock-step Processing:
 *    - Global sequence number per order
 *    - Process in strict sequence order
 *    - Advantage: Strictest fairness
 *    - Disadvantage: Serialization bottleneck
 * 
 * C) Per-instrument Sequencing:
 *    - Separate sequencer per ticker
 *    - Parallel processing (different instruments)
 *    - Advantage: Higher throughput
 *    - Disadvantage: Cross-instrument order not guaranteed
 * 
 * CHOSEN DESIGN: Batch sequencing
 * - Simple implementation
 * - Good balance (fairness and latency)
 * - Sufficient for most use cases
 * - Production-proven approach
 */
