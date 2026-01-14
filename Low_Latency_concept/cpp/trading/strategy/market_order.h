#pragma once

#include <array>
#include <sstream>
#include "common/types.h"

using namespace Common;

/*
 * MARKET ORDER - TRADING FIRM'S ORDER BOOK REPRESENTATION
 * ========================================================
 * 
 * PURPOSE:
 * Trading firm's internal representation of exchange order book.
 * Tracks all orders (not just own orders) to maintain accurate market view.
 * 
 * WHY REPLICATE ORDER BOOK:
 * - Exchange: Has full order book (all orders)
 * - Trading firm: Receives market data, rebuilds order book locally
 * - Purpose: Make trading decisions based on current market state
 * - Critical: Low-latency access (cannot query exchange)
 * 
 * DATA STRUCTURES:
 * 
 * 1. MarketOrder:
 *    - Single order in the book
 *    - Doubly linked list node (at same price level)
 * 
 * 2. MarketOrdersAtPrice:
 *    - All orders at a specific price level
 *    - Doubly linked list node (across price levels)
 * 
 * 3. MarketOrderBook:
 *    - Full order book (all price levels)
 *    - Separate buy and sell sides
 * 
 * ORDER BOOK STRUCTURE:
 * ```
 * SELLS (asks):
 * Price 101.00 -> [Order3(20)] -> [Order7(10)] -> [Order9(5)]
 * Price 100.75 -> [Order2(15)]
 * Price 100.50 <- TOP OF BOOK (best ask)
 *       X
 * Price 100.25 <- TOP OF BOOK (best bid)
 * Price 100.00 -> [Order1(10)] -> [Order4(20)]
 * Price  99.75 -> [Order5(15)] -> [Order6(5)] -> [Order8(10)]
 * BUYS (bids)
 * ```
 * 
 * DOUBLY LINKED LIST ADVANTAGES:
 * - Fast insertion: O(1) at end of price level
 * - Fast removal: O(1) with pointer
 * - FIFO ordering: Orders executed in arrival order
 * - Price-time priority: Standard exchange matching
 * 
 * BBO (BEST BID OFFER):
 * - Best bid: Highest buy price (100.25 above)
 * - Best ask: Lowest sell price (100.50 above)
 * - Spread: ask - bid (0.25 above)
 * - Critical: Most important market data for trading
 * 
 * MARKET UPDATES:
 * - ADD: New order added to book
 * - MODIFY: Order quantity changed (usually reduced)
 * - CANCEL: Order removed from book
 * - TRADE: Orders matched, removed from book
 * - CLEAR: Full book reset (recovery, EOD)
 * 
 * PERFORMANCE:
 * - Order lookup: O(1) via hash map (order_id -> MarketOrder)
 * - Price lookup: O(1) via hash map (price -> MarketOrdersAtPrice)
 * - BBO access: O(1) via pointers (bids_by_price_, asks_by_price_)
 * - Add order: O(1) append to price level
 * - Remove order: O(1) doubly linked list removal
 * - Memory: Pre-allocated pools (no heap allocation)
 * 
 * MEMORY POOLS:
 * - MemPool<MarketOrder>: Pre-allocated orders
 * - MemPool<MarketOrdersAtPrice>: Pre-allocated price levels
 * - Capacity: ME_MAX_ORDER_IDS orders, ME_MAX_PRICE_LEVELS prices
 * - Advantage: O(1) alloc/dealloc, no fragmentation
 */

