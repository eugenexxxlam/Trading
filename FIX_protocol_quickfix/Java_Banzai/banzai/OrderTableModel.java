// ============================================================================
// ORDER TABLE MODEL - Swing Table Model for Orders
// ============================================================================
// MVC Model component for displaying orders in JTable.
// Manages order data and provides interface for JTable rendering.
//
// PURPOSE:
// - Store and manage Order objects for GUI display
// - Provide table data (rows/columns) to JTable
// - Handle order additions and updates
// - Map between table rows and Order objects
// - Support lookups by order ID or row number
//
// MVC PATTERN:
// - Model: OrderTableModel (this class) - stores data
// - View: JTable - renders data visually
// - Controller: BanzaiApplication - updates model from FIX messages
//
// TABLE COLUMNS:
// 0. Symbol: Trading instrument (e.g., "AAPL")
// 1. Quantity: Total order quantity
// 2. Open: Remaining unfilled quantity
// 3. Executed: Filled quantity so far
// 4. Side: BUY or SELL
// 5. Type: MARKET, LIMIT, STOP, STOP_LIMIT
// 6. Limit: Limit price (if applicable)
// 7. Stop: Stop price (if applicable)
// 8. AvgPx: Average execution price
// 9. Target: Exchange/venue (TargetCompID)
//
// DATA STRUCTURES:
// Three HashMaps maintain fast lookups:
// - rowToOrder: row number → Order object
// - idToRow: order ID → row number
// - idToOrder: order ID → Order object
//
// This allows:
// - Given row: get Order object (for display)
// - Given order ID: get row number (for updates)
// - Given order ID: get Order object (for logic)
//
// USAGE FLOW:
// 1. User submits order via GUI
// 2. BanzaiApplication sends NewOrderSingle
// 3. Receive ExecutionReport (NEW status)
// 4. Call addOrder() → Order appears in table
// 5. Receive ExecutionReport (FILLED status)
// 6. Call updateOrder() → Order row updates with fill info
//
// THREAD SAFETY:
// Swing table models should only be accessed from EDT (Event Dispatch Thread).
// Use SwingUtilities.invokeLater() for updates from FIX threads.
//
// PRODUCTION CONSIDERATIONS:
// Real order blotters would add:
// - Sorting by columns
// - Filtering (show only open orders)
// - Color coding (filled green, rejected red)
// - Timestamps (order time, last update)
// - Order actions (right-click to cancel)
// - Multi-selection for batch operations
// ============================================================================

package quickfix.examples.banzai;

import javax.swing.table.AbstractTableModel;
import java.util.HashMap;

/**
 * Table model for displaying Order objects in GUI.
 * 
 * EXTENDS:
 * AbstractTableModel - Swing base class for custom table models
 * 
 * REQUIRED METHODS:
 * - getRowCount(): How many rows
 * - getColumnCount(): How many columns
 * - getValueAt(row, col): Cell value at position
 */
public class OrderTableModel extends AbstractTableModel {

    // ========================================================================
    // COLUMN INDICES
    // ========================================================================
    // Constants for column positions (makes code more readable)
    private final static int SYMBOL = 0;
    private final static int QUANTITY = 1;
    private final static int OPEN = 2;
    private final static int EXECUTED = 3;
    private final static int SIDE = 4;
    private final static int TYPE = 5;
    private final static int LIMITPRICE = 6;
    private final static int STOPPRICE = 7;
    private final static int AVGPX = 8;
    private final static int TARGET = 9;

    // ========================================================================
    // DATA STRUCTURES
    // ========================================================================
    
    /**
     * Maps table row number to Order object.
     * Used by getValueAt() to retrieve order for display.
     */
    private final HashMap<Integer, Order> rowToOrder;
    
    /**
     * Maps order ID to table row number.
     * Used to find which row to update when ExecutionReport received.
     */
    private final HashMap<String, Integer> idToRow;
    
    /**
     * Maps order ID to Order object.
     * Used for quick lookup without knowing row number.
     */
    private final HashMap<String, Order> idToOrder;

