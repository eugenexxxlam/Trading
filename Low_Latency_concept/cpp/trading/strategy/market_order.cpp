#include "market_order.h"

/*
 * MARKET ORDER IMPLEMENTATION
 * ============================
 * 
 * Simple implementation of MarketOrder::toString().
 * Formats order for human-readable logging.
 * 
 * See market_order.h for detailed class documentation.
 */

namespace Trading {
  /*
   * TO STRING - FORMAT MARKET ORDER
   * ================================
   * 
   * Converts MarketOrder to string for logging and debugging.
   * 
   * FORMAT:
   * "MarketOrder[oid:ID side:SIDE price:PRICE qty:QTY prio:PRIORITY prev:PREV_ID next:NEXT_ID]"
   * 
   * FIELDS:
   * - oid: Exchange order ID
   * - side: BUY or SELL
   * - price: Limit price (formatted to 2 decimal places typically)
   * - qty: Remaining quantity
   * - prio: Order priority (for FIFO ordering)
   * - prev: Previous order's ID in doubly linked list (INVALID if none)
   * - next: Next order's ID in doubly linked list (INVALID if none)
   * 
   * LINKED LIST DEBUGGING:
   * - prev/next: Validate circular list structure
   * - Should form circle: first->prev = last, last->next = first
   * - INVALID: Should never appear in middle of list
   * 
   * EXAMPLE OUTPUT:
   * "MarketOrder[oid:12345 side:BUY price:100.50 qty:10 prio:123456789 prev:12344 next:12346]"
   * 
   * PERFORMANCE:
   * - std::stringstream: Convenient but not fastest
   * - OK for logging: This is not on hot path
   * - Alternative: char buffer + sprintf (slightly faster)
   * 
   * USAGE:
   * ```cpp
   * auto* order = oid_to_order_[order_id];
   * logger_->log("Processing order: %", order->toString());
   * ```
   */
  auto MarketOrder::toString() const -> std::string {
    std::stringstream ss;
    
    // Format: MarketOrder[field:value field:value ...]
    ss << "MarketOrder" << "["
       << "oid:" << orderIdToString(order_id_) << " "          // Order ID
       << "side:" << sideToString(side_) << " "                // BUY or SELL
       << "price:" << priceToString(price_) << " "             // Limit price
       << "qty:" << qtyToString(qty_) << " "                   // Remaining quantity
       << "prio:" << priorityToString(priority_) << " "        // FIFO priority
       << "prev:" << orderIdToString(prev_order_ ? prev_order_->order_id_ : OrderId_INVALID) << " "  // Previous order
       << "next:" << orderIdToString(next_order_ ? next_order_->order_id_ : OrderId_INVALID) << "]"; // Next order

    return ss.str();
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. LINKED LIST VALIDATION:
 *    - prev/next pointers: Should always form valid circle
 *    - Debugging: Check prev->next == this, next->prev == this
 *    - Circular: Last->next = first, first->prev = last
 * 
 * 2. FORMATTING HELPERS:
 *    - orderIdToString(): Formats order ID (typically just std::to_string)
 *    - sideToString(): "BUY" or "SELL"
 *    - priceToString(): Fixed-point to decimal (e.g., 1000000 -> "100.0000")
 *    - qtyToString(): Quantity formatting
 *    - priorityToString(): Priority/timestamp formatting
 * 
 * 3. POINTER SAFETY:
 *    - Check: prev_order_ and next_order_ may be nullptr
 *    - Use: Ternary operator (ptr ? ptr->field : INVALID)
 *    - Safe: No crashes if pointers uninitialized
 * 
 * 4. PERFORMANCE:
 *    - Not hot path: toString() only for logging
 *    - Logging: Typically async (off critical path)
 *    - Acceptable: std::stringstream overhead OK
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Faster formatting: char buffer + sprintf
 * - Conditional logging: Only if log level enabled
 * - Structured logging: JSON format for parsing
 * - Sampling: Log only X% of orders (reduce volume)
 * 
 * DEBUGGING USE CASES:
 * - Order book visualization: Print all orders at price
 * - Linked list validation: Check circular structure
 * - State inspection: Examine order fields
 * - Error investigation: Log order state on error
 * 
 * EXAMPLE DEBUGGING SESSION:
 * ```cpp
 * // Print all orders at price 100.50
 * auto* level = getOrdersAtPrice(100.50);
 * if (level) {
 *   auto* first = level->first_mkt_order_;
 *   for (auto* order = first; ; order = order->next_order_) {
 *     std::cout << order->toString() << std::endl;
 *     if (order->next_order_ == first) break;  // Full circle
 *   }
 * }
 * 
 * // Validate circular list
 * auto* first = level->first_mkt_order_;
 * assert(first->prev_order_->next_order_ == first);  // Circular check
 * assert(first->next_order_->prev_order_ == first);  // Circular check
 * ```
 */
