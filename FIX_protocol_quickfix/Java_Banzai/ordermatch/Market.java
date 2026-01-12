// ============================================================================
// MARKET - Single-Symbol Order Matching Engine
// ============================================================================
// Implements a simple order book for a single trading symbol.
// Maintains separate bid and ask lists, matches orders using price-time priority.
//
// PURPOSE:
// - Maintain order book (bids and asks) for one symbol
// - Match buy orders against sell orders
// - Execute trades when prices cross
// - Implement price-time priority matching
//
// ORDER BOOK STRUCTURE:
// BIDS (Buy orders - highest price first):
//   $150.10 x 200  [best bid]
//   $150.00 x 500
//   $149.90 x 300
//
// ASKS (Sell orders - lowest price first):
//   $150.20 x 400  [best ask]
//   $150.30 x 600
//   $150.40 x 100
//
// MATCHING LOGIC:
// Orders match when bid price >= ask price
// - BUY 100 @ $150.30 crosses ASK @ $150.20 → TRADE @ $150.20
// - BUY 100 @ $150.10 vs ASK @ $150.20 → No trade (no cross)
//
// MARKET ORDERS:
// - MARKET orders have highest priority
// - Execute at counterparty's limit price
// - BUY MARKET executes at best ask price
// - SELL MARKET executes at best bid price
//
// PRICE-TIME PRIORITY:
// 1. Better price executes first
// 2. At same price, earlier time executes first
//
// EXAMPLE MATCHING SEQUENCE:
// Initial state:
//   BIDS: $150.00 x 500
//   ASKS: $150.20 x 400
//
// New order: BUY 300 @ $150.25
//   → Crosses with ASK $150.20 x 400
//   → Trade: 300 @ $150.20
//   → Result BIDS: $150.00 x 500
//           ASKS: $150.20 x 100 (remaining)
//
// COMPARISON TO C++ VERSION:
// Same matching algorithm but Java implementation:
// - ArrayList instead of std::list
// - No iterators, use indexed access
// - Simpler price comparison (no BigDecimal needed here)
//
// PRODUCTION CONSIDERATIONS:
// Real matching engines would add:
// - Order book depth (multiple price levels with aggregation)
// - Stop orders and conditional orders
// - Iceberg orders (hidden quantity)
// - Minimum quantity and fill-or-kill
// - Auction matching (opening/closing crosses)
// - Book depth snapshot publishing
// - Fast data structures (trees, priority queues)
// ============================================================================

package quickfix.examples.ordermatch;

import quickfix.field.OrdType;
import quickfix.field.Side;

import java.text.DecimalFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

public class Market {
    // ========================================================================
    // ORDER BOOK DATA STRUCTURES
    // ========================================================================
    // Separate lists for buy and sell orders
    // Orders sorted by price-time priority within each list
    
    private final List<Order> bidOrders = new ArrayList<>();  // Buy orders
    private final List<Order> askOrders = new ArrayList<>();  // Sell orders

    // ========================================================================
    // MAIN MATCHING ALGORITHM
    // ========================================================================
    /**
     * Matches orders in the book, executing trades where prices cross.
     * 
     * ALGORITHM:
     * 1. Loop continuously while orders can match
     * 2. Check if both bid and ask lists non-empty
     * 3. Get best bid (highest price) and best ask (lowest price)
     * 4. Check if prices cross (bid >= ask) or MARKET order present
     * 5. If crossing: execute trade, add to output list, remove if filled
     * 6. If not crossing: stop matching
     * 7. Repeat until no more matches possible
     * 
     * MATCHING CONDITIONS:
     * Trade occurs if any of these true:
     * - Bid is MARKET order (will pay any price)
     * - Ask is MARKET order (will accept any price)
     * - Bid price >= Ask price (prices cross)
     * 
     * EXAMPLE EXECUTION:
     * BIDS: $150.30 x 200, $150.20 x 500
     * ASKS: $150.20 x 300, $150.40 x 400
     * 
     * Iteration 1:
     * - bestBid=$150.30, bestAsk=$150.20
     * - 150.30 >= 150.20 → MATCH
     * - Trade 200 @ $150.20 (ask's price, passive side)
     * - Remove fully filled bid
     * 
     * Iteration 2:
     * - bestBid=$150.20, bestAsk=$150.20 (100 remaining)
     * - 150.20 >= 150.20 → MATCH
     * - Trade 100 @ $150.20
     * - Remove fully filled ask
     * 
     * Iteration 3:
     * - bestBid=$150.20 (400 remaining), bestAsk=$150.40
     * - 150.20 < 150.40 → NO MATCH
     * - Stop matching
     * 
     * OUTPUT PARAMETER:
     * orders list populated with Order objects that executed.
     * Used to send ExecutionReport messages for each filled order.
     * 
     * @param symbol Symbol being matched (for logging)
     * @param orders Output: list of orders that executed
     * @return true if any orders were matched
     */
    public boolean match(String symbol, List<Order> orders) {
        while (true) {
            // ================================================================
            // CHECK IF MATCHING POSSIBLE
            // ================================================================
            // Need at least one bid and one ask to match
            if (bidOrders.size() == 0 || askOrders.size() == 0) {
                return orders.size() != 0;
            }
            
            // ================================================================
            // GET BEST BID AND ASK
            // ================================================================
            // Both lists sorted, so index 0 is best price
            // Best bid = highest price (most aggressive buyer)
            // Best ask = lowest price (most aggressive seller)
            Order bidOrder = bidOrders.get(0);
            Order askOrder = askOrders.get(0);
            
            // ================================================================
            // CHECK IF PRICES CROSS
            // ================================================================
            // Trade conditions:
            // 1. MARKET order on either side (always executes)
            // 2. Bid price >= Ask price (prices overlap)
            if (bidOrder.getType() == OrdType.MARKET 
                    || askOrder.getType() == OrdType.MARKET
                    || (bidOrder.getPrice() >= askOrder.getPrice())) {
                
                // ============================================================
                // EXECUTE TRADE
                // ============================================================
                match(bidOrder, askOrder);
                
                // ============================================================
                // ADD TO OUTPUT LIST
                // ============================================================
                // Track which orders executed (need to send ExecutionReports)
                // Avoid duplicates if order partially fills multiple times
                if (!orders.contains(bidOrder)) {
                    orders.add(0, bidOrder);
                }
                if (!orders.contains(askOrder)) {
                    orders.add(0, askOrder);
                }

                // ============================================================
                // REMOVE FULLY FILLED ORDERS
                // ============================================================
                // Orders with openQuantity=0 are removed from book
                if (bidOrder.isClosed()) {
                    bidOrders.remove(bidOrder);
                }
                if (askOrder.isClosed()) {
                    askOrders.remove(askOrder);
                }
            } else {
                // No crossing - matching complete
                return orders.size() != 0;
            }
        }
    }

