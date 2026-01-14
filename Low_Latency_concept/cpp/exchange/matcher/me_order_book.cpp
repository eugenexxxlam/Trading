#include "me_order_book.h"

#include "matcher/matching_engine.h"

/*
 * ORDER BOOK IMPLEMENTATION - CORE MATCHING LOGIC
 * ================================================
 * 
 * Implementation of MEOrderBook: add, cancel, match operations.
 * Contains the core matching algorithm for price-time priority.
 * 
 * See me_order_book.h for detailed class documentation and data structures.
 * 
 * KEY ALGORITHMS IMPLEMENTED:
 * - add(): Add new order, attempt matching, add remainder to book
 * - cancel(): Remove order from book
 * - match(): Execute trade between aggressive and passive orders
 * - checkForMatch(): Match aggressive order against passive side
 * - toString(): Visualize order book state (debugging)
 */

namespace Exchange {
  /*
   * CONSTRUCTOR - INITIALIZE ORDER BOOK
   * ====================================
   * 
   * Initializes order book for single instrument.
   * 
   * ALGORITHM:
   * 1. Store ticker ID, matching engine pointer, logger
   * 2. Initialize memory pools (orders and price levels)
   * 3. Hash maps initialized empty (zeros)
   * 4. Best bid/ask initialized to nullptr (empty book)
   * 
   * MEMORY POOLS:
   * - orders_at_price_pool_: ME_MAX_PRICE_LEVELS (256K price levels)
   * - order_pool_: ME_MAX_ORDER_IDS (1M orders)
   * - Pre-allocated: No heap allocation during matching
   * 
   * Parameters:
   * - ticker_id: Which instrument (0=AAPL, 1=MSFT, etc.)
   * - logger: Async logger for debugging
   * - matching_engine: Parent engine (for sending responses/updates)
   */
  MEOrderBook::MEOrderBook(TickerId ticker_id, Logger *logger, MatchingEngine *matching_engine)
      : ticker_id_(ticker_id),                             // Store instrument ID
        matching_engine_(matching_engine),                 // Store parent engine
        orders_at_price_pool_(ME_MAX_PRICE_LEVELS),       // Initialize price level pool
        order_pool_(ME_MAX_ORDER_IDS),                    // Initialize order pool
        logger_(logger) {                                  // Store logger
  }