namespace Trading {
  /*
   * MARKET ORDER - SINGLE ORDER IN BOOK
   * ====================================
   * 
   * Represents a single limit order in the trading firm's order book.
   * 
   * FIELDS:
   * 
   * order_id_:
   * - Unique order identifier from exchange
   * - Used for: MODIFY, CANCEL, TRADE messages
   * 
   * side_:
   * - BUY or SELL
   * - Determines: Which side of book (bids vs asks)
   * 
   * price_:
   * - Limit price (fixed-point)
   * - Determines: Which price level
   * 
   * qty_:
   * - Remaining quantity (updated on partia fills)
   * - Zero: Order fully filled (removed from book)
   * 
   * priority_:
   * - Order priority within price level (exchange-specific)
   * - Typically: Arrival timestamp (FIFO)
   * - Used for: Correct FIFO ordering
   * 
   * DOUBLY LINKED LIST (ORDERS AT SAME PRICE):
   * - prev_order_: Previous order at this price
   * - next_order_: Next order at this price
   * - Circular: Last->next = First, First->prev = Last
   * - Purpose: FIFO queue at each price level
   * 
   * MEMORY:
   * - Size: ~48 bytes (platform-dependent)
   * - Alignment: Natural (8-byte aligned)
   * - Pool: Pre-allocated (no heap allocation)
   * 
   * USAGE:
   * ```cpp
   * // Allocate from pool
   * auto* order = order_pool_.allocate(
   *   order_id, Side::BUY, 100.50, 10, priority, nullptr, nullptr
   * );
   * 
   * // Add to book
   * addOrder(order);  // Links into doubly linked list
   * 
   * // Remove from book
   * removeOrder(order);  // Unlinks, returns to pool
   * ```
   */
  struct MarketOrder {
    OrderId order_id_ = OrderId_INVALID;     // Unique order ID from exchange
    Side side_ = Side::INVALID;               // BUY or SELL
    Price price_ = Price_INVALID;             // Limit price
    Qty qty_ = Qty_INVALID;                   // Remaining quantity
    Priority priority_ = Priority_INVALID;    // Order priority (for FIFO)

    // Doubly linked list pointers (orders at same price level)
    // Circular: last->next = first, first->prev = last
    MarketOrder *prev_order_ = nullptr;       // Previous order at this price
    MarketOrder *next_order_ = nullptr;       // Next order at this price

    /*
     * DEFAULT CONSTRUCTOR
     * ===================
     * 
     * Required for use with MemPool.
     * MemPool pre-allocates array of objects, needs default constructor.
     */
    MarketOrder() = default;

    /*
     * PARAMETERIZED CONSTRUCTOR
     * =========================
     * 
     * Creates MarketOrder with specified attributes.
     * Called by: MemPool::allocate()
     * 
     * Parameters:
     * - order_id: Exchange order ID
     * - side: BUY or SELL
     * - price: Limit price
     * - qty: Order quantity
     * - priority: Order priority (for FIFO)
     * - prev_order: Previous order at this price (for linking)
     * - next_order: Next order at this price (for linking)
     */
    MarketOrder(OrderId order_id, Side side, Price price, Qty qty, Priority priority, MarketOrder *prev_order, MarketOrder *next_order) noexcept
        : order_id_(order_id), side_(side), price_(price), qty_(qty), priority_(priority), prev_order_(prev_order), next_order_(next_order) {}

    /*
     * TO STRING - FORMAT ORDER FOR LOGGING
     * =====================================
     * 
     * Formats MarketOrder for human-readable logging.
     * 
     * Example output:
     * "MarketOrder[oid:12345 side:BUY price:100.50 qty:10 prio:123456789 prev:12344 next:12346]"
     * 
     * Used for:
     * - Order book debugging
     * - Market data logging
     * - Linked list validation
     * 
     * Implemented in market_order.cpp.
     */
    auto toString() const -> std::string;
  };

  /*
   * ORDER HASH MAP
   * ==============
   * 
   * Fast lookup from OrderId to MarketOrder.
   * 
   * Structure:
   * - std::array indexed by OrderId
   * - Size: ME_MAX_ORDER_IDS (e.g., 1,048,576 = 2^20)
   * - Value: MarketOrder* (nullptr if not present)
   * 
   * Access: O(1)
   * ```cpp
   * auto* order = oid_to_order_[order_id];  // Direct array access
   * if (order) {
   *   // Order exists, process
   * }
   * ```
   * 
   * Memory:
   * - Size: 8 bytes/pointer * ME_MAX_ORDER_IDS
   * - Example: 8 MB for 1M orders
   * - Trade-off: Memory vs speed (O(1) lookup)
   */
  typedef std::array<MarketOrder *, ME_MAX_ORDER_IDS> OrderHashMap;