    // ========================================================================
    // TRADE EXECUTION
    // ========================================================================
    /**
     * Executes trade between bid and ask orders.
     * 
     * PRICE DETERMINATION:
     * Uses passive side's price (price-time priority):
     * - If ask is LIMIT: use ask price (passive)
     * - If ask is MARKET: use bid price (bid was passive)
     * 
     * RATIONALE:
     * Passive order (resting in book) gets price improvement.
     * Aggressive order (incoming) crosses the spread.
     * 
     * EXAMPLES:
     * 1. Resting ASK @ $150.20, incoming BUY @ $150.30
     *    → Trade @ $150.20 (seller's price, was passive)
     * 
     * 2. Resting BID @ $150.10, incoming SELL MARKET
     *    → Trade @ $150.10 (buyer's price, was passive)
     * 
     * QUANTITY DETERMINATION:
     * Trade quantity = min(bid open qty, ask open qty)
     * Smaller order fills completely, larger order partially fills.
     * 
     * EXAMPLE:
     * BID 500 vs ASK 300 → trade 300 (ask fills completely)
     * BID 200 vs ASK 500 → trade 200 (bid fills completely)
     * 
     * EXECUTION:
     * Calls Order.execute() on both sides to:
     * - Update open/executed quantities
     * - Calculate average execution price
     * - Record last fill details
     * 
     * @param bid Buy order
     * @param ask Sell order
     */
    private void match(Order bid, Order ask) {
        // Determine execution price (passive side gets their price)
        double price = ask.getType() == OrdType.LIMIT ? ask.getPrice() : bid.getPrice();
        
        // Determine execution quantity (min of both open quantities)
        long quantity = bid.getOpenQuantity() >= ask.getOpenQuantity() 
            ? ask.getOpenQuantity() 
            : bid.getOpenQuantity();

        // Execute both sides of the trade
        bid.execute(price, quantity);
        ask.execute(price, quantity);
    }

    // ========================================================================
    // ORDER INSERTION
    // ========================================================================
    /**
     * Inserts order into appropriate book (bid or ask).
     * 
     * ROUTING:
     * - BUY orders → bid book (descending price sort)
     * - SELL orders → ask book (ascending price sort)
     * 
     * @param order Order to insert
     * @return true if inserted successfully
     */
    public boolean insert(Order order) {
        return order.getSide() == Side.BUY 
            ? insert(order, true, bidOrders)   // Bids: high to low
            : insert(order, false, askOrders); // Asks: low to high
    }

