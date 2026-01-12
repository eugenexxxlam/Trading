// ============================================================================
// ORDER TYPE - Order Type Enumeration
// ============================================================================
// Type-safe enumeration of order types supported by the trading system.
// Implements pattern similar to Java enums (pre-Java 5 style).
//
// PURPOSE:
// - Define valid order types for GUI dropdown
// - Ensure type safety (no invalid strings)
// - Provide human-readable names
// - Enable parsing from strings
//
// SUPPORTED ORDER TYPES:
// - **MARKET**: Execute at best available price immediately
// - **LIMIT**: Execute only at specified price or better
// - **STOP**: Becomes market order when stop price reached
// - **STOP_LIMIT**: Becomes limit order when stop price reached
//
// ORDER TYPE EXPLANATIONS:
// 
// MARKET ORDER:
// - Executes immediately at current market price
// - Guarantees execution, not price
// - BUY: pays ask price
// - SELL: receives bid price
// Example: "BUY 100 AAPL MARKET" → Buys at current ask
//
// LIMIT ORDER:
// - Executes only at limit price or better
// - Guarantees price, not execution
// - May not execute if price not reached
// - BUY: executes at limit or lower
// - SELL: executes at limit or higher
// Example: "BUY 100 AAPL @ $150.00 LIMIT"
//
// STOP ORDER (Stop Loss):
// - Triggers when market reaches stop price
// - Then becomes MARKET order
// - Used to limit losses or protect profits
// - BUY STOP: triggers when price rises to stop (breakout)
// - SELL STOP: triggers when price falls to stop (stop loss)
// Example: "SELL 100 AAPL @ $145.00 STOP" (sell if drops to $145)
//
// STOP LIMIT:
// - Triggers when market reaches stop price
// - Then becomes LIMIT order (not market)
// - More control than stop, but may not execute
// Example: "SELL 100 AAPL Stop=$145.00 Limit=$144.50"
//   → If drops to $145, sell at $144.50 or better
//
// DESIGN PATTERN:
// Type-safe enumeration pattern (pre-Java 5):
// - Private constructor prevents external instantiation
// - Static constants are only instances
// - Registry map allows parsing from strings
//
// USAGE IN BANZAI:
// - Populate dropdown in order entry GUI
// - Validate user-selected order types
// - Map to FIX OrdType field values
// - Parse order types from configuration
// ============================================================================

package quickfix.examples.banzai;

import java.util.HashMap;
import java.util.Map;

public class OrderType {
    // ========================================================================
    // REGISTRY - Map of name to OrderType instance
    // ========================================================================
    static private final Map<String, OrderType> known = new HashMap<>();
    
    // ========================================================================
    // ORDER TYPE CONSTANTS
    // ========================================================================
    // These are the only OrderType instances that will ever exist
    
    /**
     * MARKET order - execute immediately at best available price.
     * 
     * CHARACTERISTICS:
     * - Highest priority (executes first)
     * - No price specified
     * - Guarantees execution (assuming liquidity)
     * - Price uncertain until execution
     * 
     * USE CASES:
     * - Need immediate execution
     * - Liquid markets with tight spreads
     * - Emergency exits
     * 
     * RISKS:
     * - May execute at unfavorable price
     * - Slippage in volatile markets
     * - No price protection
     */
    static public final OrderType MARKET = new OrderType("Market");
    
    /**
     * LIMIT order - execute only at specified price or better.
     * 
     * CHARACTERISTICS:
     * - Requires limit price
     * - Price guaranteed (if executed)
     * - Execution not guaranteed
     * - May rest in order book unfilled
     * 
     * USE CASES:
     * - Price-sensitive trades
     * - Patient trading
     * - Avoid overpaying
     * 
     * RISKS:
     * - May not execute
     * - Missed opportunities if market moves away
     */
    static public final OrderType LIMIT = new OrderType("Limit");
    
    /**
     * STOP order - triggers at stop price, then becomes MARKET order.
     * 
     * CHARACTERISTICS:
     * - Requires stop price
     * - Dormant until stop price reached
     * - Then executes as market order
     * - Guarantees trigger, not execution price
     * 
     * USE CASES:
     * - Stop loss (limit downside)
     * - Breakout trading (buy on momentum)
     * - Protect profits on winning positions
     * 
     * RISKS:
     * - Slippage after trigger
     * - False triggers in volatile markets
     * - Gap risk (opens below stop)
     */
    static public final OrderType STOP = new OrderType("Stop");
    
    /**
     * STOP LIMIT order - triggers at stop, becomes LIMIT order.
     * 
     * CHARACTERISTICS:
     * - Requires stop price and limit price
     * - Triggers at stop price
     * - Then becomes limit order
     * - More control than stop market
     * 
     * USE CASES:
     * - Stop loss with price control
     * - Avoid extreme slippage
     * - Controlled exits
     * 
     * RISKS:
     * - May not execute after trigger
     * - Worst of both worlds if wrong
     * - Gap risk (may miss exit entirely)
     */
    static public final OrderType STOP_LIMIT = new OrderType("Stop Limit");
    
    // ========================================================================
    // INTERNAL STATE
    // ========================================================================
    private final String name;  // Human-readable name

    // Array for GUI dropdown population
    static private final OrderType[] array = { MARKET, LIMIT, STOP, STOP_LIMIT };

    // ========================================================================
    // CONSTRUCTOR - Private to prevent external instantiation
    // ========================================================================
    /**
     * Private constructor ensures only static constants exist.
     * Registers instance in known map for parsing.
     */
    private OrderType(String name) {
        this.name = name;
        synchronized (OrderType.class) {
            known.put(name, this);
        }
    }

    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Gets human-readable name.
     * 
     * @return Order type name (e.g., "Market", "Limit")
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
     * Returns array of all order types for GUI dropdown.
     * 
     * USAGE:
     * JComboBox orderTypeDropdown = new JComboBox(OrderType.toArray());
     * 
     * @return Array containing all OrderType instances
     */
    static public Object[] toArray() {
        return array;
    }

    /**
     * Parses order type from string name.
     * 
     * PARSING:
     * Case-sensitive lookup in registry map.
     * Must match exactly: "Market", "Limit", "Stop", "Stop Limit"
     * 
     * USAGE:
     * OrderType type = OrderType.parse("Limit");
     * 
     * @param type String name to parse
     * @return Corresponding OrderType instance
     * @throws IllegalArgumentException if type unknown
     */
    public static OrderType parse(String type) throws IllegalArgumentException {
        OrderType result = known.get(type);
        if (result == null) {
            throw new IllegalArgumentException
            ("OrderType: " + type + " is unknown.");
        }
        return result;
    }
}
