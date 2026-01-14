#pragma once

#include <array>
#include <sstream>
#include "common/types.h"

using namespace Common;

/*
 * MATCHING ENGINE ORDER STRUCTURES - ORDER BOOK DATA STRUCTURES
 * ==============================================================
 * 
 * PURPOSE:
 * Defines the core data structures for the matching engine's limit order book:
 * - MEOrder: Individual order (node in FIFO queue at a price level)
 * - MEOrdersAtPrice: Price level (contains FIFO queue of orders)
 * 
 * ORDER BOOK STRUCTURE:
 * ```
 * BIDS (descending price):        ASKS (ascending price):
 * $150.00 (best bid)              $150.05 (best ask)
 *   ├─ Order 1 (100 shares)        ├─ Order 5 (50 shares)
 *   ├─ Order 2 (200 shares)        └─ Order 6 (150 shares)
 *   └─ Order 3 (50 shares)       $150.10
 * $149.95                           └─ Order 7 (100 shares)
 *   └─ Order 4 (300 shares)
 * ```
 * 
 * TIME-PRICE PRIORITY:
 * 1. Price: Best price first (highest bid, lowest ask)
 * 2. Time: Within price, FIFO (first order at price gets filled first)
 * 
 * DATA STRUCTURE CHOICE:
 * - Doubly-linked lists (not std::vector, not std::list)
 * - Custom memory pool (not heap allocation)
 * - Hash maps for O(1) lookup (not std::unordered_map)
 * 
 * WHY DOUBLY-LINKED LISTS?
 * - O(1) insert at end (new order at price)
 * - O(1) delete anywhere (cancel order)
 * - O(1) traverse (match orders)
 * - Cache-friendly (pool allocation, sequential access)
 * - No reallocation (unlike std::vector)
 * 
 * WHY NOT STD CONTAINERS?
 * - std::list: Heap allocation per node (slow, non-deterministic)
 * - std::vector: Reallocation (unpredictable latency spikes)
 * - std::unordered_map: Hash collisions, heap allocation
 * - Custom: Predictable, fast, zero-allocation after init
 * 
 * PERFORMANCE:
 * - Add order: O(1) - 20-50 ns
 * - Cancel order: O(1) - 20-50 ns
 * - Match order: O(1) per matched order
 * - Memory: Pre-allocated pool (no runtime allocation)
 */

namespace Exchange {
  /*
   * ME ORDER - INDIVIDUAL ORDER IN ORDER BOOK
   * ==========================================
   * 
   * Represents a single limit order in the matching engine.
   * Also serves as a node in a doubly-linked list (FIFO queue at price level).
   * 
   * FIELDS:
   * 
   * Order Identity:
   * - ticker_id_: Which instrument (0=AAPL, 1=MSFT, etc.)
   * - client_id_: Which client submitted this order
   * - client_order_id_: Client's order identifier
   * - market_order_id_: Exchange's unique identifier
   * 
   * Order Details:
   * - side_: BUY or SELL
   * - price_: Limit price (fixed-point integer)
   * - qty_: Remaining quantity (decreases on partial fills)
   * - priority_: Time priority (lower = earlier, FIFO ordering)
   * 
   * Linked List Pointers (for FIFO queue at price level):
   * - prev_order_: Previous order in FIFO queue (circular)
   * - next_order_: Next order in FIFO queue (circular)
   * 
   * CIRCULAR DOUBLY-LINKED LIST:
   * - Empty: nullptr
   * - Single order: prev = next = self
   * - Multiple orders: prev/next form circular chain
   * 
   * Example (3 orders at $150.00):
   * ```
   * Order A (first) <-> Order B <-> Order C (last) <-> Order A (wraps)
   * prev_order_: C      A               B               C
   * next_order_: B      C               A               A
   * ```
   * 
   * PRIORITY:
   * - Monotonically increasing (1, 2, 3, ...)
   * - Assigned at price level (see getNextPriority in order book)
   * - Used for time-priority matching (FIFO)
   * - Lower priority = earlier order (filled first)
   * 
   * SIZE: ~64 bytes
   * - 8 uint64_t / 4-byte fields: ~40 bytes
   * - 2 pointers: 16 bytes (64-bit)
   * - Padding: ~8 bytes
   * - Cache-friendly: Fits in 1 cache line (64 bytes)
   * 
   * MEMORY POOL:
   * - Allocated from MemPool<MEOrder>
   * - No heap allocation (predictable latency)
   * - Deallocated back to pool (reused)
   * - See common/mem_pool.h for details
   */
  struct MEOrder {
    // Order identity
    TickerId ticker_id_ = TickerId_INVALID;           // Instrument ID
    ClientId client_id_ = ClientId_INVALID;           // Client ID
    OrderId client_order_id_ = OrderId_INVALID;       // Client's order ID
    OrderId market_order_id_ = OrderId_INVALID;       // Exchange's order ID
    
