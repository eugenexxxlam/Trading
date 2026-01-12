// ============================================================================
// ORDER - Trading Order Data Model
// ============================================================================
// This class represents a single trading order in the Banzai application.
// It encapsulates all order properties and state throughout the order lifecycle.
//
// PURPOSE:
// - Store order parameters (symbol, quantity, price, etc.)
// - Track order state (new, filled, canceled, rejected)
// - Track execution progress (open quantity, executed quantity, avg price)
// - Support order modifications (cancel/replace)
// - Integrate with Swing table models for GUI display
//
// ORDER LIFECYCLE STATES:
// 1. NEW: Just created, not yet acknowledged by server
// 2. ACKNOWLEDGED: Server accepted order (isNew = false)
// 3. PARTIALLY FILLED: Some quantity executed, some open
// 4. FILLED: Completely executed (open = 0, executed = quantity)
// 5. CANCELED: User canceled (canceled = true, open = 0)
// 6. REJECTED: Server rejected (rejected = true, open = 0)
//
// DESIGN PATTERNS:
// - JavaBean: Standard getter/setter pattern for properties
// - Cloneable: Supports order replacement (clone with new ID)
// - Mutable: State changes as ExecutionReports arrive
//
// USAGE IN BANZAI:
// - Created in OrderEntryPanel when user submits order
// - Stored in OrderTableModel for display in order blotter
// - Updated by BanzaiApplication when ExecutionReports arrive
// - Cloned for cancel/replace operations
// ============================================================================

package quickfix.examples.banzai;

import quickfix.SessionID;

// ============================================================================
// ORDER CLASS - Mutable Order State
// ============================================================================
// Implements Cloneable to support order replacement workflow:
// 1. User clicks "Replace" on existing order
// 2. Order is cloned with new ID
// 3. Clone is modified with new parameters
// 4. OrderCancelReplaceRequest sent with both IDs
// ============================================================================
public class Order implements Cloneable {
    // ========================================================================
    // SESSION AND ROUTING
    // ========================================================================
    
    // FIX session this order belongs to
    // Identifies which connection to use for sending messages
    // Format: BeginString:SenderCompID->TargetCompID
    private SessionID sessionID = null;
    
    // ========================================================================
    // ORDER PARAMETERS - Immutable After Submission
    // ========================================================================
    
    // Trading instrument (e.g., "AAPL", "MSFT", "GOOGL")
    private String symbol = null;
    
    // Total order quantity (number of shares/contracts)
    private int quantity = 0;
    
    // Buy or Sell (or short sell variants)
    private OrderSide side = OrderSide.BUY;
    
    // Order type: MARKET, LIMIT, STOP, STOP_LIMIT
    private OrderType type = OrderType.MARKET;
    
    // Time in force: DAY, GTC, IOC, OPG, GTX
    // Determines how long order remains active
    private OrderTIF tif = OrderTIF.DAY;
    
    // Limit price (for LIMIT and STOP_LIMIT orders)
    // null for MARKET and STOP orders
    private Double limit = null;
    
    // Stop price (for STOP and STOP_LIMIT orders)
    // null for MARKET and LIMIT orders
    private Double stop = null;
    
    // ========================================================================
    // ORDER STATE - Updates Via ExecutionReports
    // ========================================================================
    
    // Quantity still available for execution
    // Decreases as fills occur, becomes 0 when filled or canceled
    private int open = 0;
    
    // Cumulative quantity that has been filled
    // Increases as fills occur, reaches quantity when complete
    private double executed = 0;
    
    // Weighted average price of all fills
    // Example: Fill 100 @ $10, then 50 @ $11 = $10.33 average
    private double avgPx = 0.0;
    
    // ========================================================================
    // ORDER STATUS FLAGS
    // ========================================================================
    
    // Server rejected order (validation failed, risk limits, etc.)
    private boolean rejected = false;
    
    // User canceled order (via OrderCancelRequest)
    private boolean canceled = false;
    
    // Order is new (not yet acknowledged by server)
    // Set to false when first ExecutionReport with NEW status arrives
    private boolean isNew = true;
    
    // ========================================================================
    // MESSAGING AND TRACKING
    // ========================================================================
    
