#include "me_order.h"

/*
 * ME ORDER IMPLEMENTATION
 * =======================
 * 
 * Implementation file for MEOrder toString() method.
 * Simple string conversion for logging and debugging.
 * 
 * See me_order.h for detailed class documentation.
 */

namespace Exchange {
  /*
   * TO STRING - ORDER REPRESENTATION
   * =================================
   * 
   * Generates human-readable string representation of order.
   * Used for logging, debugging, and monitoring.
   * 
   * OUTPUT FORMAT:
   * ```
   * MEOrder[ticker:AAPL cid:42 oid:12345 moid:9876543 side:BUY price:150.00 qty:100 prio:5 prev:9876542 next:9876544]
   * ```
   * 
   * FIELDS:
   * - ticker: Instrument (AAPL, MSFT, etc.)
   * - cid: Client ID (which trading firm)
   * - oid: Client order ID (client's identifier)
   * - moid: Market order ID (exchange's identifier)
   * - side: BUY or SELL
   * - price: Limit price (fixed-point, formatted as dollars)
   * - qty: Remaining quantity (shares/contracts)
   * - prio: Time priority (lower = earlier)
   * - prev: Previous order in FIFO queue (moid)
   * - next: Next order in FIFO queue (moid)
   * 
   * LINKED LIST:
   * - prev/next show FIFO queue structure
   * - Helps debug circular list correctness
   * - INVALID means nullptr (no previous/next)
   * 
   * PERFORMANCE:
   * - Not performance-critical (logging only)
   * - Uses std::stringstream (allocation)
   * - Called async in logger (off hot path)
   * 
   * Returns: String representation of order
   */
  auto MEOrder::toString() const -> std::string {
    std::stringstream ss;
    
    // Build formatted string with all order fields
    ss << "MEOrder" << "["
       << "ticker:" << tickerIdToString(ticker_id_) << " "         // Instrument name
       << "cid:" << clientIdToString(client_id_) << " "            // Client ID
       << "oid:" << orderIdToString(client_order_id_) << " "       // Client's order ID
       << "moid:" << orderIdToString(market_order_id_) << " "      // Exchange's order ID
       << "side:" << sideToString(side_) << " "                    // BUY or SELL
       << "price:" << priceToString(price_) << " "                 // Limit price
       << "qty:" << qtyToString(qty_) << " "                       // Remaining quantity
       << "prio:" << priorityToString(priority_) << " "            // Time priority
       << "prev:" << orderIdToString(prev_order_ ? prev_order_->market_order_id_ : OrderId_INVALID) << " "  // Previous in FIFO
       << "next:" << orderIdToString(next_order_ ? next_order_->market_order_id_ : OrderId_INVALID) << "]"; // Next in FIFO

    return ss.str();
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. STRINGSTREAM:
 *    - Convenient for formatted output
 *    - Allocates memory (heap)
 *    - Not performance-critical (logging only)
 *    - Production: Could use custom formatter (faster)
 * 
 * 2. HELPER FUNCTIONS:
 *    - tickerIdToString(), sideToString(), etc.
 *    - Defined in common/types.h
 *    - Convert enums/integers to human-readable strings
 * 
 * 3. POINTER SAFETY:
 *    - Check prev_order_, next_order_ for nullptr
 *    - Print INVALID if nullptr (safe)
 *    - Prevents segfault if order not in list
 * 
 * 4. DEBUGGING USE:
 *    - Trace order through matching process
 *    - Verify FIFO queue structure
 *    - Audit trail (regulatory compliance)
 *    - Performance analysis (identify hot orders)
 */