    /**
     * Column headers displayed at top of table.
     */
    private final String[] headers;

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    /**
     * Creates empty order table model.
     * Initializes data structures and column headers.
     */
    public OrderTableModel() {
        rowToOrder = new HashMap<>();
        idToRow = new HashMap<>();
        idToOrder = new HashMap<>();

        headers = new String[]
                  {"Symbol", "Quantity", "Open", "Executed",
                   "Side", "Type", "Limit", "Stop", "AvgPx",
                   "Target"};
    }

    // ========================================================================
    // TABLE MODEL INTERFACE - Required by JTable
    // ========================================================================
    
    /**
     * Specifies that no cells are editable.
     * This is a read-only table for displaying order status.
     * 
     * EDITABLE TABLE:
     * To allow editing, return true for specific cells
     * and implement setValueAt() to handle changes.
     * 
     * @return false (all cells read-only)
     */
    public boolean isCellEditable(int rowIndex, int columnIndex) {
        return false;
    }

    /**
     * Specifies column data type (all String for simple rendering).
     * 
     * TYPE BENEFITS:
     * - String: Simple display, no special rendering
     * - Integer: Right-aligned by default
     * - Boolean: Checkbox rendering
     * - Custom: Can provide custom renderer
     * 
     * @return String.class for all columns
     */
    public Class<String> getColumnClass(int columnIndex) {
        return String.class;
    }

    /**
     * Returns number of rows in table.
     * 
     * ROW COUNT:
     * Equals number of orders in rowToOrder map.
     * JTable calls this to determine scroll bar size and row rendering.
     * 
     * @return Number of orders in table
     */
    public int getRowCount() {
        return rowToOrder.size();
    }

    /**
     * Returns number of columns in table.
     * 
     * COLUMN COUNT:
     * Fixed at 10 columns (see headers array).
     * JTable calls this once during initialization.
     * 
     * @return Number of columns (10)
     */
    public int getColumnCount() {
        return headers.length;
    }

    /**
     * Returns column header name for display.
     * 
     * USAGE:
     * JTable displays these at top of table.
     * User can click headers to sort (if sorting implemented).
     * 
     * @param columnIndex Column position (0-9)
     * @return Header string (e.g., "Symbol", "Quantity")
     */
    public String getColumnName(int columnIndex) {
        return headers[columnIndex];
    }

    /**
     * Returns value for specific table cell.
     * 
     * RENDERING FLOW:
     * 1. JTable needs to display cell at (row, col)
     * 2. Calls getValueAt(row, col)
     * 3. We lookup Order for that row
     * 4. We extract appropriate field based on column
     * 5. JTable renders the returned value
     * 
     * CALLED FREQUENTLY:
     * This method is called for EVERY visible cell on EVERY repaint.
     * Should be fast (just lookups and getters).
     * 
     * @param rowIndex Row position (0 to rowCount-1)
     * @param columnIndex Column position (0-9)
     * @return Cell value as Object (String, Integer, etc.)
     */
    public Object getValueAt(int rowIndex, int columnIndex) {
        Order order = rowToOrder.get(rowIndex);
        
        // Map column to appropriate Order field
        switch (columnIndex) {
        case SYMBOL:
            return order.getSymbol();
        case QUANTITY:
            return order.getQuantity();
        case OPEN:
            return order.getOpen();
        case EXECUTED:
            return order.getExecuted();
        case SIDE:
            return order.getSide();
        case TYPE:
            return order.getType();
        case LIMITPRICE:
            return order.getLimit();
        case STOPPRICE:
            return order.getStop();
        case AVGPX:
            return order.getAvgPx();
        case TARGET:
            return order.getSessionID().getTargetCompID();
        }
        return "";  // Shouldn't reach here
    }

    /**
     * Placeholder for setValueAt (not used since table is read-only).
     */
    public void setValueAt(Object value, int rowIndex, int columnIndex) { }

    // ========================================================================
    // ORDER MANAGEMENT METHODS
    // ========================================================================
    