    // Optional text message from server (reject reason, info, etc.)
    private String message = null;
    
    // Client Order ID - unique identifier for this order
    // Generated on client side, echoed in all server responses
    private String ID = null;
    
    // Original Order ID for replaced orders
    // When replacing order, this references the order being replaced
    private String originalID = null;
    
    // Static counter for generating unique IDs
    private static int nextID = 1;

    // ========================================================================
    // CONSTRUCTORS
    // ========================================================================
    
    /**
     * Default constructor - generates new ID automatically.
     * Used when creating new orders from UI.
     */
    public Order() {
        ID = generateID();
    }

    /**
     * Constructor with specific ID.
     * Used when reconstructing orders from messages.
     * @param ID Client Order ID
     */
    public Order(String ID) {
        this.ID = ID;
    }

    // ========================================================================
    // CLONING - For Order Replacement
    // ========================================================================
    
    /**
     * Creates a copy of this order for replacement.
     * 
     * REPLACEMENT WORKFLOW:
     * 1. User selects order and clicks "Replace"
     * 2. Order is cloned
     * 3. Clone gets new ID
     * 4. Clone's originalID = original order's ID
     * 5. User modifies clone's parameters
     * 6. OrderCancelReplaceRequest sent with both IDs
     * 
     * WHY CLONE?
     * - Preserves original order for reference
     * - Maintains order history
     * - Allows tracking replacement chain
     * 
     * @return Cloned Order with new ID
     */
    public Object clone() {
        try {
            Order order = (Order) super.clone();
            order.setOriginalID(getID());  // Link to original
            order.setID(order.generateID());  // Generate new ID
            return order;
        } catch (CloneNotSupportedException e) {}
        return null;
    }

    // ========================================================================
    // ID GENERATION
    // ========================================================================
    
    /**
     * Generates unique Client Order ID.
     * 
     * ALGORITHM:
     * Uses current timestamp (milliseconds) + counter
     * Ensures uniqueness even if multiple orders created in same millisecond
     * 
     * FORMAT EXAMPLE:
     * "1736799600123" (timestamp) + "1" (counter) = "17367996001231"
     * 
     * PRODUCTION CONSIDERATIONS:
     * This simple approach is fine for single-instance clients.
     * For distributed systems, consider:
     * - UUID for guaranteed uniqueness
     * - Prefix with client ID
     * - Use sequence numbers from database
     * - Include date for easy sorting
     * 
     * @return Unique order ID as string
     */
    public String generateID() {
        return Long.toString(System.currentTimeMillis() + (nextID++));
    }

    // ========================================================================
    // GETTERS AND SETTERS - Session and Routing
    // ========================================================================
    
    /**
     * Returns the FIX session this order is associated with.
     * Determines which connection to use for order messages.
     * @return SessionID
     */
    public SessionID getSessionID() {
        return sessionID;
    }

    /**
     * Sets the FIX session for this order.
     * Must be set before sending order.
     * @param sessionID FIX session identifier
     */
    public void setSessionID(SessionID sessionID) {
        this.sessionID = sessionID;
    }

    // ========================================================================
    // GETTERS AND SETTERS - Order Parameters
    // ========================================================================
    
    public String getSymbol() {
        return symbol;
    }

    public void setSymbol(String symbol) {
        this.symbol = symbol;
    }

    public int getQuantity() {
        return quantity;
    }

    public void setQuantity(int quantity) {
        this.quantity = quantity;
    }

    public int getOpen() {
        return open;
    }

    public void setOpen(int open) {
        this.open = open;
    }

    public double getExecuted() {
        return executed;
    }

    public void setExecuted(double executed) { 
        this.executed = executed; 
    }

    public OrderSide getSide() {
        return side;
    }

    public void setSide(OrderSide side) {
        this.side = side;
    }

    public OrderType getType() {
        return type;
    }

    public void setType(OrderType type) {
        this.type = type;
    }

    public OrderTIF getTIF() {
        return tif;
    }

    public void setTIF(OrderTIF tif) {
        this.tif = tif;
    }

    // ========================================================================
    // PRICE GETTERS AND SETTERS - Support Null Values
    // ========================================================================
    