  /*
   * MARKET ORDERS AT PRICE - PRICE LEVEL IN BOOK
   * =============================================
   * 
   * Represents all orders at a specific price level.
   * Maintains FIFO queue of orders at this price.
   * 
   * FIELDS:
   * 
   * side_:
   * - BUY or SELL
   * - Determines: Bid or ask side
   * 
   * price_:
   * - Price level
   * - All orders at this node have this price
   * 
   * first_mkt_order_:
   * - Pointer to first order in FIFO queue
   * - Circular list: first->prev = last, last->next = first
   * - Purpose: Iterate all orders at this price
   * 
   * DOUBLY LINKED LIST (PRICE LEVELS):
   * - prev_entry_: Previous price level (better price)
   * - next_entry_: Next price level (worse price)
   * - Circular: Last->next = First, First->prev = Last
   * - Sorted: Bids descending (100, 99, 98), Asks ascending (100, 101, 102)
   * 
   * PRICE LEVEL ORDERING:
   * ```
   * BIDS (descending):
   * 100.25 (best) <-> 100.00 <-> 99.75 <-> 99.50 <-> ...
   * 
   * ASKS (ascending):
   * 100.50 (best) <-> 100.75 <-> 101.00 <-> 101.25 <-> ...
   * ```
   * 
   * TOP OF BOOK:
   * - bids_by_price_: Points to best bid (highest price)
   * - asks_by_price_: Points to best ask (lowest price)
   * - O(1) BBO access
   * 
   * USAGE:
   * ```cpp
   * // Iterate all orders at price level
   * auto* orders_at_price = getOrdersAtPrice(100.50);
   * auto* first = orders_at_price->first_mkt_order_;
   * for (auto* order = first; ; order = order->next_order_) {
   *   // Process order
   *   if (order->next_order_ == first) break;  // Full circle
   * }
   * 
   * // Iterate all price levels (bids)
   * for (auto* level = bids_by_price_; level; ) {
   *   // Process price level
   *   level = (level->next_entry_ == bids_by_price_ ? nullptr : level->next_entry_);
   * }
   * ```
   */
  struct MarketOrdersAtPrice {
    Side side_ = Side::INVALID;               // BUY or SELL
    Price price_ = Price_INVALID;             // Price level

    // Pointer to first order at this price (circular doubly linked list)
    MarketOrder *first_mkt_order_ = nullptr;

    // Doubly linked list pointers (price levels)
    // Circular: last->next = first, first->prev = last
    // Sorted: Bids descending, Asks ascending
    MarketOrdersAtPrice *prev_entry_ = nullptr;  // Better price (for buys: higher, for sells: lower)
    MarketOrdersAtPrice *next_entry_ = nullptr;  // Worse price (for buys: lower, for sells: higher)

    /*
     * DEFAULT CONSTRUCTOR
     * ===================
     * 
     * Required for use with MemPool.
     */
    MarketOrdersAtPrice() = default;

    /*
     * PARAMETERIZED CONSTRUCTOR
     * =========================
     * 
     * Creates MarketOrdersAtPrice with specified attributes.
     * Called by: MemPool::allocate()
     * 
     * Parameters:
     * - side: BUY or SELL
     * - price: Price level
     * - first_mkt_order: First order at this price
     * - prev_entry: Previous price level (better price)
     * - next_entry: Next price level (worse price)
     */
    MarketOrdersAtPrice(Side side, Price price, MarketOrder *first_mkt_order, MarketOrdersAtPrice *prev_entry, MarketOrdersAtPrice *next_entry)
        : side_(side), price_(price), first_mkt_order_(first_mkt_order), prev_entry_(prev_entry), next_entry_(next_entry) {}

    /*
     * TO STRING - FORMAT PRICE LEVEL FOR LOGGING
     * ===========================================
     * 
     * Formats MarketOrdersAtPrice for debugging.
     * 
     * Example output:
     * "MarketOrdersAtPrice[side:BUY price:100.50 first_mkt_order:MarketOrder[...] prev:100.75 next:100.25]"
     * 
     * Used for:
     * - Order book structure debugging
     * - Price level validation
     * - Linked list validation
     */
    auto toString() const {
      std::stringstream ss;
      ss << "MarketOrdersAtPrice["
         << "side:" << sideToString(side_) << " "
         << "price:" << priceToString(price_) << " "
         << "first_mkt_order:" << (first_mkt_order_ ? first_mkt_order_->toString() : "null") << " "
         << "prev:" << priceToString(prev_entry_ ? prev_entry_->price_ : Price_INVALID) << " "
         << "next:" << priceToString(next_entry_ ? next_entry_->price_ : Price_INVALID) << "]";

      return ss.str();
    }
  };