    /**
     * Adds new order to table.
     * 
     * PROCESS:
     * 1. Assign next available row number
     * 2. Store in all three maps
     * 3. Notify JTable to display new row
     * 
     * NOTIFICATIONS:
     * fireTableRowsInserted() tells JTable:
     * - New row added at position 'row'
     * - JTable will call getValueAt() for new row
     * - JTable will repaint to show new order
     * 
     * USAGE:
     * Called when ExecutionReport (NEW status) received.
     * 
     * @param order Order to add
     */
    public void addOrder(Order order) {
        int row = rowToOrder.size();  // Next row = current size

        // Store in all three maps
        rowToOrder.put(row, order);
        idToRow.put(order.getID(), row);
        idToOrder.put(order.getID(), order);

        // Notify JTable to display new row
        fireTableRowsInserted(row, row);
    }

    /**
     * Updates existing order in table.
     * 
     * ID CHANGE HANDLING:
     * If order ID changed (e.g., ClOrdID → OrderID mapping):
     * - Update order's ID
     * - Call replaceOrder() to update all maps
     * 
     * NORMAL UPDATE:
     * If ID unchanged (execution update):
     * - Find row for this order
     * - Notify JTable to repaint row
     * 
     * NOTIFICATIONS:
     * fireTableRowsUpdated() tells JTable:
     * - Row 'row' changed
     * - JTable will call getValueAt() for this row
     * - JTable will repaint to show updates
     * 
     * USAGE:
     * Called when ExecutionReport (FILLED/PARTIAL) received.
     * 
     * @param order Order to update
     * @param id Current order ID (may differ from order.getID())
     */
    public void updateOrder(Order order, String id) {

        // Check if order ID changed
        if (!id.equals(order.getID())) {
            String originalID = order.getID();
            order.setID(id);
            replaceOrder(order, originalID);
            return;
        }

        // Find row for this order
        Integer row = idToRow.get(order.getID());
        if (row == null)
            return;  // Order not in table
            
        // Notify JTable to repaint row
        fireTableRowsUpdated(row, row);
    }

    /**
     * Replaces order with new ID.
     * 
     * USAGE:
     * When order ID changes (ClOrdID → OrderID mapping):
     * - Client assigns ClOrdID: "ABC123"
     * - Exchange assigns OrderID: "987654"
     * - Need to track both IDs for same Order object
     * 
     * PROCESS:
     * 1. Find row using original ID
     * 2. Update all maps with new ID
     * 3. Notify JTable to repaint row
     * 
     * @param order Order with new ID
     * @param originalID Previous ID
     */
    public void replaceOrder(Order order, String originalID) {

        // Find row using original ID
        Integer row = idToRow.get(originalID);
        if (row == null)
            return;

        // Update all maps with new ID
        rowToOrder.put(row, order);
        idToRow.put(order.getID(), row);
        idToOrder.put(order.getID(), order);

        // Notify JTable to repaint row
        fireTableRowsUpdated(row, row);
    }

    /**
     * Adds additional ID mapping to same Order.
     * 
     * USAGE:
     * Track multiple IDs for same order:
     * - ClOrdID (client's ID)
     * - OrderID (exchange's ID)
     * - OrigClOrdID (for cancel/replace)
     * 
     * This allows lookup by any of these IDs.
     * 
     * @param order Order object
     * @param newID Additional ID to map to this order
     */
    public void addID(Order order, String newID) {
        idToOrder.put(newID, order);
    }

    // ========================================================================
    // LOOKUP METHODS
    // ========================================================================
    
    /**
     * Gets Order by ID.
     * 
     * USAGE:
     * When ExecutionReport received:
     * - Extract OrderID or ClOrdID
     * - Lookup Order object
     * - Update Order fields
     * - Call updateOrder() to refresh display
     * 
     * @param id Order ID (ClOrdID or OrderID)
     * @return Order object or null if not found
     */
    public Order getOrder(String id) {
        return idToOrder.get(id);
    }

    /**
     * Gets Order by row number.
     * 
     * USAGE:
     * When user selects row in table:
     * - Get selected row index
     * - Lookup Order object
     * - Display order details or enable actions (cancel)
     * 
     * @param row Row number (0-based)
     * @return Order object or null if row invalid
     */
    public Order getOrder(int row) {
        return rowToOrder.get(row);
    }
}
