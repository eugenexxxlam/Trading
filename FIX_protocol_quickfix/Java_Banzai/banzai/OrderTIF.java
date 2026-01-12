// ============================================================================
// ORDER TIF (Time In Force) - Order Duration Enumeration
// ============================================================================
// Type-safe enumeration of order time-in-force values.
// Specifies how long an order remains active before automatic cancellation.
//
// PURPOSE:
// - Define valid TIF values for GUI dropdown
// - Control order lifetime and execution behavior
// - Ensure type safety
// - Enable parsing from strings
//
// SUPPORTED TIF VALUES:
// - **DAY**: Valid for current trading day only
// - **IOC**: Immediate-or-Cancel (execute immediately, cancel remainder)
// - **OPG**: At-the-Opening (execute at market open only)
// - **GTC**: Good-Till-Cancel (active until filled or manually cancelled)
// - **GTX**: Good-Till-Crossing (execute only in crossing session)
//
// TIF EXPLANATIONS:
//
// DAY ORDER:
// - Most common TIF
// - Active during regular trading hours
// - Auto-cancelled at end of trading day
// - Does not carry over to next day
// Example: Submit DAY order at 10 AM → Cancelled at 4 PM if not filled
//
// IOC (Immediate-or-Cancel):
// - Execute immediately against available liquidity
// - Cancel any unfilled quantity instantly
// - No resting in order book
// - Partial fills acceptable
// Example: "BUY 1000 IOC" → Fills 300 immediately, cancels 700
// Use case: Sweep the book, get instant liquidity
//
// OPG (At-the-Opening):
// - Participate in opening auction/cross only
// - Not valid for continuous trading
// - Cancelled if not filled at open
// Example: Submit before market open → Executes at opening price or cancelled
// Use case: VWAP strategies, avoid intraday volatility
//
// GTC (Good-Till-Cancel):
// - Remains active until filled or manually cancelled
// - Persists across trading days
// - No automatic expiration
// - Some brokers impose max days (e.g., 90 days)
// Example: "BUY 100 @ $50.00 GTC" → Active for weeks until filled or cancelled
// Use case: Patient limit orders, long-term price targets
//
// GTX (Good-Till-Crossing):
// - Execute only in crossing session
// - Crossing sessions: pre-open, mid-day, close
// - Not for continuous trading
// - Cancelled if not filled in cross
// Example: Submit for closing cross → Executes at closing price or cancelled
// Use case: Benchmarking to specific auction prices
//
// DESIGN PATTERN:
// Type-safe enumeration pattern (pre-Java 5):
// - Private constructor prevents external instantiation
// - Static constants are only instances
// - Registry map allows parsing from strings
//
// USAGE IN BANZAI:
// - Populate dropdown in order entry GUI
// - Validate user-selected TIF values
// - Map to FIX TimeInForce field values
// - Control order lifecycle
//
// EXCHANGE SUPPORT:
// Not all exchanges support all TIF values.
// Check venue specifications before using.
// ============================================================================

package quickfix.examples.banzai;

import java.util.HashMap;
import java.util.Map;

public class OrderTIF {
    // ========================================================================
    // REGISTRY - Map of name to OrderTIF instance
    // ========================================================================
    static private final Map<String, OrderTIF> known = new HashMap<>();
    
    // ========================================================================
    // TIF CONSTANTS
    // ========================================================================
    
    /**
     * DAY order - valid for current trading day only.
     * 
     * BEHAVIOR:
     * - Active from submission until end of trading day
     * - Auto-cancelled at market close if not filled
     * - Most common TIF for retail traders
     * 
     * FIX VALUE: TimeInForce='0'
     */
    static public final OrderTIF DAY = new OrderTIF("Day");
    