    /**
     * Inserts order into list with price-time priority.
     * 
     * PRICE-TIME PRIORITY:
     * Orders sorted by:
     * 1. Price (best price first)
     * 2. Time (earlier orders first at same price)
     * 
     * BID ORDERING (descending=true):
     * - Higher prices first (most aggressive buyers)
     * - Example: $150.30, $150.20, $150.10
     * 
     * ASK ORDERING (descending=false):
     * - Lower prices first (most aggressive sellers)
     * - Example: $150.20, $150.30, $150.40
     * 
     * MARKET ORDER PRIORITY:
     * MARKET orders always inserted at index 0 (highest priority).
     * Execute before any LIMIT orders.
     * 
     * INSERTION ALGORITHM:
     * 1. If empty list: append order
     * 2. If MARKET order: insert at index 0 (front)
     * 3. If LIMIT order: find correct price-time position
     *    - Iterate through list
     *    - If better price AND earlier time: insert here
     *    - Otherwise continue
     * 4. If no position found: append at end
     * 
     * BUG NOTE:
     * The condition on line 64-65 has a logic issue:
     * Uses AND instead of OR for price/time comparison.
     * Should be: better price OR (same price AND earlier time)
     * Current code only inserts if BOTH better price AND earlier time.
     * This causes orders to often append at end instead of proper position.
     * 
     * CORRECT LOGIC WOULD BE:
     * if ((descending ? order.getPrice() > o.getPrice() : order.getPrice() < o.getPrice())
     *     || (order.getPrice() == o.getPrice() && order.getEntryTime() < o.getEntryTime()))
     * 
     * @param order Order to insert
     * @param descending true for bids (high→low), false for asks (low→high)
     * @param orders List to insert into
     * @return true always
     */
    private boolean insert(Order order, boolean descending, List<Order> orders) {
        if (orders.size() == 0) {
            // Empty list - just add
            orders.add(order);
        } else if (order.getType() == OrdType.MARKET) {
            // MARKET orders go to front (highest priority)
            orders.add(0, order);
        } else {
            // LIMIT order - find price-time position
            for (int i = 0; i < orders.size(); i++) {
                Order o = orders.get(i);
                // Check if this order should go before order 'o'
                // NOTE: Logic issue - see comment above
                if ((descending ? order.getPrice() > o.getPrice() : order.getPrice() < o.getPrice())
                        && order.getEntryTime() < o.getEntryTime()) {
                    orders.add(i, order);
                }
            }
            // If not inserted yet, append at end
            orders.add(order);
        }
        return true;
    }

    // ========================================================================
    // ORDER REMOVAL
    // ========================================================================
    /**
     * Removes order from appropriate book.
     * 
     * USAGE:
     * - Order cancelled by client
     * - Order fully filled (automatic during matching)
     * 
     * PROCESS:
     * 1. Determine which book (bid or ask) based on side
     * 2. Find order by client order ID
     * 3. Remove from list
     * 
     * @param order Order to remove
     */
    public void erase(Order order) {
        if (order.getSide() == Side.BUY) {
            bidOrders.remove(find(bidOrders, order.getClientOrderId()));
        } else {
            askOrders.remove(find(askOrders, order.getClientOrderId()));
        }
    }

    // ========================================================================
    // ORDER LOOKUP
    // ========================================================================
    
    /**
     * Finds order by symbol, side, and client order ID.
     * 
     * PARAMETERS:
     * @param symbol Symbol (unused, Market only handles one symbol)
     * @param side BUY or SELL (determines which book to search)
     * @param id Client order ID to find
     * @return Order if found, null otherwise
     */
    public Order find(String symbol, char side, String id) {
        return find(side == Side.BUY ? bidOrders : askOrders, id);
    }

    /**
     * Finds order in list by client order ID.
     * 
     * SEARCH:
     * Linear search through list (O(n)).
     * Production systems would use HashMap for O(1) lookup.
     * 
     * @param orders List to search
     * @param clientOrderId Client order ID
     * @return Order if found, null otherwise
     */
    private Order find(List<Order> orders, String clientOrderId) {
        for (Order order : orders) {
            if (order.getClientOrderId().equals(clientOrderId)) {
                return order;
            }
        }
        return null;
    }

    // ========================================================================
    // MARKET DATA DISPLAY
    // ========================================================================
    
    /**
     * Displays current order book state.
     * 
     * OUTPUT FORMAT:
     * BIDS:
     * ----
     *   $150.30 200 CLIENT1 Mon Jan 13 12:34:56 PST 2026
     *   $150.20 500 CLIENT2 Mon Jan 13 12:35:10 PST 2026
     * ASKS:
     * ----
     *   $150.40 400 CLIENT3 Mon Jan 13 12:35:20 PST 2026
     *   $150.50 300 CLIENT4 Mon Jan 13 12:35:30 PST 2026
     * 
     * Shows:
     * - Price (formatted to 2 decimals)
     * - Open quantity (remaining)
     * - Owner (SenderCompID)
     * - Entry time (when order entered system)
     */
    public void display() {
        displaySide(bidOrders, "BIDS");
        displaySide(askOrders, "ASKS");
    }

    /**
     * Displays one side of the order book.
     * 
     * FORMAT:
     * Uses DecimalFormat for consistent price/quantity display:
     * - Price: #.00 (always 2 decimals: $150.20)
     * - Quantity: ###### (right-aligned integer)
     * 
     * @param orders List of orders to display
     * @param title "BIDS" or "ASKS"
     */
    private void displaySide(List<Order> orders, String title) {
        DecimalFormat priceFormat = new DecimalFormat("#.00");
        DecimalFormat qtyFormat = new DecimalFormat("######");
        
        System.out.println(title + ":\n----");
        for (Order order : orders) {
            System.out.println("  $" + priceFormat.format(order.getPrice()) + " "
                    + qtyFormat.format(order.getOpenQuantity()) + " " 
                    + order.getOwner() + " "
                    + new Date(order.getEntryTime()));
        }
    }
}