  /*
   * DESTRUCTOR - CLEANUP AND LOG FINAL STATE
   * =========================================
   * 
   * Logs final order book state and cleans up.
   * 
   * ALGORITHM:
   * 1. Log final order book state (toString)
   * 2. Clear matching engine pointer
   * 3. Clear best bid/ask pointers
   * 4. Clear client order hash map
   * 
   * LOGGING:
   * - toString(false, true): Not detailed, with validity check
   * - Shows final state at shutdown (debugging, audit)
   * - Useful: Verify no orders left, check consistency
   * 
   * CLEANUP:
   * - Memory pools auto-cleanup (RAII)
   * - No manual deallocation needed
   */
  MEOrderBook::~MEOrderBook() {
    // Log final order book state (for debugging, audit)
    logger_->log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str_),
                toString(false, true));

    // Clear pointers (not owned, don't delete)
    matching_engine_ = nullptr;
    bids_by_price_ = asks_by_price_ = nullptr;
    
    // Clear client order hash map (all nullptr)
    for (auto &itr: cid_oid_to_order_) {
      itr.fill(nullptr);
    }
  }

  /*
   * MATCH - EXECUTE TRADE BETWEEN TWO ORDERS
   * =========================================
   * 
   * Matches aggressive order against passive order.
   * Generates execution reports and market updates.
   * 
   * ALGORITHM:
   * 1. Calculate fill quantity: min(aggressive qty, passive qty)
   * 2. Update quantities:
   *    - Aggressive: leaves_qty -= fill_qty (remaining)
   *    - Passive: order->qty_ -= fill_qty (updated)
   * 3. Generate execution reports (FILLED):
   *    a. For aggressive order (taker)
   *    b. For passive order (maker)
   * 4. Generate TRADE market update (execution occurred)
   * 5. If passive order fully filled (qty = 0):
   *    - Generate CANCEL market update (order removed)
   *    - Remove order from book (removeOrder)
   * 6. Else (passive order partially filled):
   *    - Generate MODIFY market update (qty reduced)
   *    - Keep order in book (with updated qty)
   * 
   * FILL QUANTITY:
   * - fill_qty = min(aggressive leaves_qty, passive order_qty)
   * - Example: Aggressive 100, Passive 30 -> Fill 30
   * - Example: Aggressive 30, Passive 100 -> Fill 30
   * 
   * EXECUTION PRICE:
   * - Always passive order's price (price-time priority)
   * - Example: Aggressive BUY $150.10, Passive SELL $150.05
   *   - Execution at $150.05 (better for buyer)
   * - Price improvement for aggressive order
   * 
   * TWO FILLED RESPONSES:
   * - Aggressive order: client_id, new_market_order_id, leaves_qty updated
   * - Passive order: order->client_id_, order->market_order_id_, order->qty_ updated
   * - Both clients receive execution reports
   * 
   * MARKET UPDATES:
   * - TRADE: Execution occurred (side, price, fill_qty)
   *   - order_id = INVALID (represents both sides)
   * - CANCEL or MODIFY: Passive order state changed
   * 
   * PERFORMANCE:
   * - 50-100 ns per match (including responses)
   * - Memory: O(1) (reuse member variables)
   * - No allocation (responses/updates reused)
   * 
   * Parameters:
   * - ticker_id: Instrument
   * - client_id: Aggressive order's client
   * - side: Aggressive order's side
   * - client_order_id: Aggressive order's client ID
   * - new_market_order_id: Aggressive order's market ID
   * - itr: Passive order (being matched)
   * - leaves_qty: Aggressive order's remaining qty (in/out parameter)
   */
  auto MEOrderBook::match(TickerId ticker_id, ClientId client_id, Side side, OrderId client_order_id, OrderId new_market_order_id, MEOrder* itr, Qty* leaves_qty) noexcept {
    const auto order = itr;                    // Passive order
    const auto order_qty = order->qty_;        // Original passive qty (before match)
    
    // Calculate fill quantity (min of aggressive and passive)
    const auto fill_qty = std::min(*leaves_qty, order_qty);

    // Update quantities
    *leaves_qty -= fill_qty;        // Aggressive: Reduce remaining
    order->qty_ -= fill_qty;        // Passive: Reduce quantity

    // Generate FILLED response for aggressive order (taker)
    client_response_ = {ClientResponseType::FILLED,        // Type: FILLED
                        client_id, ticker_id,               // Client, instrument
                        client_order_id,                    // Client's order ID
                        new_market_order_id,                // Market order ID
                        side,                               // Aggressive side
                        itr->price_,                        // Execution price (passive price)
                        fill_qty,                           // Quantity filled
                        *leaves_qty};                       // Quantity remaining
    matching_engine_->sendClientResponse(&client_response_);  // Send to client

    // Generate FILLED response for passive order (maker)
    client_response_ = {ClientResponseType::FILLED,           // Type: FILLED
                        order->client_id_,                    // Passive client
                        ticker_id,                            // Instrument
                        order->client_order_id_,              // Passive client order ID
                        order->market_order_id_,              // Passive market order ID
                        order->side_,                         // Passive side
                        itr->price_,                          // Execution price
                        fill_qty,                             // Quantity filled
                        order->qty_};                         // Quantity remaining
    matching_engine_->sendClientResponse(&client_response_);  // Send to client

    // Generate TRADE market update (execution occurred)
    market_update_ = {MarketUpdateType::TRADE,     // Type: TRADE
                      OrderId_INVALID,              // No specific order (both sides)
                      ticker_id,                    // Instrument
                      side,                         // Aggressive side
                      itr->price_,                  // Execution price
                      fill_qty,                     // Quantity traded
                      Priority_INVALID};            // No priority (not relevant)
    matching_engine_->sendMarketUpdate(&market_update_);  // Broadcast

    // Handle passive order post-match
    if (!order->qty_) {
      // Passive order fully filled (qty = 0): Remove from book
      
      // Generate CANCEL market update (order removed)
      market_update_ = {MarketUpdateType::CANCEL,        // Type: CANCEL (fully filled)
                        order->market_order_id_,          // Order ID
                        ticker_id,                        // Instrument
                        order->side_,                     // Side
                        order->price_,                    // Price
                        order_qty,                        // Original quantity
                        Priority_INVALID};                // Priority
      matching_engine_->sendMarketUpdate(&market_update_);  // Broadcast

      // Remove order from order book (deallocate, update structures)
      START_MEASURE(Exchange_MEOrderBook_removeOrder);
      removeOrder(order);                                // O(1) removal
      END_MEASURE(Exchange_MEOrderBook_removeOrder, (*logger_));
    } else {
      // Passive order partially filled (qty > 0): Keep in book with reduced qty
      
      // Generate MODIFY market update (quantity reduced)
      market_update_ = {MarketUpdateType::MODIFY,        // Type: MODIFY (qty changed)
                        order->market_order_id_,          // Order ID
                        ticker_id,                        // Instrument
                        order->side_,                     // Side
                        order->price_,                    // Price (unchanged)
                        order->qty_,                      // New quantity (reduced)
                        order->priority_};                // Priority (unchanged)
      matching_engine_->sendMarketUpdate(&market_update_);  // Broadcast
    }
  }

  /*
   * CHECK FOR MATCH - AGGRESSIVE ORDER MATCHING
   * ============================================
   * 
   * Checks if aggressive order can match existing passive orders.
   * Matches all possible orders at favorable prices (sweep the book).
   * 
   * ALGORITHM:
   * 1. Initialize leaves_qty = order quantity (full amount)
   * 2. If BUY order:
   *    a. While qty remains AND asks exist:
   *       - Get best ask (asks_by_price_->first_me_order_)
   *       - If buy price < ask price: STOP (can't match)
   *       - Else: Call match() to execute trade
   * 3. If SELL order:
   *    a. While qty remains AND bids exist:
   *       - Get best bid (bids_by_price_->first_me_order_)
   *       - If sell price > bid price: STOP (can't match)
   *       - Else: Call match() to execute trade
   * 4. Return leaves_qty (remaining quantity to add to book)
   * 
   * MATCHING RULES:
   * - BUY order: Matches ASK if buy_price >= ask_price
   * - SELL order: Matches BID if sell_price <= bid_price
   * 
   * PRICE-TIME PRIORITY:
   * - Best price first: Highest bid, lowest ask
   * - Same price: FIFO (first_me_order_ is earliest)
   * 
   * SWEEP THE BOOK:
   * - Aggressive order can match multiple price levels
   * - Example: BUY 100 @ $150.10
   *   - Match 50 @ $150.05 (best ask)
   *   - Match 30 @ $150.08 (next ask)
   *   - Stop at $150.12 (too expensive)
   *   - Remaining: 20 shares
   * 
   * LIKELY MACRO:
   * - Branch prediction hint: Usually no match (passive order)
   * - Optimizes common case (add to book)
   * - Uncommon: Aggressive order crosses spread
   * 
   * PERFORMANCE:
   * - No match: 5-10 ns (just checks, no matching)
   * - Match 1 order: 50-100 ns
   * - Match N orders: N * 50-100 ns
   * 
   * Returns: leaves_qty (remaining quantity after matching)
   * 
   * Parameters:
   * - client_id: Order's client
   * - client_order_id: Client's order ID
   * - ticker_id: Instrument
   * - side: BUY or SELL
   * - price: Limit price
   * - qty: Order quantity
   * - new_market_order_id: Market order ID
   */
  auto MEOrderBook::checkForMatch(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty, Qty new_market_order_id) noexcept {
    auto leaves_qty = qty;  // Start with full quantity

    // BUY ORDER: Match against asks (sell side)
    if (side == Side::BUY) {
      // While quantity remains AND asks exist
      while (leaves_qty && asks_by_price_) {
        // Get best ask (lowest ask price, earliest time)
        const auto ask_itr = asks_by_price_->first_me_order_;
        
        // Check if buy price can match ask price
        // BUY $150.00 cannot match ASK $150.05 (too expensive)
        if (LIKELY(price < ask_itr->price_)) {  // LIKELY = no match (passive order)
          break;  // Stop matching (remaining qty becomes passive)
        }

        // Match: Buy price >= ask price (can execute)
        START_MEASURE(Exchange_MEOrderBook_match);
        match(ticker_id, client_id, side, client_order_id, new_market_order_id, ask_itr, &leaves_qty);
        END_MEASURE(Exchange_MEOrderBook_match, (*logger_));
        
        // leaves_qty updated by match() (reduced by fill_qty)
        // Loop continues if qty remains and more asks exist
      }
    }
    
    // SELL ORDER: Match against bids (buy side)
    if (side == Side::SELL) {
      // While quantity remains AND bids exist
      while (leaves_qty && bids_by_price_) {
        // Get best bid (highest bid price, earliest time)
        const auto bid_itr = bids_by_price_->first_me_order_;
        
        // Check if sell price can match bid price
        // SELL $150.10 cannot match BID $150.00 (too cheap)
        if (LIKELY(price > bid_itr->price_)) {  // LIKELY = no match (passive order)
          break;  // Stop matching
        }

        // Match: Sell price <= bid price (can execute)
        START_MEASURE(Exchange_MEOrderBook_match);
        match(ticker_id, client_id, side, client_order_id, new_market_order_id, bid_itr, &leaves_qty);
        END_MEASURE(Exchange_MEOrderBook_match, (*logger_));
        
        // leaves_qty updated (reduced by match)
      }
    }

    // Return remaining quantity (0 if fully filled, >0 if partially filled or no match)
    return leaves_qty;
  }

  /*
   * ADD - NEW ORDER SUBMISSION
   * ===========================
   * 
   * Primary order book operation: Add new order.
   * 
   * ALGORITHM:
   * 1. Generate unique market_order_id (exchange's ID)
   * 2. Send ACCEPTED response (order received, acknowledged)
   * 3. Check for matches (checkForMatch):
   *    - Match against opposite side (bids vs asks)
   *    - Returns remaining quantity (leaves_qty)
   * 4. If quantity remains (not fully filled):
   *    a. Get time priority (getNextPriority)
   *    b. Allocate order from pool
   *    c. Add to order book (addOrder)
   *    d. Send ADD market update (order added to book)
   * 
   * ACCEPTED RESPONSE:
   * - Sent immediately (before matching)
   * - Confirms order received and valid
   * - Includes: market_order_id (exchange's ID)
   * - Client can cancel using this ID
   * 
   * MATCHING:
   * - checkForMatch: Attempts to match aggressive order
   * - Returns leaves_qty (remaining after matching)
   * - If 0: Fully filled (no passive order needed)
   * - If > 0: Add remainder to book (passive order)
   * 
   * TIME PRIORITY:
   * - getNextPriority: Assigns priority at price level
   * - Lower priority = earlier (FIFO)
   * - Used for matching (first order at price gets filled first)
   * 
   * ADD TO BOOK:
   * - Allocate order from pool (order_pool_.allocate)
   * - Add to data structures (addOrder)
   * - Send ADD market update (broadcast to subscribers)
   * 
   * LIKELY MACRO:
   * - leaves_qty likely > 0 (most orders passive)
   * - Optimizes common case (add to book)
   * - Uncommon: Fully filled immediately
   * 
   * PERFORMANCE:
   * - No match: 20-50 ns (check + add)
   * - With match: 50-500 ns (depends on matches)
   * - Memory: O(1) (pool allocation)
   * 
   * Parameters:
   * - client_id: Which client submitted order
   * - client_order_id: Client's order ID
   * - ticker_id: Instrument (for validation)
   * - side: BUY or SELL
   * - price: Limit price
   * - qty: Order quantity
   */
  auto MEOrderBook::add(ClientId client_id, OrderId client_order_id, TickerId ticker_id, Side side, Price price, Qty qty) noexcept -> void {
    // Generate unique market order ID (exchange's identifier)
    const auto new_market_order_id = generateNewMarketOrderId();
    
    // Send ACCEPTED response (order acknowledged)
    client_response_ = {ClientResponseType::ACCEPTED,  // Type: ACCEPTED
                        client_id, ticker_id,           // Client, instrument
                        client_order_id,                // Client's order ID
                        new_market_order_id,            // Exchange's order ID (assigned)
                        side, price,                    // Order details
                        0,                              // exec_qty: 0 (not filled yet)
                        qty};                           // leaves_qty: Full quantity
    matching_engine_->sendClientResponse(&client_response_);  // Send to client

    // Attempt to match order against opposite side
    START_MEASURE(Exchange_MEOrderBook_checkForMatch);
    const auto leaves_qty = checkForMatch(client_id, client_order_id, ticker_id, side, price, qty, new_market_order_id);
    END_MEASURE(Exchange_MEOrderBook_checkForMatch, (*logger_));

    // If quantity remains after matching (not fully filled)
    if (LIKELY(leaves_qty)) {  // LIKELY = most orders passive (added to book)
      // Get time priority for this price level (FIFO ordering)
      const auto priority = getNextPriority(price);

      // Allocate order from memory pool (O(1))
      auto order = order_pool_.allocate(ticker_id, client_id, client_order_id, new_market_order_id, 
                                        side, price, leaves_qty, priority, 
                                        nullptr, nullptr);  // prev/next set by addOrder
      
      // Add order to order book data structures
      START_MEASURE(Exchange_MEOrderBook_addOrder);
      addOrder(order);  // Add to FIFO queue at price, update hash maps
      END_MEASURE(Exchange_MEOrderBook_addOrder, (*logger_));

      // Send ADD market update (broadcast order added to book)
      market_update_ = {MarketUpdateType::ADD,         // Type: ADD (new passive order)
                        new_market_order_id,            // Order ID
                        ticker_id, side, price,         // Order details
                        leaves_qty,                     // Quantity (remaining)
                        priority};                      // Time priority
      matching_engine_->sendMarketUpdate(&market_update_);  // Broadcast
    }
  }

  /*
   * CANCEL - REMOVE ORDER FROM BOOK
   * ================================
   * 
   * Cancel existing order from order book.
   * 
   * ALGORITHM:
   * 1. Validate client_id (within bounds)
   * 2. Lookup order by client_id + order_id (hash map)
   * 3. If order exists:
   *    a. Send CANCELED response (success)
   *    b. Send CANCEL market update (broadcast removal)
   *    c. Remove order from book (removeOrder)
   * 4. If order doesn't exist:
   *    a. Send CANCEL_REJECTED response (failure)
   * 
   * VALIDATION:
   * - client_id < cid_oid_to_order_.size(): Valid client ID
   * - cid_oid_to_order_[client_id][order_id] != nullptr: Order exists
   * 
   * REJECTION REASONS:
   * - Invalid client_id (out of range)
   * - Order doesn't exist (already filled, or never existed)
   * - Wrong client_id (order belongs to different client)
   * 
   * RACE CONDITION:
   * - Client sends cancel while order being filled
   * - Possible: Order fills before cancel processed
   * - Result: CANCEL_REJECTED (order no longer exists)
   * - Client should expect this (valid scenario)
   * 
   * RESPONSES:
   * - CANCELED: Success (order removed)
   *   - Includes: market_order_id, side, price, remaining qty
   * - CANCEL_REJECTED: Failure (order not found)
   *   - Includes: INVALID values (order doesn't exist)
   * 
   * MARKET UPDATE:
   * - CANCEL: Broadcast order removal
   * - Allows subscribers to update their order books
   * 
   * PERFORMANCE:
   * - O(1) lookup (hash map)
   * - O(1) removal (doubly-linked list)
   * - Total: 20-50 ns
   * 
   * LIKELY/UNLIKELY MACROS:
   * - LIKELY(is_cancelable): Most cancels succeed
   * - UNLIKELY(!is_cancelable): Rejections uncommon
   * - Optimizes common case (successful cancel)
   * 
   * Parameters:
   * - client_id: Which client submitted cancel
   * - order_id: Order to cancel (client's ID)
   * - ticker_id: Instrument (for validation)
   */
  auto MEOrderBook::cancel(ClientId client_id, OrderId order_id, TickerId ticker_id) noexcept -> void {
    // Validate client ID (within bounds)
    auto is_cancelable = (client_id < cid_oid_to_order_.size());
    MEOrder *exchange_order = nullptr;
    
    // If valid client ID, lookup order
    if (LIKELY(is_cancelable)) {  // LIKELY = valid client
      // Get client's order map
      auto &co_itr = cid_oid_to_order_.at(client_id);
      // Lookup order by order ID
      exchange_order = co_itr.at(order_id);
      // Check if order exists (not nullptr)
      is_cancelable = (exchange_order != nullptr);
    }

    // Generate appropriate response based on whether order found
    if (UNLIKELY(!is_cancelable)) {  // UNLIKELY = cancel rejection uncommon
      // Order not found: Send CANCEL_REJECTED response
      client_response_ = {ClientResponseType::CANCEL_REJECTED,  // Type: CANCEL_REJECTED
                          client_id, ticker_id,                  // Client, instrument
                          order_id,                              // Client's order ID
                          OrderId_INVALID,                       // No market order ID (doesn't exist)
                          Side::INVALID,                         // No side
                          Price_INVALID,                         // No price
                          Qty_INVALID,                           // No executed qty
                          Qty_INVALID};                          // No remaining qty
    } else {
      // Order found: Send CANCELED response
      client_response_ = {ClientResponseType::CANCELED,             // Type: CANCELED (success)
                          client_id, ticker_id,                     // Client, instrument
                          order_id,                                 // Client's order ID
                          exchange_order->market_order_id_,         // Exchange's order ID
                          exchange_order->side_,                    // Order side
                          exchange_order->price_,                   // Order price
                          Qty_INVALID,                              // No executed qty (cancel, not fill)
                          exchange_order->qty_};                    // Remaining qty (being canceled)
      
      // Generate CANCEL market update (broadcast removal)
      market_update_ = {MarketUpdateType::CANCEL,              // Type: CANCEL
                        exchange_order->market_order_id_,       // Order ID
                        ticker_id,                              // Instrument
                        exchange_order->side_,                  // Side
                        exchange_order->price_,                 // Price
                        0,                                      // Quantity (0 for cancel)
                        exchange_order->priority_};             // Priority

      // Remove order from order book
      START_MEASURE(Exchange_MEOrderBook_removeOrder);
      removeOrder(exchange_order);  // O(1) removal
      END_MEASURE(Exchange_MEOrderBook_removeOrder, (*logger_));

      // Send CANCEL market update (broadcast)
      matching_engine_->sendMarketUpdate(&market_update_);
    }

    // Send response to client (CANCELED or CANCEL_REJECTED)
    matching_engine_->sendClientResponse(&client_response_);
  }

  /*
   * TO STRING - ORDER BOOK VISUALIZATION
   * =====================================
   * 
   * Generates human-readable order book representation.
   * Shows all price levels and orders (optionally).
   * 
   * ALGORITHM:
   * 1. Print header (ticker ID)
   * 2. Print asks (sell side):
   *    - Iterate from best ask (lowest price) to worst
   *    - For each price level:
   *      - Print price, total qty, number of orders
   *      - Optionally: Print each individual order
   *    - Validate: Prices ascending (if validity_check)
   * 3. Print separator ("X" = spread)
   * 4. Print bids (buy side):
   *    - Iterate from best bid (highest price) to worst
   *    - For each price level: Same as asks
   *    - Validate: Prices descending (if validity_check)
   * 
   * LAMBDA PRINTER:
   * - printer: Local lambda function (captures ss, time_str)
   * - Parameters: stringstream, price level, side, last_price, sanity_check
   * - Purpose: Reusable printing logic (DRY principle)
   * - Computes: Total qty, number of orders at price
   * - Formats: Price level info, individual orders
   * 
   * OUTPUT FORMAT (SUMMARY):
   * ```
   * Ticker:AAPL
   * ASKS L:0 => <px:150.05 p:150.10 n:150.05> 150.05 @ 300  (2)
   * ASKS L:1 => <px:150.10 p:150.05 n:150.15> 150.10 @ 800  (4)
   * 
   *                           X
   * 
   * BIDS L:0 => <px:150.00 p:149.95 n:150.00> 150.00 @ 500  (3)
   * BIDS L:1 => <px:149.95 p:149.90 n:150.00> 149.95 @ 1000 (5)
   * ```
   * 
   * OUTPUT FORMAT (DETAILED):
   * - Includes individual orders at each price:
   * - [oid:9876543 q:100 p:9876542 n:9876544]
   * - Shows: order ID, qty, prev order, next order (FIFO queue)
   * 
   * VALIDITY CHECK:
   * - sanity_check = true: Validate price ordering
   * - ASKS: Prices must be ascending (lowest first)
   * - BIDS: Prices must be descending (highest first)
   * - FATAL if violated: Data structure corrupted
   * 
   * CIRCULAR LIST TRAVERSAL:
   * - Start: asks_by_price_ or bids_by_price_
   * - Iterate: next_entry_ until wrap around
   * - Stop: When next_entry_ == start (circular)
   * - nullptr: No more price levels
   * 
   * PERFORMANCE:
   * - Not performance-critical (debugging only)
   * - O(P * O) where P = price levels, O = orders per level
   * - Typical: 10-100 price levels, 1-10 orders per level
   * - Time: 1-10 ms (acceptable for debugging)
   * 
   * USE CASES:
   * - Debugging: Visualize order book state
   * - Testing: Verify correctness
   * - Monitoring: Display live order book
   * - Audit: Log order book state periodically
   * 
   * Parameters:
   * - detailed: Include individual orders (not just totals)
   * - validity_check: Validate price ordering (data integrity)
   * 
   * Returns: String representation of order book
   */
  auto MEOrderBook::toString(bool detailed, bool validity_check) const -> std::string {
    std::stringstream ss;
    std::string time_str;

    // Lambda function to print price level info
    // Captures: ss, time_str (by reference)
    // Parameters: stringstream, price level, side, last_price, sanity_check
    auto printer = [&](std::stringstream &ss, MEOrdersAtPrice *itr, Side side, Price &last_price, bool sanity_check) {
      char buf[4096];    // Formatted output buffer
      Qty qty = 0;        // Total quantity at this price
      size_t num_orders = 0;  // Number of orders at this price

      // Iterate all orders at this price level (circular list)
      for (auto o_itr = itr->first_me_order_;; o_itr = o_itr->next_order_) {
        qty += o_itr->qty_;      // Sum quantities
        ++num_orders;            // Count orders
        // Stop when wrapped around (circular list)
        if (o_itr->next_order_ == itr->first_me_order_)
          break;
      }
      
      // Format price level summary
      // <px:price p:prev n:next> price @ total_qty (num_orders)
      sprintf(buf, " <px:%3s p:%3s n:%3s> %-3s @ %-5s(%-4s)",
              priceToString(itr->price_).c_str(),             // Current price
              priceToString(itr->prev_entry_->price_).c_str(), // Previous price level
              priceToString(itr->next_entry_->price_).c_str(), // Next price level
              priceToString(itr->price_).c_str(),             // Price (repeated)
              qtyToString(qty).c_str(),                        // Total quantity
              std::to_string(num_orders).c_str());             // Number of orders
      ss << buf;
      
      // If detailed, print individual orders
      for (auto o_itr = itr->first_me_order_;; o_itr = o_itr->next_order_) {
        if (detailed) {
          // Format individual order: [oid:X q:Y p:prev n:next]
          sprintf(buf, "[oid:%s q:%s p:%s n:%s] ",
                  orderIdToString(o_itr->market_order_id_).c_str(),  // Order ID
                  qtyToString(o_itr->qty_).c_str(),                   // Quantity
                  orderIdToString(o_itr->prev_order_ ? o_itr->prev_order_->market_order_id_ : OrderId_INVALID).c_str(),  // Prev order
                  orderIdToString(o_itr->next_order_ ? o_itr->next_order_->market_order_id_ : OrderId_INVALID).c_str()); // Next order
          ss << buf;
        }
        // Stop when wrapped around
        if (o_itr->next_order_ == itr->first_me_order_)
          break;
      }

      ss << std::endl;  // End price level line

      // Validate price ordering (data integrity check)
      if (sanity_check) {
        // SELL: Prices must be ascending (last < current)
        // BUY: Prices must be descending (last > current)
        if ((side == Side::SELL && last_price >= itr->price_) || 
            (side == Side::BUY && last_price <= itr->price_)) {
          FATAL("Bids/Asks not sorted by ascending/descending prices last:" + 
                priceToString(last_price) + " itr:" + itr->toString());
        }
        last_price = itr->price_;  // Update for next iteration
      }
    };

    // Print header (instrument)
    ss << "Ticker:" << tickerIdToString(ticker_id_) << std::endl;
    
    // Print ASKS (sell side) - ascending prices
    {
      auto ask_itr = asks_by_price_;  // Start at best ask (lowest price)
      auto last_ask_price = std::numeric_limits<Price>::min();  // Initialize for validation
      
      // Iterate all ask price levels
      for (size_t count = 0; ask_itr; ++count) {
        ss << "ASKS L:" << count << " => ";  // Level number
        
        // Get next price level (or nullptr if wrapped around)
        auto next_ask_itr = (ask_itr->next_entry_ == asks_by_price_ ? nullptr : ask_itr->next_entry_);
        
        // Print this price level
        printer(ss, ask_itr, Side::SELL, last_ask_price, validity_check);
        
        // Move to next price level
        ask_itr = next_ask_itr;
      }
    }

    // Print spread separator
    ss << std::endl << "                          X" << std::endl << std::endl;

    // Print BIDS (buy side) - descending prices
    {
      auto bid_itr = bids_by_price_;  // Start at best bid (highest price)
      auto last_bid_price = std::numeric_limits<Price>::max();  // Initialize for validation
      
      // Iterate all bid price levels
      for (size_t count = 0; bid_itr; ++count) {
        ss << "BIDS L:" << count << " => ";  // Level number
        
        // Get next price level (or nullptr if wrapped around)
        auto next_bid_itr = (bid_itr->next_entry_ == bids_by_price_ ? nullptr : bid_itr->next_entry_);
        
        // Print this price level
        printer(ss, bid_itr, Side::BUY, last_bid_price, validity_check);
        
        // Move to next price level
        bid_itr = next_bid_itr;
      }
    }

    return ss.str();  // Return formatted string
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. MATCHING ALGORITHM:
 *    - Price-time priority: Best price first, then FIFO
 *    - Aggressive order: Crosses spread (matches immediately)
 *    - Passive order: Doesn't cross (added to book)
 *    - Sweep the book: Match multiple price levels if needed
 * 
 * 2. RESPONSE GENERATION:
 *    - ACCEPTED: Immediate (before matching)
 *    - FILLED: After each match (aggressive + passive)
 *    - CANCELED: On successful cancel
 *    - CANCEL_REJECTED: On failed cancel
 * 
 * 3. MARKET UPDATES:
 *    - TRADE: Execution occurred
 *    - ADD: Order added to book
 *    - MODIFY: Order quantity changed (partial fill)
 *    - CANCEL: Order removed (full fill or cancel)
 * 
 * 4. PERFORMANCE OPTIMIZATION:
 *    - Reuse member variables (client_response_, market_update_)
 *    - Avoid allocation: responses/updates reused
 *    - Memory pools: O(1) order allocation/deallocation
 *    - LIKELY/UNLIKELY: Branch prediction hints
 *    - noexcept: No exception overhead
 * 
 * 5. DATA INTEGRITY:
 *    - toString with validity_check: Verify price ordering
 *    - Used in destructor: Final state check
 *    - Catches bugs: Data structure corruption
 * 
 * LATENCY BREAKDOWN (ADD ORDER):
 * - Generate market_order_id: 1-2 ns
 * - Send ACCEPTED: 10-20 ns (queue write)
 * - checkForMatch (no match): 5-10 ns
 * - getNextPriority: 5-10 ns
 * - Allocate order: 10-20 ns (pool)
 * - addOrder: 20-50 ns (linked list ops)
 * - Send ADD update: 10-20 ns (queue write)
 * - TOTAL: 60-150 ns (no matching)
 * 
 * LATENCY BREAKDOWN (MATCH ORDER):
 * - Calculate fill_qty: 1-2 ns
 * - Update quantities: 2-5 ns
 * - Send FILLED (2x): 20-40 ns
 * - Send TRADE: 10-20 ns
 * - removeOrder or MODIFY: 20-50 ns
 * - TOTAL: 50-120 ns per match
 * 
 * LATENCY BREAKDOWN (CANCEL ORDER):
 * - Lookup order: 5-10 ns (hash map)
 * - Send CANCELED: 10-20 ns
 * - Send CANCEL update: 10-20 ns
 * - removeOrder: 20-50 ns
 * - TOTAL: 45-100 ns
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Self-trade prevention: Block client from trading with itself
 * - Order types: Market, stop, IOC, FOK
 * - Time-in-force: DAY, GTC, IOC
 * - Price bands: Reject orders outside range (fat finger protection)
 * - Kill switch: Emergency halt
 * - Pre-trade risk: Position limits, order size limits
 * - Post-trade reporting: Regulatory (MiFID II, CAT)
 * - Graceful degradation: Continue on non-critical errors
 */
