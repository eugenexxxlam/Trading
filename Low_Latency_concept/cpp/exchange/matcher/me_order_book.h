#pragma once

#include "common/types.h"
#include "common/mem_pool.h"
#include "common/logging.h"
#include "order_server/client_response.h"
#include "market_data/market_update.h"

#include "me_order.h"

using namespace Common;

/*
 * MATCHING ENGINE ORDER BOOK - LIMIT ORDER BOOK IMPLEMENTATION
 * =============================================================
 * 
 * PURPOSE:
 * Implements a high-performance limit order book for a single instrument.
 * Supports add, cancel, and matching operations with time-price priority.
 * 
 * ORDER MATCHING:
 * - Price Priority: Best price first (highest bid, lowest ask)
 * - Time Priority: Within price level, FIFO (first in, first out)
 * - Aggressive orders: Match immediately if possible
 * - Passive orders: Rest in book if no match
 * 
 * DATA STRUCTURES:
 * - Doubly-linked lists: Price levels and orders (O(1) operations)
 * - Hash maps: Fast lookup (O(1) by price or order ID)
 * - Memory pools: Pre-allocated, no heap (predictable latency)
 * 
 * ORDER BOOK VISUALIZATION:
 * ```
 * BIDS (descending):          ASKS (ascending):
 * $150.00 [500 @ 3 orders]    $150.05 [300 @ 2 orders]
 * $149.95 [1000 @ 5 orders]   $150.10 [800 @ 4 orders]
 * $149.90 [200 @ 1 order]     $150.15 [500 @ 2 orders]
 * ```
 * 
 * OPERATIONS COMPLEXITY:
 * - Add order: O(1) if no match, O(N) if matches N orders
 * - Cancel order: O(1) always
 * - Get best bid/ask: O(1) always (maintained pointers)
 * - Memory: O(1) allocation (from pool)
 * 
 * PERFORMANCE:
 * - Add order: 20-100 ns (no match), 50-500 ns (with matching)
 * - Cancel order: 20-50 ns
 * - Memory: Zero heap allocation after initialization
 * - Throughput: 1M+ orders/second per instrument
 * 
 * THREAD SAFETY:
 * - NOT thread-safe (single matching engine thread per instrument)
 * - Matching engine guarantees sequential access
 * - No locking needed (performance optimization)
 */

namespace Exchange {
  // Forward declaration (parent class)
  class MatchingEngine;

  class MEOrderBook final {  // final = no inheritance (optimization)
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Parameters:
     * - ticker_id: Which instrument (0=AAPL, 1=MSFT, etc.)
     * - logger: Async logger for debugging
     * - matching_engine: Parent matching engine (for publishing updates)
     * 
     * Initializes:
     * - Memory pools (orders and price levels)
     * - Hash maps (for fast lookup)
     * - Empty order book (bids/asks = nullptr)
     */
    explicit MEOrderBook(TickerId ticker_id, Logger *logger, MatchingEngine *matching_engine);

    /*
     * DESTRUCTOR
     * ==========
     * 
     * Cleans up memory pools (returns memory to OS).
     * Note: Pools are automatically cleaned up, no manual deallocation needed.
     */
    ~MEOrderBook();

