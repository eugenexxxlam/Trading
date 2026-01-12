// ============================================================================
// ORDER - Trading Order Representation
// ============================================================================
// Represents a single trading order with all its attributes and execution state.
//
// PURPOSE:
// - Store order details (symbol, side, price, quantity)
// - Track execution state (fills, open quantity)
// - Calculate average execution price
// - Manage order lifecycle (new → partial fill → filled/cancelled)
//
// ORDER STATES:
// - New: Just created, not yet executed
// - Partially Filled: Some quantity executed, some remaining open
// - Filled: Entire quantity executed (openQuantity = 0)
// - Cancelled: Order cancelled (openQuantity = 0)
// - Closed: Either filled or cancelled (openQuantity = 0)
//
// KEY CONCEPTS:
// - quantity: Total order quantity (never changes)
// - openQuantity: Remaining quantity that can still execute
// - executedQuantity: Total quantity that has executed
// - avgExecutedPrice: Volume-weighted average price of all fills
//
// PARTIAL FILL EXAMPLE:
// Order: BUY 1000 @ $50.00
// Fill 1: 300 @ $50.00 → open=700, executed=300, avgPx=$50.00
// Fill 2: 500 @ $50.10 → open=200, executed=800, avgPx=$50.06
// Fill 3: 200 @ $49.90 → open=0,   executed=1000, avgPx=$50.04
//
// COMPARISON TO C++ VERSION:
// Same concept but Java implementation:
// - No explicit copy constructor (implements Cloneable if needed)
// - Uses System.currentTimeMillis() for timestamp
// - Immutable fields (final) for thread safety
// ============================================================================

package quickfix.examples.ordermatch;

import quickfix.field.Side;

public class Order {
    // ========================================================================
    // IMMUTABLE ORDER ATTRIBUTES
    // ========================================================================
    // These fields never change after order creation
    
    private final long entryTime;         // Timestamp when order entered system
    private final String clientOrderId;   // Client's ID (ClOrdID from FIX)
    private final String symbol;          // Trading instrument (e.g., "AAPL")
    private final String owner;           // SenderCompID from FIX
    private final String target;          // TargetCompID from FIX
    private final char side;              // BUY or SELL
    private final char type;              // MARKET or LIMIT
    private final double price;           // Limit price (0 for market orders)
    private final long quantity;          // Total order quantity
    
    // ========================================================================
    // MUTABLE EXECUTION STATE
    // ========================================================================
    // These fields change as order executes
    
    private long openQuantity;            // Remaining qty (can still execute)
    private long executedQuantity;        // Total qty executed so far
    private double avgExecutedPrice;      // Volume-weighted average fill price
    private double lastExecutedPrice;     // Price of most recent fill
    private long lastExecutedQuantity;    // Quantity of most recent fill

    // ========================================================================
    // CONSTRUCTOR - Create New Order
    // ========================================================================
    /**
     * Creates a new order from FIX message fields.
     * 
     * PARAMETERS:
     * @param clientId Client's order ID (ClOrdID from FIX tag 11)
     * @param symbol Trading symbol (Symbol from FIX tag 55)
     * @param owner Order originator (SenderCompID from FIX header)
     * @param target Order destination (TargetCompID from FIX header)
     * @param side BUY or SELL (Side from FIX tag 54)
     * @param type MARKET or LIMIT (OrdType from FIX tag 40)
     * @param price Limit price, 0 for MARKET (Price from FIX tag 44)
     * @param quantity Order quantity (OrderQty from FIX tag 38)
     * 
     * INITIAL STATE:
     * - openQuantity = quantity (entire order open)
     * - executedQuantity = 0 (nothing executed yet)
     * - entryTime = current time (for price-time priority)
     */
    public Order(String clientId, String symbol, String owner, String target, char side, char type,
            double price, long quantity) {
        super();
        this.clientOrderId = clientId;
        this.symbol = symbol;
        this.owner = owner;
        this.target = target;
        this.side = side;
        this.type = type;
        this.price = price;
        this.quantity = quantity;
        this.openQuantity = quantity;      // Initially all quantity is open
        this.entryTime = System.currentTimeMillis();  // Timestamp for ordering
    }

    // ========================================================================
    // GETTERS - Read Order Attributes
    // ========================================================================
    
    /**
     * Gets volume-weighted average execution price.
     * 
     * CALCULATION:
     * avgPx = (fill1.qty * fill1.px + fill2.qty * fill2.px + ...) / totalQty
     * 
     * EXAMPLE:
     * Fill 300 @ $50.00 and 700 @ $50.10:
     * avgPx = (300*50 + 700*50.10) / 1000 = $50.07
     * 
     * USAGE:
     * Sent in ExecutionReport (AvgPx tag 6) to inform client
     * of weighted average fill price across all partial fills.
     */
    public double getAvgExecutedPrice() {
        return avgExecutedPrice;
    }

    /**
     * Gets client's order ID (ClOrdID).
     * Used to correlate ExecutionReports back to client's original order.
     */
    public String getClientOrderId() {
        return clientOrderId;
    }

    /**
     * Gets total executed quantity so far.
     * For fully filled order: executedQuantity == quantity
     */
    public long getExecutedQuantity() {
        return executedQuantity;
    }