    /**
     * Returns limit price (for LIMIT and STOP_LIMIT orders).
     * @return Limit price or null
     */
    public Double getLimit() {
        return limit;
    }

    /**
     * Sets limit price as Double.
     * @param limit Limit price or null
     */
    public void setLimit(Double limit) {
        this.limit = limit;
    }

    /**
     * Sets limit price from string (UI input).
     * Handles empty string as null (no limit price).
     * @param limit Limit price as string or empty/null
     */
    public void setLimit(String limit) {
        if (limit == null || limit.equals("")) {
            this.limit = null;
        } else {
            this.limit = Double.parseDouble(limit);
        }
    }

    /**
     * Returns stop price (for STOP and STOP_LIMIT orders).
     * @return Stop price or null
     */
    public Double getStop() {
        return stop;
    }

    /**
     * Sets stop price as Double.
     * @param stop Stop price or null
     */
    public void setStop(Double stop) {
        this.stop = stop;
    }

    /**
     * Sets stop price from string (UI input).
     * Handles empty string as null (no stop price).
     * @param stop Stop price as string or empty/null
     */
    public void setStop(String stop) {
        if (stop == null || stop.equals("")) {
            this.stop = null;
        } else {
            this.stop = Double.parseDouble(stop);
        }
    }

    // ========================================================================
    // EXECUTION STATE GETTERS AND SETTERS
    // ========================================================================
    
    /**
     * Sets weighted average execution price.
     * Updated by BanzaiApplication when fills occur.
     * @param avgPx Average price
     */
    public void setAvgPx(double avgPx) {
        this.avgPx = avgPx;
    }

    /**
     * Returns weighted average execution price.
     * Only meaningful if executed > 0.
     * @return Average fill price
     */
    public double getAvgPx() {
        return avgPx;
    }

    // ========================================================================
    // STATUS FLAG GETTERS AND SETTERS
    // ========================================================================
    
    /**
     * Sets rejected flag.
     * Called when ExecutionReport with REJECTED status received.
     * @param rejected true if order rejected
     */
    public void setRejected(boolean rejected) {
        this.rejected = rejected;
    }

    /**
     * Returns rejected flag.
     * @return true if order was rejected by server
     */
    public boolean getRejected() {
        return rejected;
    }

    /**
     * Sets canceled flag.
     * Called when ExecutionReport with CANCELED status received.
     * @param canceled true if order canceled
     */
    public void setCanceled(boolean canceled) {
        this.canceled = canceled;
    }

    /**
     * Returns canceled flag.
     * @return true if order was canceled
     */
    public boolean getCanceled() {
        return canceled;
    }

    /**
     * Sets new order flag.
     * Starts as true, set to false when first acknowledge received.
     * @param isNew true if order not yet acknowledged
     */
    public void setNew(boolean isNew) {
        this.isNew = isNew;
    }

    /**
     * Returns new order flag.
     * @return true if order not yet acknowledged by server
     */
    public boolean isNew() {
        return isNew;
    }

    // ========================================================================
    // MESSAGE GETTERS AND SETTERS
    // ========================================================================
    
    /**
     * Sets optional message from server.
     * Often contains reject reasons or informational text.
     * @param message Text from ExecutionReport or reject
     */
    public void setMessage(String message) {
        this.message = message;
    }

    /**
     * Returns optional message from server.
     * @return Message text or null
     */
    public String getMessage() {
        return message;
    }

    // ========================================================================
    // ID GETTERS AND SETTERS
    // ========================================================================
    
    /**
     * Sets Client Order ID.
     * Usually auto-generated, but can be set explicitly.
     * @param ID Client Order ID
     */
    public void setID(String ID) {
        this.ID = ID;
    }

    /**
     * Returns Client Order ID.
     * Unique identifier for this order.
     * @return Client Order ID
     */
    public String getID() {
        return ID;
    }

    /**
     * Sets original order ID for replacements.
     * Links replacement to original order.
     * @param originalID ID of order being replaced
     */
    public void setOriginalID(String originalID) {
        this.originalID = originalID;
    }

    /**
     * Returns original order ID for replacements.
     * @return Original order ID or null if not a replacement
     */
    public String getOriginalID() {
        return originalID;
    }
}