    // Order details
    Side side_ = Side::INVALID;                       // BUY or SELL
    Price price_ = Price_INVALID;                     // Limit price
    Qty qty_ = Qty_INVALID;                           // Remaining quantity
    Priority priority_ = Priority_INVALID;            // Time priority (FIFO)

    // Doubly-linked list pointers (circular)
    // Form FIFO queue at price level
    MEOrder *prev_order_ = nullptr;  // Previous order in queue (or last if first)
    MEOrder *next_order_ = nullptr;  // Next order in queue (or first if last)

    // Default constructor (required for MemPool)
    MEOrder() = default;

    // Full constructor (used when creating new order)
    MEOrder(TickerId ticker_id, ClientId client_id, OrderId client_order_id, OrderId market_order_id, Side side, Price price,
                     Qty qty, Priority priority, MEOrder *prev_order, MEOrder *next_order) noexcept
        : ticker_id_(ticker_id), client_id_(client_id), client_order_id_(client_order_id), market_order_id_(market_order_id), side_(side),
          price_(price), qty_(qty), priority_(priority), prev_order_(prev_order), next_order_(next_order) {}

    // String conversion (implemented in me_order.cpp)
    auto toString() const -> std::string;
  };

  /*
   * ORDER HASH MAP - FAST ORDER LOOKUP
   * ===================================
   * 
   * Fast lookup: OrderId -> MEOrder*
   * Used by matching engine to find orders quickly (for cancels, fills).
   * 
   * TYPE: std::array<MEOrder*, ME_MAX_ORDER_IDS>
   * - Fixed-size array (no heap allocation)
   * - Direct indexing: O(1) lookup
   * - order_id used as array index
   * 
   * SIZE: ME_MAX_ORDER_IDS = 1M (typical)
   * - Array size: 1M * 8 bytes (pointer) = 8 MB
   * - Memory overhead acceptable for O(1) lookup
   * 
   * WHY NOT std::unordered_map?
   * - Hash collisions (unpredictable performance)
   * - Heap allocation (non-deterministic latency)
   * - Cache misses (scattered memory)
   * - std::array: Predictable, fast, zero runtime allocation
   * 
   * USAGE:
   * ```cpp
   * OrderHashMap orders;
   * orders[order_id] = &order;  // Insert: O(1)
   * MEOrder* order = orders[order_id];  // Lookup: O(1)
   * orders[order_id] = nullptr;  // Delete: O(1)
   * ```
   */
  typedef std::array<MEOrder *, ME_MAX_ORDER_IDS> OrderHashMap;

  /*
   * CLIENT ORDER HASH MAP - PER-CLIENT ORDER LOOKUP
   * ================================================
   * 
   * Two-level lookup: ClientId -> OrderId -> MEOrder*
   * Allows fast lookup of orders by client and order ID.
   * 
   * TYPE: std::array<OrderHashMap, ME_MAX_NUM_CLIENTS>
   * - Outer array: Indexed by client_id
   * - Inner array: Indexed by order_id
   * 
   * USAGE:
   * ```cpp
   * ClientOrderHashMap client_orders;
   * client_orders[client_id][order_id] = &order;  // Insert
   * MEOrder* order = client_orders[client_id][order_id];  // Lookup
   * ```
   * 
   * WHY PER-CLIENT?
   * - Order IDs unique per client (not globally)
   * - Client 1 order ID 100 ≠ Client 2 order ID 100
   * - Two-level indexing prevents collisions
   * 
   * SIZE:
   * - ME_MAX_NUM_CLIENTS = 256 (typical)
   * - ME_MAX_ORDER_IDS = 1M
   * - Total: 256 * 8 MB = 2 GB (large but manageable)
   * - Production: Use sparse structures or smaller limits
   */
  typedef std::array<OrderHashMap, ME_MAX_NUM_CLIENTS> ClientOrderHashMap;