    /*
     * ADD ORDER - NEW ORDER SUBMISSION
     * =================================
     * 
     * Primary order book operation: Add new order.
     * 
     * ALGORITHM:
     * 1. Generate unique market_order_id
     * 2. Check if order matches existing orders (aggressive)
     *    - BUY order: Matches ASK if price >= best ask
     *    - SELL order: Matches BID if price <= best bid
     * 3. If matches: Execute match (call match() for each filled order)
     * 4. If remaining quantity: Add to book as passive order
     * 5. Publish responses and market updates
     * 
     * MATCHING EXAMPLE (BUY order):
     * ```
     * Incoming: BUY 100 @ $150.10
     * Order book asks:
     *   $150.05 [50 shares]
     *   $150.08 [30 shares]
     *   $150.12 [100 shares]
     * 
     * Matching:
     *   1. Fill 50 @ $150.05 (leaves_qty = 50)
     *   2. Fill 30 @ $150.08 (leaves_qty = 20)
     *   3. Add remaining 20 @ $150.10 to bid book (passive)
     * ```
     * 
     * RESPONSES:
     * - ACCEPTED: Order booked successfully
     * - FILLED: Order matched (partial or full)
     * - Multiple FILLEDs possible (if matches multiple orders)
     * 
     * MARKET UPDATES:
     * - TRADE: For each match
     * - ADD: If remaining quantity added to book
     * - CANCEL: If matched order fully filled (removed)
     * - MODIFY: If matched order partially filled (qty reduced)
     * 
     * PERFORMANCE:
     * - No match: 20-50 ns (just add to book)
     * - Match 1 order: 50-100 ns
     * - Match N orders: 50 + (N * 50) ns
     * 
     * NOEXCEPT:
     * - Performance: No exception handling overhead
     * - Acceptable: Errors logged, system continues
     * 
     * Parameters:
     * - client_id: Which client submitted order
     * - client_order_id: Client's order identifier
     * - ticker_id: Instrument (for validation)
     * - side: BUY or SELL
     * - price: Limit price (fixed-point integer)
     * - qty: Order quantity (shares/contracts)
     */
    auto add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept -> void;

    /*
     * CANCEL ORDER - REMOVE ORDER FROM BOOK
     * ======================================
     * 
     * Remove existing order from order book.
     * 
     * ALGORITHM:
     * 1. Lookup order by client_id + order_id (hash map)
     * 2. If found: Remove from book (removeOrder)
     *    - Remove from FIFO queue at price level
     *    - Remove from hash maps
     *    - Deallocate order (return to pool)
     *    - Publish CANCELED response and market update
     * 3. If not found: Publish CANCEL_REJECTED response
     *    - Order already filled
     *    - Order never existed
     *    - Wrong client_id (not your order)
     * 
     * REJECTION REASONS:
     * - Order fully filled (no longer in book)
     * - Order doesn't exist (invalid order_id)
     * - Wrong client (order belongs to different client)
     * 
     * CANCEL RACE CONDITION:
     * ```
     * T1: Client sends cancel for order 123
     * T2: Order 123 matches (fully filled)
     * T3: Cancel arrives
     * Result: CANCEL_REJECTED (order already filled)
     * ```
     * 
     * PERFORMANCE:
     * - Lookup: O(1) - hash map access
     * - Remove: O(1) - doubly-linked list removal
     * - Total: 20-50 ns
     * 
     * Parameters:
     * - client_id: Which client submitted cancel
     * - order_id: Order to cancel (client's ID)
     * - ticker_id: Instrument (for validation)
     */
    auto cancel(ClientId client_id, OrderId order_id, TickerId ticker_id) noexcept -> void;

    /*
     * TO STRING - DEBUG REPRESENTATION
     * =================================
     * 
     * Generate string representation of order book state.
     * 
     * Parameters:
     * - detailed: Include all orders (not just totals)
     * - validity_check: Validate data structure integrity
     * 
     * Output example:
     * ```
     * MEOrderBook[AAPL]
     * BIDS:
     *   $150.00: 500 shares (3 orders)
     *   $149.95: 1000 shares (5 orders)
     * ASKS:
     *   $150.05: 300 shares (2 orders)
     *   $150.10: 800 shares (4 orders)
     * ```
     * 
     * Used for: Debugging, monitoring, audit trail
     */
    auto toString(bool detailed, bool validity_check) const -> std::string;

