// ============================================================================
// EXECUTION TABLE MODEL - Swing Table Model for Executions
// ============================================================================
// MVC Model component for displaying trade executions (fills) in JTable.
// Manages Execution objects and provides interface for JTable rendering.
//
// PURPOSE:
// - Store and manage Execution objects for GUI display
// - Provide table data (rows/columns) to JTable
// - Show trade history (all fills)
// - Handle execution additions
// - Prevent duplicate executions
// - Support lookups by execution ID or row number
//
// EXECUTION vs ORDER:
// - Order table: Shows orders (intent to trade, may be unfilled)
// - Execution table: Shows actual trades that occurred
// 
// One order may generate multiple executions (partial fills):
// Order: BUY 1000 AAPL @ $150.00
// Execution 1: BUY 300 @ $150.00
// Execution 2: BUY 700 @ $149.95
//
// MVC PATTERN:
// - Model: ExecutionTableModel (this class) - stores data
// - View: JTable - renders data visually
// - Controller: BanzaiApplication - updates model from ExecutionReports
//
// TABLE COLUMNS:
// 0. Symbol: Trading instrument (e.g., "AAPL")
// 1. Quantity: Filled quantity for this execution
// 2. Side: BUY or SELL
// 3. Price: Execution price (LastPx from ExecutionReport)
//
// DATA STRUCTURES:
// Four HashMaps maintain fast lookups and prevent duplicates:
// - rowToExecution: row number → Execution object
// - idToRow: execution ID → row number
// - idToExecution: execution ID → Execution object
// - exchangeIdToExecution: ExecID → Execution object (duplicate check)
//
// DUPLICATE PREVENTION:
// ExecutionReports can be duplicated due to:
// - Network retransmission
// - Session replay
// - PossDupFlag scenarios
// 
// exchangeIdToExecution map ensures each ExecID only appears once.
//
// USAGE FLOW:
// 1. Order submitted and partially fills
// 2. Receive ExecutionReport (ExecType=PARTIAL_FILL)
// 3. Create Execution object from LastQty/LastPx
// 4. Call addExecution() → Execution appears in table
// 5. Order fills more → new ExecutionReport
// 6. Call addExecution() → Second execution appears
//
// THREAD SAFETY:
// Swing table models should only be accessed from EDT (Event Dispatch Thread).
// Use SwingUtilities.invokeLater() for updates from FIX threads.
//
// PRODUCTION CONSIDERATIONS:
// Real execution blotters would add:
// - Timestamps (execution time)
// - Execution venue/market center
// - Commission and fees
// - Liquidity indicators (maker/taker)
// - Sorting by time or symbol
// - Filtering by symbol or time range
// - Export to CSV for reconciliation
// - Execution quality metrics
// - P&L calculation
// ============================================================================

package quickfix.examples.banzai;

import javax.swing.table.AbstractTableModel;
import java.util.HashMap;

/**
 * Table model for displaying Execution objects in GUI.
 * 
 * EXTENDS:
 * AbstractTableModel - Swing base class for custom table models
 * 
 * REQUIRED METHODS:
 * - getRowCount(): How many rows
 * - getColumnCount(): How many columns
 * - getValueAt(row, col): Cell value at position
 */
public class ExecutionTableModel extends AbstractTableModel {

    // ========================================================================
    // COLUMN INDICES
    // ========================================================================
    // Constants for column positions
    private final static int SYMBOL = 0;
    private final static int QUANTITY = 1;
    private final static int SIDE = 2;
    private final static int PRICE = 3;

    // ========================================================================
    // DATA STRUCTURES
    // ========================================================================
    
    /**
     * Maps table row number to Execution object.
     * Used by getValueAt() to retrieve execution for display.
     */
    private final HashMap<Integer, Execution> rowToExecution;
    
    /**
     * Maps client execution ID to table row number.
     * Used to find which row for a given execution.
     */
    private final HashMap<String, Integer> idToRow;
    
    /**
     * Maps client execution ID to Execution object.
     * Used for quick lookup without knowing row number.
     */
    private final HashMap<String, Execution> idToExecution;
    
    /**
     * Maps exchange execution ID (ExecID) to Execution object.
     * 
     * PURPOSE - Duplicate Prevention:
     * FIX ExecutionReports can be duplicated due to:
     * - Network retransmission
     * - Session sequence number gaps
     * - Resend requests
     * 
     * ExecID (tag 17) is unique per execution.
     * Check this map before adding to prevent duplicate displays.
     * 
     * EXAMPLE:
     * ExecutionReport 1: ExecID="12345", LastQty=100 → added
     * ExecutionReport 2: ExecID="12345", LastQty=100 → ignored (duplicate)
     */
    private final HashMap<String, Execution> exchangeIdToExecution;

    /**
     * Column headers displayed at top of table.
     */
    private final String[] headers;

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    /**
     * Creates empty execution table model.
     * Initializes data structures and column headers.
     */
    public ExecutionTableModel() {
        rowToExecution = new HashMap<>();
        idToRow = new HashMap<>();
        idToExecution = new HashMap<>();
        exchangeIdToExecution = new HashMap<>();

        headers = new String[] {"Symbol", "Quantity", "Side", "Price"};
    }

