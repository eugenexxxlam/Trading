// ============================================================================
// EXECUTION - Trade Execution Record
// ============================================================================
// Represents a single trade execution (fill) for display in the GUI.
// Created from ExecutionReport messages received from the exchange.
//
// PURPOSE:
// - Store execution details for GUI display
// - Track filled orders separately from pending orders
// - Show trade history in execution table
// - Calculate P&L and execution statistics
//
// EXECUTION vs ORDER:
// - Order: Client's intent to trade (may execute partially or not at all)
// - Execution: Actual trade that occurred (always has quantity and price)
// 
// One Order can result in multiple Executions (partial fills):
// - Order: BUY 1000 AAPL @ $150.00
// - Execution 1: BUY 300 AAPL @ $150.00
// - Execution 2: BUY 700 AAPL @ $149.95
//
// DATA SOURCE:
// Created from ExecutionReport messages with:
// - ExecType = FILL or PARTIAL_FILL
// - LastQty = quantity of this fill
// - LastPx = price of this fill
// - ExecID = unique execution ID
// - OrderID = exchange order ID
//
// USAGE IN BANZAI:
// 1. Receive ExecutionReport from exchange
// 2. Create Execution object with fill details
// 3. Add to ExecutionTableModel
// 4. Display in execution table in GUI
//
// PRODUCTION CONSIDERATIONS:
// Real execution records would include:
// - Timestamp of execution
// - Venue/exchange where executed
// - Commission/fees
// - Counterparty information
// - Execution quality metrics
// - Trade settlement details
// ============================================================================

package quickfix.examples.banzai;

public class Execution {
    // ========================================================================
    // EXECUTION ATTRIBUTES
    // ========================================================================
    private String symbol = null;                    // Trading instrument
    private int quantity = 0;                        // Filled quantity
    private OrderSide side = OrderSide.BUY;         // BUY or SELL
    private double price;                            // Execution price
    private String ID = null;                        // Client-side execution ID
    private String exchangeID = null;                // Exchange execution ID (ExecID)
    
    // Static counter for generating unique client-side IDs
    private static int nextID = 1;

    // ========================================================================
    // CONSTRUCTORS
    // ========================================================================
    
    /**
     * Creates execution with auto-generated ID.
     * 
     * ID GENERATION:
     * Uses static counter to generate unique IDs: "1", "2", "3", etc.
     * 
     * USAGE:
     * When creating execution from ExecutionReport,
     * use this constructor then set fields from message.
     */
    public Execution() {
        ID = Integer.toString(nextID++);
    }

    /**
     * Creates execution with specified ID.
     * 
     * USAGE:
     * When execution ID is provided from external source
     * (e.g., loading from database or file).
     * 
     * @param ID Execution ID to use
     */
    public Execution(String ID) {
        this.ID = ID;
    }

    // ========================================================================
    // GETTERS AND SETTERS
    // ========================================================================
    
    /**
     * Gets trading symbol.
     * 
     * SYMBOL:
     * Identifies the instrument that was traded.
     * Examples: "AAPL", "MSFT", "GOOGL"
     * 
     * @return Symbol string
     */
    public String getSymbol() {
        return symbol;
    }

    /**
     * Sets trading symbol.
     * Populated from ExecutionReport Symbol field (tag 55).
     */
    public void setSymbol(String symbol) {
        this.symbol = symbol;
    }

    /**
     * Gets filled quantity.
     * 
     * QUANTITY:
     * Number of shares/contracts executed in this fill.
     * From ExecutionReport LastQty field (tag 32).
     * 
     * PARTIAL FILLS:
     * Each partial fill is separate Execution with its own quantity.
     * 
     * @return Filled quantity
     */
    public int getQuantity() {
        return quantity;
    }

    /**
     * Sets filled quantity.
     * Populated from ExecutionReport LastQty field.
     */
    public void setQuantity(int quantity) {
        this.quantity = quantity;
    }

    /**
     * Gets execution side (BUY or SELL).
     * 
     * SIDE:
     * - BUY: Purchased shares (long position)
     * - SELL: Sold shares (short or closing position)
     * 
     * @return OrderSide enum
     */
    public OrderSide getSide() {
        return side;
    }

    /**
     * Sets execution side.
     * Populated from ExecutionReport Side field (tag 54).
     */
    public void setSide(OrderSide side) {
        this.side = side;
    }

    /**
     * Gets execution price.
     * 
     * PRICE:
     * Actual price at which trade executed.
     * From ExecutionReport LastPx field (tag 31).
     * 
     * PRICE IMPROVEMENT:
     * May differ from order's limit price due to:
     * - Price improvement from exchange
     * - Market order executing at best available price
     * - Matching against better limit price in book
     * 
     * @return Execution price
     */
    public double getPrice() {
        return price;
    }

    /**
     * Sets execution price.
     * Populated from ExecutionReport LastPx field.
     */
    public void setPrice(double price) {
        this.price = price;
    }

    /**
     * Gets client-side execution ID.
     * 
     * CLIENT ID:
     * Auto-generated sequential ID for GUI display.
     * Not sent to exchange, purely for local tracking.
     * 
     * @return Client execution ID
     */
    public String getID() {
        return ID;
    }

    /**
     * Sets exchange execution ID.
     * 
     * EXCHANGE ID (ExecID):
     * Unique identifier assigned by exchange for this fill.
     * From ExecutionReport ExecID field (tag 17).
     * 
     * USAGE:
     * - Reference specific execution in queries
     * - Reconciliation with exchange reports
     * - Trade confirmation matching
     * - Regulatory reporting
     * 
     * @param exchangeID Exchange's execution ID
     */
    public void setExchangeID(String exchangeID) {
        this.exchangeID = exchangeID;
    }

    /**
     * Gets exchange execution ID.
     * 
     * @return Exchange's execution ID (ExecID)
     */
    public String getExchangeID() {
        return exchangeID;
    }
}
