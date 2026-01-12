// ============================================================================
// ID GENERATOR - Unique Identifier Generation
// ============================================================================
// Simple utility class for generating unique OrderID and ExecutionID values.
//
// PURPOSE:
// - Provide sequential, unique identifiers for orders and executions
// - Ensure no ID collision within a single session
// - Maintain separate sequences for different ID types
//
// IMPLEMENTATION:
// Uses simple integer counters that increment with each request.
// IDs are returned as String representations of integers: "0", "1", "2", etc.
//
// THREAD SAFETY:
// NOT thread-safe. For multi-threaded use, methods should be synchronized
// or use AtomicInteger for thread-safe increment.
//
// PRODUCTION CONSIDERATIONS:
// Real systems would need:
// - Persistent ID generation (survive restarts)
// - Distributed ID generation (multiple servers)
// - UUID or snowflake IDs for uniqueness across systems
// - Check for wraparound at MAX_VALUE
// - Separate ID sequences per day/session
// - Database sequences or distributed coordination
//
// EXAMPLE USAGE:
// IdGenerator gen = new IdGenerator();
// String orderId = gen.genOrderID();     // "0"
// String execId = gen.genExecutionID();  // "0"
// String orderId2 = gen.genOrderID();    // "1"
// ============================================================================

package quickfix.examples.ordermatch;

public class IdGenerator {
    // ========================================================================
    // ID COUNTERS
    // ========================================================================
    private int orderIdCounter = 0;       // Next OrderID to assign
    private int executionIdCounter = 0;   // Next ExecutionID to assign

    /**
     * Generates unique ExecutionID.
     * 
     * EXECUTION ID:
     * Unique identifier for each fill/execution event.
     * Multiple executions can occur for a single order (partial fills).
     * 
     * FIX PROTOCOL USAGE:
     * - Sent in ExecutionReport (tag 17)
     * - Used to identify specific fill events
     * - Must be unique across all executions
     * - Never reused
     * 
     * IMPLEMENTATION:
     * Post-increment ensures:
     * - First call returns "0"
     * - Each call gets different ID
     * - Counter ready for next call
     * 
     * @return Unique execution ID as String
     */
    public String genExecutionID() {
        return Integer.toString(executionIdCounter++);
    }

    /**
     * Generates unique OrderID.
     * 
     * ORDER ID:
     * Exchange-assigned identifier for an order.
     * Different from ClOrdID (client's order ID).
     * 
     * FIX PROTOCOL USAGE:
     * - Sent in ExecutionReport (tag 37)
     * - Assigned when order first accepted
     * - Used to identify order in subsequent messages
     * - Remains same for all fills of the order
     * 
     * EXAMPLE FLOW:
     * Client sends: ClOrdID=ABC123
     * Server assigns: OrderID=0
     * All ExecutionReports reference both:
     * - ClOrdID=ABC123 (client's ID)
     * - OrderID=0 (server's ID)
     * 
     * @return Unique order ID as String
     */
    public String genOrderID() {
        return Integer.toString(orderIdCounter++);
    }
}