    // ========================================================================
    // TABLE MODEL INTERFACE - Required by JTable
    // ========================================================================
    
    /**
     * Specifies that no cells are editable.
     * Execution table is read-only (historical data).
     * 
     * @return false (all cells read-only)
     */
    public boolean isCellEditable(int rowIndex, int columnIndex) {
        return false;
    }

    /**
     * Specifies column data type (all String for simple rendering).
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
     * Equals number of executions in rowToExecution map.
     * Grows as orders fill (each fill adds a row).
     * 
     * @return Number of executions in table
     */
    public int getRowCount() {
        return rowToExecution.size();
    }

    /**
     * Returns number of columns in table.
     * 
     * COLUMN COUNT:
     * Fixed at 4 columns (Symbol, Quantity, Side, Price).
     * 
     * @return Number of columns (4)
     */
    public int getColumnCount() {
        return headers.length;
    }

    /**
     * Returns column header name for display.
     * 
     * @param columnIndex Column position (0-3)
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
     * 3. We lookup Execution for that row
     * 4. We extract appropriate field based on column
     * 5. JTable renders the returned value
     * 
     * @param rowIndex Row position (0 to rowCount-1)
     * @param columnIndex Column position (0-3)
     * @return Cell value as Object
     */
    public Object getValueAt(int rowIndex, int columnIndex) {
        Execution execution = rowToExecution.get(rowIndex);

        // Map column to appropriate Execution field
        switch (columnIndex) {
        case SYMBOL:
            return execution.getSymbol();
        case QUANTITY:
            return execution.getQuantity();
        case SIDE:
            return execution.getSide();
        case PRICE:
            return execution.getPrice();
        }
        return "";  // Shouldn't reach here
    }

    /**
     * Placeholder for setValueAt (not used since table is read-only).
     */
    public void setValueAt(Object value, int rowIndex, int columnIndex) { }

    // ========================================================================
    // EXECUTION MANAGEMENT METHODS
    // ========================================================================
    
    /**
     * Adds new execution to table (with duplicate check).
     * 
     * DUPLICATE CHECK:
     * Before adding, check if ExecID already exists.
     * If exists: ignore (duplicate ExecutionReport)
     * If new: add to table
     * 
     * PROCESS:
     * 1. Check exchangeIdToExecution for duplicate
     * 2. If duplicate: return immediately (no-op)
     * 3. Assign next available row number
     * 4. Store in all four maps
     * 5. Notify JTable to display new row
     * 
     * NOTIFICATIONS:
     * fireTableRowsInserted() tells JTable:
     * - New row added at position 'row'
     * - JTable will call getValueAt() for new row
     * - JTable will repaint to show new execution
     * 
     * USAGE:
     * Called when ExecutionReport with ExecType=FILL or PARTIAL_FILL received.
     * 
     * EXAMPLE:
     * ExecutionReport: ExecID="12345", LastQty=100, LastPx=150.50
     * → Create Execution object
     * → Call addExecution()
     * → Execution appears in table
     * 
     * @param execution Execution to add
     */
    public void addExecution(Execution execution) {
        int row = rowToExecution.size();  // Next row = current size

        // ====================================================================
        // DUPLICATE CHECK
        // ====================================================================
        // If this ExecID already seen, ignore (duplicate)
        if (exchangeIdToExecution.get(execution.getExchangeID()) != null)
            return;

        // ====================================================================
        // STORE IN ALL MAPS
        // ====================================================================
        rowToExecution.put(row, execution);
        idToRow.put(execution.getID(), row);
        idToExecution.put(execution.getID(), execution);
        exchangeIdToExecution.put(execution.getExchangeID(), execution);

        // ====================================================================
        // NOTIFY JTABLE
        // ====================================================================
        // Notify JTable to display new row
        fireTableRowsInserted(row, row);
    }

    // ========================================================================
    // LOOKUP METHODS
    // ========================================================================
    
    /**
     * Gets Execution by exchange execution ID (ExecID).
     * 
     * USAGE:
     * Check if ExecutionReport already processed:
     * - Extract ExecID from message
     * - Lookup in exchangeIdToExecution
     * - If found: duplicate (ignore)
     * - If not found: new (process)
     * 
     * @param exchangeID Exchange execution ID (ExecID tag 17)
     * @return Execution object or null if not found
     */
    public Execution getExchangeExecution(String exchangeID) {
        return exchangeIdToExecution.get(exchangeID);
    }

    /**
     * Gets Execution by row number.
     * 
     * USAGE:
     * When user selects row in execution table:
     * - Get selected row index
     * - Lookup Execution object
     * - Display execution details
     * 
     * @param row Row number (0-based)
     * @return Execution object or null if row invalid
     */
    public Execution getExecution(int row) {
        return rowToExecution.get(row);
    }
}