  /*
   * ME ORDERS AT PRICE - PRICE LEVEL IN ORDER BOOK
   * ===============================================
   * 
   * Represents all orders at a single price level.
   * Contains a FIFO queue of MEOrder objects (doubly-linked list).
   * Also serves as a node in a doubly-linked list of price levels.
   * 
   * FIELDS:
   * 
   * Price Level Identity:
   * - side_: BUY or SELL
   * - price_: Price level (e.g., $150.00)
   * 
   * Orders at Price:
   * - first_me_order_: Head of FIFO queue (earliest order)
   * 
   * Linked List Pointers (for price level list):
   * - prev_entry_: More aggressive price (circular)
   * - next_entry_: Less aggressive price (circular)
   * 
   * PRICE LEVEL ORDERING:
   * 
   * BID SIDE (descending price):
   * ```
   * $150.00 (best bid, first_entry_) -> $149.95 -> $149.90 -> ...
   * ```
   * 
   * ASK SIDE (ascending price):
   * ```
   * $150.05 (best ask, first_entry_) -> $150.10 -> $150.15 -> ...
   * ```
   * 
   * CIRCULAR DOUBLY-LINKED LIST:
   * - prev_entry_ points to previous price level
   * - next_entry_ points to next price level
   * - Last entry's next_entry_ wraps to first entry
   * 
   * Example (3 bid price levels):
   * ```
   * $150.00 <-> $149.95 <-> $149.90 <-> $150.00 (wraps)
   * ```
   * 
   * FIFO QUEUE (first_me_order_):
   * - Points to first order at this price
   * - Orders form circular doubly-linked list
   * - See MEOrder for details
   * 
   * SIZE: ~40 bytes
   * - side: 1 byte
   * - price: 8 bytes
   * - first_me_order: 8 bytes (pointer)
   * - prev_entry: 8 bytes (pointer)
   * - next_entry: 8 bytes (pointer)
   * - padding: ~7 bytes
   * 
   * MEMORY POOL:
   * - Allocated from MemPool<MEOrdersAtPrice>
   * - No heap allocation
   * - Reused when price level becomes empty
   */
  struct MEOrdersAtPrice {
    // Price level identity
    Side side_ = Side::INVALID;     // BUY or SELL
    Price price_ = Price_INVALID;   // Price level

    // Head of FIFO queue of orders at this price
    MEOrder *first_me_order_ = nullptr;

    // Doubly-linked list of price levels (circular)
    // Ordered by price (most aggressive first)
    MEOrdersAtPrice *prev_entry_ = nullptr;  // More aggressive price
    MEOrdersAtPrice *next_entry_ = nullptr;  // Less aggressive price

    // Default constructor (required for MemPool)
    MEOrdersAtPrice() = default;

    // Full constructor (used when creating new price level)
    MEOrdersAtPrice(Side side, Price price, MEOrder *first_me_order, MEOrdersAtPrice *prev_entry, MEOrdersAtPrice *next_entry)
        : side_(side), price_(price), first_me_order_(first_me_order), prev_entry_(prev_entry), next_entry_(next_entry) {}

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "MEOrdersAtPrice["
         << "side:" << sideToString(side_) << " "
         << "price:" << priceToString(price_) << " "
         << "first_me_order:" << (first_me_order_ ? first_me_order_->toString() : "null") << " "
         << "prev:" << priceToString(prev_entry_ ? prev_entry_->price_ : Price_INVALID) << " "
         << "next:" << priceToString(next_entry_ ? next_entry_->price_ : Price_INVALID) << "]";

      return ss.str();
    }
  };

  /*
   * ORDERS AT PRICE HASH MAP - FAST PRICE LEVEL LOOKUP
   * ===================================================
   * 
   * Fast lookup: Price -> MEOrdersAtPrice*
   * Used by matching engine to find price levels quickly.
   * 
   * TYPE: std::array<MEOrdersAtPrice*, ME_MAX_PRICE_LEVELS>
   * - Fixed-size array (no heap allocation)
   * - Direct indexing: price % ME_MAX_PRICE_LEVELS
   * - O(1) lookup
   * 
   * HASH FUNCTION:
   * - price % ME_MAX_PRICE_LEVELS
   * - Simple modulo (fast)
   * - Collisions possible but rare (wide price range)
   * 
   * SIZE: ME_MAX_PRICE_LEVELS = 256K (typical)
   * - Array size: 256K * 8 bytes = 2 MB
   * - Acceptable memory overhead for O(1) lookup
   * 
   * COLLISION HANDLING:
   * - Rare: Requires two active price levels with same hash
   * - Mitigation: Large ME_MAX_PRICE_LEVELS (256K)
   * - Production: Could use open addressing or chaining
   */
  typedef std::array<MEOrdersAtPrice *, ME_MAX_PRICE_LEVELS> OrdersAtPriceHashMap;
}