  /*
   * ORDERS AT PRICE HASH MAP
   * ========================
   * 
   * Fast lookup from Price to MarketOrdersAtPrice.
   * 
   * Structure:
   * - std::array indexed by (price % ME_MAX_PRICE_LEVELS)
   * - Size: ME_MAX_PRICE_LEVELS (e.g., 65,536 = 2^16)
   * - Value: MarketOrdersAtPrice* (nullptr if not present)
   * - Hash: Modulo (simple, fast)
   * 
   * Access: O(1)
   * ```cpp
   * auto index = priceToIndex(price);  // price % ME_MAX_PRICE_LEVELS
   * auto* orders_at_price = price_orders_at_price_[index];
   * ```
   * 
   * Collision Handling:
   * - Modulo hash can collide (e.g., 100.00 and 165636.00)
   * - Rare: Typically only a few hundred price levels active
   * - If collision: Price validation needed
   * 
   * Memory:
   * - Size: 8 bytes/pointer * ME_MAX_PRICE_LEVELS
   * - Example: 512 KB for 64K price levels
   */
  typedef std::array<MarketOrdersAtPrice *, ME_MAX_PRICE_LEVELS> OrdersAtPriceHashMap;

  /*
   * BBO (BEST BID OFFER) - TOP OF BOOK
   * ===================================
   * 
   * Lightweight abstraction of best prices and quantities.
   * Used by components that don't need full order book.
   * 
   * FIELDS:
   * 
   * bid_price_:
   * - Best (highest) buy price
   * - Price_INVALID if no bids
   * 
   * bid_qty_:
   * - Total quantity at best bid
   * - Sum of all orders at bid_price_
   * 
   * ask_price_:
   * - Best (lowest) sell price
   * - Price_INVALID if no asks
   * 
   * ask_qty_:
   * - Total quantity at best ask
   * - Sum of all orders at ask_price_
   * 
   * DERIVED METRICS:
   * ```cpp
   * auto spread = bbo.ask_price_ - bbo.bid_price_;  // Spread
   * auto mid = (bbo.bid_price_ + bbo.ask_price_) / 2.0;  // Mid price
   * auto imbalance = (bbo.bid_qty_ - bbo.ask_qty_) / 
   *                  (bbo.bid_qty_ + bbo.ask_qty_);  // Order imbalance
   * ```
   * 
   * UPDATE FREQUENCY:
   * - Updated: On every order book change affecting top of book
   * - Frequent: 1000-10000 updates/second (active market)
   * - Critical: Many strategies only use BBO (don't need full depth)
   * 
   * USAGE:
   * ```cpp
   * auto* bbo = book->getBBO();
   * if (bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID) {
   *   auto mid = (bbo->bid_price_ + bbo->ask_price_) / 2.0;
   *   // Trade decision based on mid price
   * }
   * ```
   * 
   * PERFORMANCE:
   * - Size: 32 bytes (4 fields * 8 bytes)
   * - Cache-friendly: Single cache line
   * - Fast access: No pointer chasing
   */
  struct BBO {
    Price bid_price_ = Price_INVALID, ask_price_ = Price_INVALID;  // Best bid and ask prices
    Qty bid_qty_ = Qty_INVALID, ask_qty_ = Qty_INVALID;            // Total qty at best prices

    /*
     * TO STRING - FORMAT BBO FOR LOGGING
     * ===================================
     * 
     * Formats BBO in standard market data notation.
     * 
     * Example output:
     * "BBO{10@100.25X100.50@15}"
     * Interpretation: 10 shares bid at 100.25, 15 shares ask at 100.50
     * 
     * Format: "bid_qty @ bid_price X ask_price @ ask_qty"
     */
    auto toString() const {
      std::stringstream ss;
      ss << "BBO{"
         << qtyToString(bid_qty_) << "@" << priceToString(bid_price_)
         << "X"
         << priceToString(ask_price_) << "@" << qtyToString(ask_qty_)
         << "}";

      return ss.str();
    };
  };
}

