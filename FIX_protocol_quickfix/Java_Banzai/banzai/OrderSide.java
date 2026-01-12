// ============================================================================
// ORDER SIDE - Trading Direction Enumeration
// ============================================================================
// This class defines all possible order sides (directions) in trading.
//
// PURPOSE:
// Provides type-safe enumeration of order sides for use in GUI dropdowns
// and FIX message construction. Prevents invalid side values and provides
// human-readable names for display.
//
// DESIGN PATTERN:
// Type-Safe Enum pattern (pre-Java 5 style with static instances)
// - Ensures only valid instances exist
// - Provides string parsing and array conversion
// - Thread-safe registration
//
// FIX PROTOCOL MAPPING:
// Each OrderSide maps to a FIX Side field value via TwoWayMap in BanzaiApplication.
//
// WHY NOT JAVA ENUM?
// This code predates Java 5 enums or maintains compatibility with older
// Java versions. Modern code would use standard enum syntax.
// ============================================================================

package quickfix.examples.banzai;

import java.util.HashMap;
import java.util.Map;

// ============================================================================
// ORDER SIDE CLASS
// ============================================================================
public class OrderSide {
    // Registry of all known OrderSide instances by name
    static private final Map<String, OrderSide> known = new HashMap<>();
    
    // ========================================================================
    // STANDARD ORDER SIDES
    // ========================================================================
    
    /**
     * BUY - Standard buy order (take long position).
     * Most common order side for investors.
     */
    static public final OrderSide BUY = new OrderSide("Buy");
    
    /**
     * SELL - Standard sell order (close long position or establish short).
     * Used to exit existing positions.
     */
    static public final OrderSide SELL = new OrderSide("Sell");
    
    /**
     * SHORT_SELL - Sell borrowed shares (establish short position).
     * Requires locate and compliance with short sale regulations.
     * In production: Must verify share availability before execution.
     */
    static public final OrderSide SHORT_SELL = new OrderSide("Short Sell");
    
    /**
     * SHORT_SELL_EXEMPT - Short sell exempt from uptick rule.
     * Used for market makers and certain hedging strategies.
     * Requires special permission and regulatory compliance.
     */
    static public final OrderSide SHORT_SELL_EXEMPT = new OrderSide("Short Sell Exempt");
    
    /**
     * CROSS - Match buyer and seller from same firm.
     * Used by brokers to internally match orders.
     * Subject to best execution requirements.
     */
    static public final OrderSide CROSS = new OrderSide("Cross");
    
    /**
     * CROSS_SHORT - Cross order involving short sale.
     * Combines crossing and short selling.
     */
    static public final OrderSide CROSS_SHORT = new OrderSide("Cross Short");
    
    /**
     * CROSS_SHORT_EXEMPT - Cross short sale exempt from regulations.
     * Specialized trading scenario.
     */
    static public final OrderSide CROSS_SHORT_EXEMPT = new OrderSide("Cross Short Exempt");

    // ========================================================================
    // ARRAY FOR GUI DROPDOWNS
    // ========================================================================
    // Provides all sides as array for populating dropdown menus
    static private final OrderSide[] array = {
            BUY, SELL, SHORT_SELL, SHORT_SELL_EXEMPT,
            CROSS, CROSS_SHORT, CROSS_SHORT_EXEMPT
    };

    // Display name for this side
    private final String name;

    // ========================================================================
    // CONSTRUCTOR - Private for Type Safety
    // ========================================================================
    /**
     * Creates OrderSide instance and registers it.
     * Private constructor ensures only predefined instances exist.
     * 
     * THREAD SAFETY:
     * Uses synchronization to prevent race conditions during
     * static initialization when multiple threads might access class.
     * 
     * @param name Display name for this side
     */
    private OrderSide(String name) {
        this.name = name;
        synchronized (OrderSide.class) {
            known.put(name, this);
        }
    }

    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Returns display name.
     * @return Human-readable side name
     */
    public String getName() {
        return name;
    }

    /**
     * Returns string representation (same as name).
     * Used for GUI display and debugging.
     * @return Side name
     */
    public String toString() {
        return name;
    }

    // ========================================================================
    // UTILITY METHODS
    // ========================================================================
    
    /**
     * Returns all order sides as array.
     * Used to populate GUI dropdowns.
     * @return Array of all OrderSide instances
     */
    static public Object[] toArray() {
        return array;
    }

    /**
     * Parses string to OrderSide instance.
     * 
     * USAGE:
     * - Loading saved orders from file/database
     * - Processing user input
     * - Deserializing configuration
     * 
     * @param type Side name string
     * @return Matching OrderSide instance
     * @throws IllegalArgumentException if type is unknown
     */
    public static OrderSide parse(String type) throws IllegalArgumentException {
        OrderSide result = known.get(type);
        if (result == null) {
            throw new IllegalArgumentException
            ("OrderSide: " + type + " is unknown.");
        }
        return result;
    }
}
