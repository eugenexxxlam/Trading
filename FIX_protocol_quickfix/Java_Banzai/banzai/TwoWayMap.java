// ============================================================================
// TWO-WAY MAP - Bidirectional Key-Value Mapping
// ============================================================================
// A simple bidirectional map that allows lookups in both directions.
// Maps objects both ways: first↔second and second↔first.
//
// PURPOSE:
// - Enable bidirectional lookups
// - Map client order IDs to exchange order IDs and vice versa
// - Map GUI order objects to FIX order IDs
// - Maintain synchronized forward and reverse mappings
//
// USAGE IN BANZAI:
// Tracks relationship between:
// - Client Order ID (ClOrdID) ↔ Order object
// - Order object ↔ Exchange Order ID (OrderID)
// 
// This allows:
// - Given ClOrdID, find Order object (for GUI updates)
// - Given Order object, find ClOrdID (for FIX messages)
// - Given ExecutionReport, update correct Order in GUI
//
// EXAMPLE:
// TwoWayMap map = new TwoWayMap();
// Order order = new Order();
// String clOrdID = "ABC123";
// 
// // Store bidirectional mapping
// map.put(clOrdID, order);
// 
// // Lookup by client order ID
// Order found = (Order) map.getFirst(clOrdID);  // Returns order
// 
// // Lookup by order object
// String id = (String) map.getSecond(order);    // Returns "ABC123"
//
// PRODUCTION CONSIDERATIONS:
// Real implementations would use:
// - Google Guava BiMap for production use
// - Generic types instead of Object
// - Thread-safe operations if multi-threaded
// - Remove() method for cleanup
// ============================================================================

package quickfix.examples.banzai;

import java.util.HashMap;

public class TwoWayMap {
    // ========================================================================
    // INTERNAL MAPS
    // ========================================================================
    // Two HashMaps maintain the bidirectional mapping
    private final HashMap<Object, Object> firstToSecond = new HashMap<>();  // Forward mapping
    private final HashMap<Object, Object> secondToFirst = new HashMap<>();  // Reverse mapping

    // ========================================================================
    // OPERATIONS
    // ========================================================================
    
    /**
     * Stores bidirectional mapping between two objects.
     * 
     * BEHAVIOR:
     * Creates mappings in both directions:
     * - first → second
     * - second → first
     * 
     * OVERWRITES:
     * If either object already exists as a key, the old mapping is replaced.
     * Be careful of orphaned mappings if same object used multiple times.
     * 
     * EXAMPLE:
     * map.put("ClOrdID_001", orderObject1);
     * // Now can lookup in either direction
     * 
     * @param first First object (e.g., ClOrdID string)
     * @param second Second object (e.g., Order object)
     */
    public void put(Object first, Object second) {
        firstToSecond.put(first, second);
        secondToFirst.put(second, first);
    }

    /**
     * Looks up by first object, returns second object.
     * 
     * USAGE:
     * Given client order ID, find Order object:
     * Order order = (Order) map.getFirst(clOrdID);
     * 
     * @param first Key to look up
     * @return Associated second object, or null if not found
     */
    public Object getFirst(Object first) {
        return firstToSecond.get(first);
    }

    /**
     * Looks up by second object, returns first object.
     * 
     * USAGE:
     * Given Order object, find client order ID:
     * String clOrdID = (String) map.getSecond(order);
     * 
     * @param second Key to look up
     * @return Associated first object, or null if not found
     */
    public Object getSecond(Object second) {
        return secondToFirst.get(second);
    }

    /**
     * Returns string representation showing both mappings.
     * 
     * FORMAT:
     * {first1=second1, first2=second2, ...}
     * {second1=first1, second2=first2, ...}
     * 
     * USAGE:
     * Debugging and logging to verify mappings.
     * 
     * @return String showing both internal maps
     */
    public String toString() {
        return firstToSecond.toString() + "\n" + secondToFirst.toString();
    }
}