/*
 * MARKET ORDER DESIGN CONSIDERATIONS
 * ===================================
 * 
 * 1. DOUBLY LINKED LISTS:
 *    - Advantage: O(1) insert, O(1) remove (with pointer)
 *    - Disadvantage: Pointer chasing (cache misses)
 *    - Alternative: Array-based (better cache, complex bookkeeping)
 *    - Industry: Most HFT firms use linked lists for order books
 * 
 * 2. CIRCULAR LISTS:
 *    - Advantage: No null checks, simpler iteration
 *    - Iteration: while (order != first) or for (; ; break when == first)
 *    - Disadvantage: Must check explicitly for full circle
 * 
 * 3. HASH MAPS (ARRAYS):
 *    - Order lookup: oid_to_order_[order_id]
 *    - Price lookup: price_orders_at_price_[priceToIndex(price)]
 *    - O(1) access: Critical for latency
 *    - Memory: Trade-off (8 MB for 1M orders)
 * 
 * 4. MEMORY POOLS:
 *    - Pre-allocation: No malloc/free during trading
 *    - O(1) alloc/dealloc: Predictable latency
 *    - No fragmentation: Memory efficiency
 *    - Capacity: Fixed at init (must be sufficient)
 * 
 * 5. PRIORITY (FIFO):
 *    - Exchange assigns: Arrival timestamp or sequence number
 *    - Trading firm: Respects FIFO (doesn't reorder)
 *    - Critical: Accurate order book representation
 * 
 * 6. BBO ABSTRACTION:
 *    - Lightweight: Only top of book
 *    - Use case: Most strategies only need BBO
 *    - Performance: Avoid full book iteration
 *    - Update: O(1) on top of book changes
 * 
 * LATENCY BREAKDOWN (ORDER BOOK UPDATE):
 * - Market data receive: 1-5 μs (UDP multicast)
 * - Hash lookup (order): 10-50 ns (array access, cache hit)
 * - Linked list update: 20-100 ns (pointer manipulation)
 * - BBO update: 10-50 ns (if top of book affected)
 * - Total: 50-200 ns (order book update)
 * - Acceptable: Sub-microsecond is fast enough
 * 
 * MEMORY USAGE:
 * - MarketOrder: ~48 bytes
 * - MarketOrdersAtPrice: ~40 bytes
 * - Order hash map: 8 MB (1M orders * 8 bytes/pointer)
 * - Price hash map: 512 KB (64K prices * 8 bytes/pointer)
 * - Total: ~10 MB per order book (one ticker)
 * - Multiple tickers: 10 tickers * 10 MB = 100 MB (acceptable)
 * 
 * VALIDATION AND DEBUGGING:
 * - Circular list: Check for cycles (should always have cycle)
 * - Price sorting: Bids descending, asks ascending
 * - Quantity consistency: Sum orders at price = BBO qty
 * - Hash map: Check no collisions (or handle properly)
 * - Memory leaks: All allocations returned to pool
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Price levels: Store aggregate qty (avoid iteration)
 * - Order count: Track num orders at each price
 * - Market depth: Maintain full depth (10-20 levels)
 * - Order book snapshots: Periodic checksums (detect corruption)
 * - Incremental updates: Only update changed levels (not full rebuild)
 * - Implied orders: For futures (complex spread relationships)
 * - Hidden orders: Iceberg, reserve qty (exchange-specific)
 * - Auction handling: Opening/closing auctions (different logic)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Array-Based Order Book:
 *    - std::array or std::vector instead of linked lists
 *    - Advantage: Better cache locality
 *    - Disadvantage: O(N) insert/remove (shift elements)
 *    - Use case: Small order books (<100 orders)
 * 
 * B) Skip List:
 *    - Probabilistic data structure
 *    - Advantage: O(log N) insert/remove/search
 *    - Disadvantage: More complex, less deterministic
 *    - Industry: Some high-frequency trading systems
 * 
 * C) B-Tree or B+ Tree:
 *    - Sorted tree structure
 *    - Advantage: Good cache locality, sorted iteration
 *    - Disadvantage: More complex, O(log N) operations
 *    - Use case: Massive order books (millions of orders)
 * 
 * COMMON BUGS:
 * - Dangling pointers: Order removed but not unlinked
 * - Memory leaks: Order allocated but not returned to pool
 * - Double-free: Order deallocated twice
 * - Incorrect FIFO: Orders not added at end of queue
 * - BBO not updated: Top of book changed but BBO stale
 * - Hash collisions: Price collision not handled
 * - Circular list broken: prev/next pointers inconsistent
 */