/*
 * ORDER BOOK DATA STRUCTURE DESIGN RATIONALE
 * ===========================================
 * 
 * 1. DOUBLY-LINKED LISTS vs ALTERNATIVES:
 * 
 *    Doubly-Linked List (CHOSEN):
 *    - Insert: O(1) at end (append new order)
 *    - Delete: O(1) anywhere (cancel order)
 *    - Traverse: O(N) for matching (acceptable)
 *    - Memory: Pool-allocated (predictable)
 *    ✓ Best for frequent inserts/deletes
 * 
 *    std::vector:
 *    - Insert: O(1) amortized at end, O(N) in middle
 *    - Delete: O(N) (shift elements)
 *    - Lookup: O(1) by index, O(N) by value
 *    - Memory: Reallocation (latency spikes)
 *    ✗ Delete performance unacceptable
 * 
 *    std::list:
 *    - Insert/Delete: O(1)
 *    - Memory: Heap allocation per node
 *    - Predictability: Non-deterministic (allocator)
 *    ✗ Heap allocation unacceptable for HFT
 * 
 *    Array + Free List:
 *    - Insert/Delete: O(1)
 *    - Memory: Pre-allocated (good)
 *    - Complexity: More code (manual management)
 *    ✓ Viable alternative, more complex
 * 
 * 2. CIRCULAR vs NULL-TERMINATED LISTS:
 * 
 *    Circular (CHOSEN):
 *    - Last->next points to first
 *    - First->prev points to last
 *    - Single pointer tracks list (first)
 *    - Insert at end: first->prev->next = new (no traversal)
 *    ✓ O(1) insert at end without tail pointer
 * 
 *    NULL-terminated:
 *    - Last->next = nullptr
 *    - First->prev = nullptr
 *    - Need both head and tail pointers
 *    - Insert at end: tail->next = new
 *    ✗ Requires two pointers (more memory)
 * 
 * 3. MEMORY POOL vs HEAP ALLOCATION:
 * 
 *    Memory Pool (CHOSEN):
 *    - Pre-allocate all orders (e.g., 1M orders)
 *    - Allocation: O(1) placement new
 *    - Deallocation: O(1) return to pool
 *    - Latency: Predictable (no heap)
 *    - Fragmentation: None (reuse slots)
 *    ✓ Required for HFT (deterministic latency)
 * 
 *    Heap Allocation (new/delete):
 *    - Allocation: O(1) average, O(N) worst case
 *    - Deallocation: O(1) average
 *    - Latency: Unpredictable (allocator locks)
 *    - Fragmentation: Increases over time
 *    ✗ Unacceptable for HFT
 * 
 * 4. HASH MAPS vs RED-BLACK TREES:
 * 
 *    Hash Maps (CHOSEN):
 *    - Lookup: O(1) average
 *    - Insert/Delete: O(1) average
 *    - Memory: Fixed array (predictable)
 *    - Simple: Direct indexing
 *    ✓ Best for order lookup (known ID)
 * 
 *    Red-Black Trees (std::map):
 *    - Lookup: O(log N)
 *    - Insert/Delete: O(log N)
 *    - Memory: Node allocation (heap)
 *    - Sorted: Maintains order
 *    ✗ Slower and heap allocation
 *    (But useful for price levels if traversal needed)
 * 
 * PERFORMANCE SUMMARY:
 * - Add order: O(1) = 20-50 ns
 * - Cancel order: O(1) = 20-50 ns
 * - Match order: O(N) where N = orders matched (typically 1-3)
 * - Memory: Pre-allocated (no runtime allocation)
 * - Latency: Deterministic (no heap, no locks)
 * 
 * INDUSTRY PRACTICE:
 * - Most HFT matching engines use similar designs
 * - Doubly-linked lists for FIFO queues
 * - Memory pools for predictable allocation
 * - Hash maps or arrays for fast lookup
 * - Avoid STL containers (heap allocation)
 */