    /**
     * IOC (Immediate-or-Cancel) - execute immediately, cancel remainder.
     * 
     * BEHAVIOR:
     * - Attempts immediate execution
     * - Fills as much as possible
     * - Cancels any unfilled quantity immediately
     * - Never rests in order book
     * 
     * USE CASES:
     * - Liquidity sweep
     * - Market impact measurement
     * - Aggressive execution
     * 
     * FIX VALUE: TimeInForce='3'
     */
    static public final OrderTIF IOC = new OrderTIF("IOC");
    
    /**
     * OPG (At-the-Opening) - execute at market open only.
     * 
     * BEHAVIOR:
     * - Participates in opening auction
     * - Executes at opening price or not at all
     * - Cancelled if not filled at open
     * - Submit before market opens
     * 
     * USE CASES:
     * - Opening price benchmarking
     * - VWAP strategies
     * - Overnight rebalancing
     * 
     * FIX VALUE: TimeInForce='2'
     */
    static public final OrderTIF OPG = new OrderTIF("OPG");
    
    /**
     * GTC (Good-Till-Cancel) - active until filled or cancelled.
     * 
     * BEHAVIOR:
     * - Persists across trading days
     * - Remains active until:
     *   - Filled completely
     *   - Manually cancelled
     *   - Broker max days reached (e.g., 90 days)
     * 
     * USE CASES:
     * - Long-term limit orders
     * - Patient price targets
     * - Set-and-forget orders
     * 
     * CAUTION:
     * - Can execute days/weeks later unexpectedly
     * - May forget about open orders
     * - Market conditions may change
     * 
     * FIX VALUE: TimeInForce='1'
     */
    static public final OrderTIF GTC = new OrderTIF("GTC");
    
    /**
     * GTX (Good-Till-Crossing) - execute in crossing session only.
     * 
     * BEHAVIOR:
     * - Active for specific crossing session
     * - Crossing sessions: pre-open, mid-day, close
     * - Cancelled if not filled in cross
     * - Not for continuous trading
     * 
     * USE CASES:
     * - Closing price benchmarking
     * - Auction participation
     * - VWAP execution
     * 
     * FIX VALUE: TimeInForce='5'
     */
    static public final OrderTIF GTX = new OrderTIF("GTX");

    // ========================================================================
    // INTERNAL STATE
    // ========================================================================
    private final String name;  // Human-readable name

    // Array for GUI dropdown population
    static private final OrderTIF[] array = { DAY, IOC, OPG, GTC, GTX };

    // ========================================================================
    // CONSTRUCTOR - Private to prevent external instantiation
    // ========================================================================
    /**
     * Private constructor ensures only static constants exist.
     * Registers instance in known map for parsing.
     */
    private OrderTIF(String name) {
        this.name = name;
        synchronized (OrderTIF.class) {
            known.put(name, this);
        }
    }

    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Gets human-readable name.
     * 
     * @return TIF name (e.g., "Day", "IOC", "GTC")
     */
    public String getName() {
        return name;
    }

    /**
     * String representation (same as name).
     * Used for display in GUI dropdown.
     */
    public String toString() {
        return name;
    }

    // ========================================================================
    // UTILITY METHODS
    // ========================================================================
    
    /**
     * Returns array of all TIF values for GUI dropdown.
     * 
     * USAGE:
     * JComboBox tifDropdown = new JComboBox(OrderTIF.toArray());
     * 
     * @return Array containing all OrderTIF instances
     */
    static public Object[] toArray() {
        return array;
    }

    /**
     * Parses TIF from string name.
     * 
     * PARSING:
     * Case-sensitive lookup in registry map.
     * Must match exactly: "Day", "IOC", "OPG", "GTC", "GTX"
     * 
     * USAGE:
     * OrderTIF tif = OrderTIF.parse("GTC");
     * 
     * @param type String name to parse
     * @return Corresponding OrderTIF instance
     * @throws IllegalArgumentException if type unknown
     */
    public static OrderTIF parse(String type) throws IllegalArgumentException {
        OrderTIF result = known.get(type);
        if (result == null) {
            throw new IllegalArgumentException
            ("OrderTIF: " + type + " is unknown.");
        }
        return result;
    }
}