    /**
     * Gets quantity from most recent fill.
     * Used in ExecutionReport (LastQty tag 32).
     */
    public long getLastExecutedQuantity() {
        return lastExecutedQuantity;
    }

    /**
     * Gets remaining open quantity.
     * 
     * STATES:
     * - openQuantity == quantity: No fills yet
     * - 0 < openQuantity < quantity: Partially filled
     * - openQuantity == 0: Fully filled or cancelled
     * 
     * Used in ExecutionReport (LeavesQty tag 151).
     */
    public long getOpenQuantity() {
        return openQuantity;
    }

    /**
     * Gets order owner (SenderCompID).
     * Identifies which client submitted this order.
     */
    public String getOwner() {
        return owner;
    }

    /**
     * Gets limit price.
     * For MARKET orders, this is 0.
     */
    public double getPrice() {
        return price;
    }

    /**
     * Gets total order quantity (never changes).
     */
    public long getQuantity() {
        return quantity;
    }

    /**
     * Gets order side (BUY or SELL).
     * Determines which order book (bids/asks) order goes into.
     */
    public char getSide() {
        return side;
    }

    /**
     * Gets trading symbol (e.g., "AAPL", "MSFT").
     */
    public String getSymbol() {
        return symbol;
    }

    /**
     * Gets order target (TargetCompID).
     * Identifies destination for order (usually exchange).
     */
    public String getTarget() {
        return target;
    }

    /**
     * Gets order type (MARKET or LIMIT).
     */
    public char getType() {
        return type;
    }

    /**
     * Gets order entry timestamp.
     * Used for price-time priority: earlier orders have priority
     * at the same price level.
     */
    public long getEntryTime() {
        return entryTime;
    }

    /**
     * Gets price from most recent fill.
     * Used in ExecutionReport (LastPx tag 31).
     */
    public double getLastExecutedPrice() {
        return lastExecutedPrice;
    }

    // ========================================================================
    // ORDER STATE CHECKS
    // ========================================================================
    
    /**
     * Checks if order is fully filled.
     * 
     * FILLED CONDITION:
     * All original quantity has been executed.
     * 
     * NOTE: This checks quantity == executedQuantity
     * which is equivalent to openQuantity == 0 for filled orders.
     * 
     * @return true if completely filled
     */
    public boolean isFilled() {
        return quantity == executedQuantity;
    }

    /**
     * Checks if order is closed (filled or cancelled).
     * 
     * CLOSED CONDITION:
     * No remaining open quantity.
     * 
     * USAGE:
     * Matching engine removes closed orders from order book.
     * Closed orders cannot execute anymore.
     * 
     * @return true if no open quantity remaining
     */
    public boolean isClosed() {
        return openQuantity == 0;
    }

    // ========================================================================
    // ORDER ACTIONS
    // ========================================================================
    
    /**
     * Cancels the order, setting open quantity to 0.
     * 
     * EFFECT:
     * - openQuantity → 0
     * - executedQuantity unchanged (keep existing fills)
     * - Order becomes closed
     * 
     * USAGE:
     * Called when processing OrderCancelRequest.
     * Remaining open quantity is cancelled.
     * Already executed quantity remains executed.
     */
    public void cancel() {
        openQuantity = 0;
    }

    /**
     * Executes order at specified price and quantity.
     * 
     * EXECUTION LOGIC:
     * 1. Calculate new average price (volume-weighted)
     * 2. Reduce open quantity
     * 3. Increase executed quantity
     * 4. Record last fill details
     * 
     * AVERAGE PRICE CALCULATION:
     * newAvg = (newQty * newPx + oldQty * oldAvg) / (newQty + oldQty)
     * 
     * EXAMPLE:
     * Order: BUY 1000, currently filled 300 @ $50.00
     * execute(50.10, 500):
     * - avgExecutedPrice = (500*50.10 + 300*50.00)/(500+300) = $50.06
     * - openQuantity = 1000 - 300 - 500 = 200
     * - executedQuantity = 300 + 500 = 800
     * - lastExecutedPrice = $50.10
     * - lastExecutedQuantity = 500
     * 
     * @param price Fill price
     * @param quantity Fill quantity
     */
    public void execute(double price, long quantity) {
        // Calculate new volume-weighted average price
        avgExecutedPrice = ((quantity * price) + (avgExecutedPrice * executedQuantity))
                / (quantity + executedQuantity);

        // Update quantities
        openQuantity -= quantity;
        executedQuantity += quantity;
        
        // Record last fill details (for ExecutionReport)
        lastExecutedPrice = price;
        lastExecutedQuantity = quantity;
    }

    // ========================================================================
    // DISPLAY
    // ========================================================================
    
    /**
     * Returns human-readable order summary.
     * 
     * FORMAT:
     * "BUY 1000@$50.00 (800)"
     * - Side: BUY/SELL
     * - Quantity: Total order quantity
     * - Price: Limit price
     * - Open: Remaining quantity in parentheses
     * 
     * USAGE:
     * For console display and logging.
     * 
     * @return Order summary string
     */
    public String toString() {
        return (side == Side.BUY ? "BUY" : "SELL") + " " + quantity + "@$" + price + " (" + openQuantity + ")";
    }
}