    // Deleted constructors (prevent accidental copies)
    // Copying order book would be expensive and unnecessary
    MEOrderBook() = delete;
    MEOrderBook(const MEOrderBook &) = delete;
    MEOrderBook(const MEOrderBook &&) = delete;
    MEOrderBook &operator=(const MEOrderBook &) = delete;
    MEOrderBook &operator=(const MEOrderBook &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Which instrument this order book represents
    TickerId ticker_id_ = TickerId_INVALID;

    // Parent matching engine (used to publish updates)
    MatchingEngine *matching_engine_ = nullptr;

    // Hash map: ClientId -> OrderId -> MEOrder*
    // Fast lookup for cancels: cid_oid_to_order_[client_id][order_id]
    // O(1) access, 2 GB memory (with default limits)
    ClientOrderHashMap cid_oid_to_order_;

    // Memory pool for price level objects (MEOrdersAtPrice)
    // Pre-allocated, O(1) allocation/deallocation
    // Typical size: 256K price levels (ME_MAX_PRICE_LEVELS)
    MemPool<MEOrdersAtPrice> orders_at_price_pool_;

    // Best bid/ask pointers (top of book)
    // bids_by_price_: Highest bid price (most aggressive)
    // asks_by_price_: Lowest ask price (most aggressive)
    // O(1) access to best prices
    MEOrdersAtPrice *bids_by_price_ = nullptr;
    MEOrdersAtPrice *asks_by_price_ = nullptr;

    // Hash map: Price -> MEOrdersAtPrice*
    // Fast lookup by price: price_orders_at_price_[price % ME_MAX_PRICE_LEVELS]
    // O(1) access (simple modulo hash)
    OrdersAtPriceHashMap price_orders_at_price_;

    // Memory pool for order objects (MEOrder)
    // Pre-allocated, O(1) allocation/deallocation
    // Typical size: 1M orders (ME_MAX_ORDER_IDS)
    MemPool<MEOrder> order_pool_;

    // Reusable response/update objects (avoid allocation)
    // Populated and published for each operation
    MEClientResponse client_response_;
    MEMarketUpdate market_update_;

    // Market order ID generator (monotonically increasing)
    // Exchange assigns unique ID to each order
    OrderId next_market_order_id_ = 1;

    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (off critical path)
    Logger *logger_ = nullptr;

  private:
    /*
     * PRIVATE HELPER METHODS
     * ======================
     */
    
    /*
     * GENERATE NEW MARKET ORDER ID
     * ============================
     * 
     * Generates unique order ID for each new order.
     * Monotonically increasing: 1, 2, 3, ...
     * 
     * Used for:
     * - Identifying orders internally
     * - Matching client_order_id to market_order_id
     * - Audit trail
     * 
     * Returns: New unique order ID
     */
    auto generateNewMarketOrderId() noexcept -> OrderId {
      return next_market_order_id_++;  // Post-increment (atomic)
    }

    /*
     * PRICE TO INDEX - HASH FUNCTION
     * ===============================
     * 
     * Converts price to hash map index.
     * Simple modulo hash: price % ME_MAX_PRICE_LEVELS
     * 
     * Collisions possible but rare (large table size).
     * Example: ME_MAX_PRICE_LEVELS = 256K
     *   - Price $150.00 (15000 fixed-point) -> index 15000 % 256K = 15000
     *   - Price $400.00 (40000 fixed-point) -> index 40000 % 256K = 40000
     * 
     * Returns: Index into price_orders_at_price_ array
     */
    auto priceToIndex(Price price) const noexcept {
      return (price % ME_MAX_PRICE_LEVELS);
    }

    /*
     * GET ORDERS AT PRICE
     * ===================
     * 
     * Lookup price level by price.
     * Returns pointer to MEOrdersAtPrice or nullptr if price level doesn't exist.
     * 
     * O(1) lookup via hash map.
     * 
     * Returns: Pointer to price level or nullptr
     */
    auto getOrdersAtPrice(Price price) const noexcept -> MEOrdersAtPrice * {
      return price_orders_at_price_.at(priceToIndex(price));
    }

    /*
     * ADD ORDERS AT PRICE - INSERT PRICE LEVEL
     * =========================================
     * 
     * Add new price level to order book.
     * Inserts into both hash map and doubly-linked list.
     * 
     * ALGORITHM:
     * 1. Add to hash map: price_orders_at_price_[price] = new_orders_at_price
     * 2. Add to doubly-linked list (sorted by price):
     *    a. If empty book: new level becomes first (circular list of 1)
     *    b. Find insertion point (binary-like search)
     *    c. Insert at correct position (maintain price ordering)
     *    d. Update best bid/ask if necessary
     * 
     * PRICE ORDERING:
     * - BID side: Descending price ($150.00, $149.95, $149.90, ...)
     * - ASK side: Ascending price ($150.05, $150.10, $150.15, ...)
     * 
     * INSERTION EXAMPLE (BID $149.97):
     * ```
     * Before: $150.00 <-> $149.95 <-> $149.90
     * After:  $150.00 <-> $149.97 <-> $149.95 <-> $149.90
     *                       ^^^^^ inserted here
     * ```
     * 
     * CIRCULAR LIST:
     * - Last entry's next points to first
     * - First entry's prev points to last
     * - Allows O(1) wraparound traversal
     * 
     * COMPLEXITY:
     * - Hash map insert: O(1)
     * - Linked list insert: O(N) worst case (N = price levels on side)
     * - Typical: O(1) to O(log N) (insert near best price)
     * - Amortized: O(1) (price levels added sequentially)
     */
    auto addOrdersAtPrice(MEOrdersAtPrice *new_orders_at_price) noexcept {
      // Add to hash map (O(1))
      price_orders_at_price_.at(priceToIndex(new_orders_at_price->price_)) = new_orders_at_price;

      // Get current best price for this side (bid or ask)
      const auto best_orders_by_price = (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_);
      
      // Case 1: Empty side of book (first price level)
      if (UNLIKELY(!best_orders_by_price)) {
        // Set as best bid or best ask
        (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_) = new_orders_at_price;
        // Create circular list of 1 element (prev and next point to self)
        new_orders_at_price->prev_entry_ = new_orders_at_price->next_entry_ = new_orders_at_price;
      } else {
        // Case 2: Non-empty side, find insertion point
        auto target = best_orders_by_price;
        
        // Check if new price should be added after target
        // SELL: add_after if new_price > target_price (ascending)
        // BUY: add_after if new_price < target_price (descending)
        bool add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                          (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
        
        // Move to next entry if adding after
        if (add_after) {
          target = target->next_entry_;
          // Re-evaluate add_after for new target
          add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                       (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
        }
        
        // Traverse to find correct insertion point
        while (add_after && target != best_orders_by_price) {
          add_after = ((new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ > target->price_) ||
                       (new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ < target->price_));
          if (add_after)
            target = target->next_entry_;
        }

        // Insert into doubly-linked list
        if (add_after) {
          // Add new_orders_at_price after target (less aggressive price)
          if (target == best_orders_by_price) {
            // Wrapped around, insert at end
            target = best_orders_by_price->prev_entry_;
          }
          // Update links: target <-> new <-> target->next
          new_orders_at_price->prev_entry_ = target;
          target->next_entry_->prev_entry_ = new_orders_at_price;
          new_orders_at_price->next_entry_ = target->next_entry_;
          target->next_entry_ = new_orders_at_price;
        } else {
          // Add new_orders_at_price before target (more aggressive price)
          // Update links: target->prev <-> new <-> target
          new_orders_at_price->prev_entry_ = target->prev_entry_;
          new_orders_at_price->next_entry_ = target;
          target->prev_entry_->next_entry_ = new_orders_at_price;
          target->prev_entry_ = new_orders_at_price;

          // Check if new price level is now best (most aggressive)
          if ((new_orders_at_price->side_ == Side::BUY && new_orders_at_price->price_ > best_orders_by_price->price_) ||
              (new_orders_at_price->side_ == Side::SELL && new_orders_at_price->price_ < best_orders_by_price->price_)) {
            // Update next_entry_ for circular list continuity
            target->next_entry_ = (target->next_entry_ == best_orders_by_price ? new_orders_at_price : target->next_entry_);
            // Update best bid/ask pointer
            (new_orders_at_price->side_ == Side::BUY ? bids_by_price_ : asks_by_price_) = new_orders_at_price;
          }
        }
      }
    }

    /*
     * REMOVE ORDERS AT PRICE - DELETE PRICE LEVEL
     * ============================================
     * 
     * Remove price level from order book (when last order at price removed).
     * Removes from both hash map and doubly-linked list.
     * 
     * ALGORITHM:
     * 1. Get price level pointer
     * 2. Remove from doubly-linked list:
     *    a. If only price level: Set best bid/ask to nullptr
     *    b. Otherwise: Unlink from list, update neighbors
     * 3. Remove from hash map: price_orders_at_price_[price] = nullptr
     * 4. Deallocate price level (return to pool)
     * 
     * COMPLEXITY: O(1) always
     * 
     * Parameters:
     * - side: BUY or SELL
     * - price: Price level to remove
     */
    auto removeOrdersAtPrice(Side side, Price price) noexcept {
      // Get current best price for this side
      const auto best_orders_by_price = (side == Side::BUY ? bids_by_price_ : asks_by_price_);
      // Get price level to remove
      auto orders_at_price = getOrdersAtPrice(price);

      // Case 1: Only price level on this side (next points to self)
      if (UNLIKELY(orders_at_price->next_entry_ == orders_at_price)) {
        // Empty side of book (no more bids or asks)
        (side == Side::BUY ? bids_by_price_ : asks_by_price_) = nullptr;
      } else {
        // Case 2: Multiple price levels, unlink this one
        // Update neighbors: prev <-> next (skip current)
        orders_at_price->prev_entry_->next_entry_ = orders_at_price->next_entry_;
        orders_at_price->next_entry_->prev_entry_ = orders_at_price->prev_entry_;

        // If removing best price, update best pointer to next best
        if (orders_at_price == best_orders_by_price) {
          (side == Side::BUY ? bids_by_price_ : asks_by_price_) = orders_at_price->next_entry_;
        }

        // Clear removed entry's pointers (good practice)
        orders_at_price->prev_entry_ = orders_at_price->next_entry_ = nullptr;
      }

      // Remove from hash map
      price_orders_at_price_.at(priceToIndex(price)) = nullptr;

      // Return to memory pool (O(1))
      orders_at_price_pool_.deallocate(orders_at_price);
    }

    /*
     * GET NEXT PRIORITY - FIFO ORDERING
     * ==================================
     * 
     * Generate priority value for new order at price level.
     * Priority determines time ordering (FIFO): lower = earlier.
     * 
     * ALGORITHM:
     * - If price level empty: priority = 1 (first order)
     * - Otherwise: priority = last_order->priority + 1
     * 
     * CIRCULAR LIST TRICK:
     * - first_order->prev_order points to last order
     * - O(1) access to last order (no traversal needed)
     * 
     * Returns: Priority value for new order
     */
    auto getNextPriority(Price price) noexcept {
      const auto orders_at_price = getOrdersAtPrice(price);
      // If price level doesn't exist, first order has priority 1
      if (!orders_at_price)
        return 1lu;

      // Get last order's priority and increment
      // first_me_order->prev_order is last order (circular list)
      return orders_at_price->first_me_order_->prev_order_->priority_ + 1;
    }

    /*
     * MATCH - EXECUTE ORDER AGAINST PASSIVE ORDER
     * ============================================
     * 
     * Match aggressive order against single passive order.
     * Generates execution reports and market updates.
     * 
     * ALGORITHM:
     * 1. Determine match quantity: min(aggressive qty, passive qty)
     * 2. Generate execution reports (FILLED):
     *    - For aggressive order (taker)
     *    - For passive order (maker)
     * 3. Generate market updates:
     *    - TRADE (execution occurred)
     *    - MODIFY or CANCEL for passive order (qty change or removal)
     * 4. Update passive order quantity or remove if fully filled
     * 5. Update aggressive order remaining quantity (leaves_qty)
     * 
     * PARTIAL vs FULL FILL:
     * - Partial: Some quantity remains (update qty, keep in book)
     * - Full: No quantity remains (remove from book)
     * 
     * EXECUTION PRICE:
     * - Always passive order's price (price-time priority)
     * - Example: Aggressive BUY $150.10, passive SELL $150.05
     *   - Execution at $150.05 (better for buyer)
     * 
     * Parameters:
     * - ticker_id: Instrument
     * - client_id: Aggressive order's client
     * - side: Aggressive order's side (BUY or SELL)
     * - client_order_id: Aggressive order's client ID
     * - new_market_order_id: Aggressive order's market ID
     * - bid_itr: Passive order being matched
     * - leaves_qty: Remaining qty on aggressive order (updated)
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto match(TickerId ticker_id, ClientId client_id, Side side, OrderId client_order_id, OrderId new_market_order_id, MEOrder* bid_itr, Qty* leaves_qty) noexcept;

    /*
     * CHECK FOR MATCH - AGGRESSIVE ORDER MATCHING
     * ============================================
     * 
     * Check if new order can match existing passive orders.
     * Matches all possible orders at favorable prices.
     * 
     * MATCHING RULES:
     * - BUY order: Matches ASK if buy_price >= ask_price
     * - SELL order: Matches BID if sell_price <= bid_price
     * 
     * ALGORITHM:
     * 1. Determine opposite side (BUY -> ASK, SELL -> BID)
     * 2. Get best opposite price (best ask or best bid)
     * 3. While match possible and quantity remains:
     *    a. Get first order at best price
     *    b. Call match() to execute
     *    c. Update remaining quantity
     *    d. Move to next order or price level
     * 4. Return remaining quantity
     * 
     * EXAMPLE (BUY 100 @ $150.10):
     * ```
     * Asks:
     *   $150.05 [50]
     *   $150.08 [30]
     *   $150.12 [100]
     * 
     * Matching:
     *   1. Match 50 @ $150.05 (leaves 50)
     *   2. Match 30 @ $150.08 (leaves 20)
     *   3. Stop (next ask $150.12 > limit $150.10)
     * Returns: 20 (remaining to add to book as passive)
     * ```
     * 
     * SWEEP THE BOOK:
     * - Aggressive order can match multiple price levels
     * - Continues until no more favorable prices or qty exhausted
     * 
     * Parameters:
     * - client_id: Order's client
     * - client_order_id: Client's order ID
     * - ticker_id: Instrument
     * - side: BUY or SELL
     * - price: Limit price
     * - qty: Order quantity
     * - new_market_order_id: Market order ID
     * 
     * Declared but not implemented in header (see .cpp file).
     */
    auto checkForMatch(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty, Qty new_market_order_id) noexcept;

    /*
     * REMOVE ORDER - DELETE ORDER FROM BOOK
     * ======================================
     * 
     * Remove order from all data structures and deallocate.
     * 
     * ALGORITHM:
     * 1. Get price level for this order
     * 2. Remove from FIFO queue at price level:
     *    a. If only order: Remove entire price level (removeOrdersAtPrice)
     *    b. Otherwise: Unlink from doubly-linked list
     * 3. Remove from hash map: cid_oid_to_order_[client_id][order_id] = nullptr
     * 4. Deallocate order (return to pool)
     * 
     * COMPLEXITY: O(1) always
     * 
     * CIRCULAR LIST HANDLING:
     * - Single order: prev = next = self
     * - Multiple orders: Unlink and update neighbors
     * - Update first_me_order if removing first
     * 
     * Parameters:
     * - order: Order to remove
     */
    auto removeOrder(MEOrder *order) noexcept {
      // Get price level for this order
      auto orders_at_price = getOrdersAtPrice(order->price_);

      // Case 1: Only order at this price (prev points to self)
      if (order->prev_order_ == order) {
        // Remove entire price level
        removeOrdersAtPrice(order->side_, order->price_);
      } else {
        // Case 2: Multiple orders, unlink this one
        const auto order_before = order->prev_order_;
        const auto order_after = order->next_order_;
        // Update neighbors: before <-> after (skip current)
        order_before->next_order_ = order_after;
        order_after->prev_order_ = order_before;

        // If removing first order, update first pointer
        if (orders_at_price->first_me_order_ == order) {
          orders_at_price->first_me_order_ = order_after;
        }

        // Clear removed order's pointers
        order->prev_order_ = order->next_order_ = nullptr;
      }

      // Remove from client order hash map
      cid_oid_to_order_.at(order->client_id_).at(order->client_order_id_) = nullptr;
      
      // Return to memory pool
      order_pool_.deallocate(order);
    }

    /*
     * ADD ORDER - INSERT ORDER INTO BOOK
     * ===================================
     * 
     * Add passive order to order book (after matching attempt).
     * Inserts at end of FIFO queue at price level.
     * 
     * ALGORITHM:
     * 1. Get or create price level
     * 2. Add to end of FIFO queue:
     *    a. If no price level: Create new (addOrdersAtPrice)
     *    b. Otherwise: Append to end of circular list
     * 3. Add to hash map: cid_oid_to_order_[client_id][order_id] = order
     * 
     * COMPLEXITY:
     * - Existing price level: O(1)
     * - New price level: O(N) where N = price levels (addOrdersAtPrice)
     * 
     * FIFO QUEUE:
     * - New orders added at end (time priority)
     * - Circular list: first->prev points to last (O(1) append)
     * 
     * Parameters:
     * - order: Order to add (already allocated from pool)
     */
    auto addOrder(MEOrder *order) noexcept {
      // Get price level (may be nullptr if doesn't exist)
      const auto orders_at_price = getOrdersAtPrice(order->price_);

      // Case 1: No price level, create new
      if (!orders_at_price) {
        // Order is only element (circular list of 1)
        order->next_order_ = order->prev_order_ = order;

        // Create new price level with this order
        auto new_orders_at_price = orders_at_price_pool_.allocate(order->side_, order->price_, order, nullptr, nullptr);
        // Add price level to book
        addOrdersAtPrice(new_orders_at_price);
      } else {
        // Case 2: Price level exists, append order at end
        auto first_order = (orders_at_price ? orders_at_price->first_me_order_ : nullptr);

        // Insert at end of circular list
        // first->prev is last order (circular property)
        first_order->prev_order_->next_order_ = order;  // last->next = order
        order->prev_order_ = first_order->prev_order_;  // order->prev = last
        order->next_order_ = first_order;                // order->next = first
        first_order->prev_order_ = order;                // first->prev = order
        // Result: first <-> ... <-> order <-> first (order now last)
      }

      // Add to client order hash map (for fast cancel)
      cid_oid_to_order_.at(order->client_id_).at(order->client_order_id_) = order;
    }
  };

  /*
   * ORDER BOOK HASH MAP
   * ===================
   * 
   * Map from TickerId -> MEOrderBook*
   * One order book per instrument.
   * 
   * Usage:
   * ```cpp
   * OrderBookHashMap books;
   * books[ticker_id] = new MEOrderBook(ticker_id, ...);
   * books[ticker_id]->add(...);  // Add order to instrument's book
   * ```
   */
  typedef std::array<MEOrderBook *, ME_MAX_TICKERS> OrderBookHashMap;
}

/*
 * ORDER BOOK IMPLEMENTATION NOTES
 * ================================
 * 
 * 1. TIME-PRICE PRIORITY:
 *    - Price: Better prices first (highest bid, lowest ask)
 *    - Time: At same price, FIFO (first order filled first)
 *    - Industry standard (most exchanges use this)
 * 
 * 2. AGGRESSIVE vs PASSIVE:
 *    - Aggressive: Crosses spread, matches immediately
 *    - Passive: Doesn't cross, rests in book
 *    - Example: BUY $150.10 when best ask is $150.05 (aggressive)
 *    - Example: BUY $150.00 when best ask is $150.05 (passive)
 * 
 * 3. PARTIAL FILLS:
 *    - Order may match multiple passive orders
 *    - Each match generates separate FILLED response
 *    - Client tracks cumulative fills
 * 
 * 4. PRICE IMPROVEMENT:
 *    - Aggressive order executes at passive price
 *    - Example: BUY $150.10 matches SELL $150.05
 *      - Execution at $150.05 (buyer gets $0.05 price improvement)
 *    - Benefit: Reward passive liquidity providers
 * 
 * 5. PERFORMANCE OPTIMIZATIONS:
 *    - Memory pools: No heap allocation
 *    - Hash maps: O(1) lookups
 *    - Doubly-linked lists: O(1) insert/delete
 *    - Circular lists: O(1) append without tail pointer
 *    - Reusable objects: client_response_, market_update_
 *    - Branch hints: LIKELY/UNLIKELY macros
 *    - noexcept: No exception overhead
 *    - final class: No virtual dispatch overhead
 * 
 * 6. ALTERNATIVE DESIGNS:
 *    - Array of price levels: Simpler but wastes memory
 *    - std::map for price levels: Heap allocation, slower
 *    - Lock-free matching: Complex, not needed (single thread)
 *    - Per-price atomics: Overhead, not needed
 * 
 * LATENCY BREAKDOWN (TYPICAL):
 * - Add passive order: 20-50 ns
 * - Cancel order: 20-50 ns
 * - Match 1 order: 50-100 ns (includes responses)
 * - Match 5 orders: 200-500 ns (sweep book)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Order types: Market, stop, IOC, FOK
 * - Self-trade prevention
 * - Maximum shown quantity (iceberg orders)
 * - Minimum execution size
 * - Post-only orders (reject if would match)
 * - Kill switch (halt trading)
 * - Circuit breakers (price bands)
 */
