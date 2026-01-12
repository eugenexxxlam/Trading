// ============================================================================
// ORDER MATCHER - Multi-Symbol Order Book Manager
// ============================================================================
// Manages multiple Market instances, one per trading symbol.
// Provides unified interface for order book operations across all symbols.
//
// PURPOSE:
// - Maintain separate order books for different symbols
// - Route order operations to correct symbol's market
// - Lazy-create markets on first use
// - Display all markets or specific symbol
//
// ARCHITECTURE:
// OrderMatcher (multi-symbol) → Market (single symbol) → Order lists
//
// EXAMPLE STRUCTURE:
// OrderMatcher
//   ├─ "AAPL" → Market (AAPL bids, AAPL asks)
//   ├─ "MSFT" → Market (MSFT bids, MSFT asks)
//   └─ "GOOGL" → Market (GOOGL bids, GOOGL asks)
//
// DELEGATION PATTERN:
// All order operations are delegated to the appropriate Market instance.
// OrderMatcher is essentially a router + factory for Market objects.
//
// PRODUCTION CONSIDERATIONS:
// Real systems would add:
// - Thread-safe concurrent access to markets
// - Market hours and trading halts
// - Circuit breakers per symbol
// - Symbol validation and normalization
// - Market data distribution to subscribers
// - Cross-symbol order types (pairs trading)
// - Historical market data archival
// ============================================================================

package quickfix.examples.ordermatch;

import java.util.ArrayList;
import java.util.HashMap;

public class OrderMatcher {
    // ========================================================================
    // STATE - Map of Symbol to Market
    // ========================================================================
    // HashMap for O(1) lookup of market by symbol name
    private final HashMap<String, Market> markets = new HashMap<>();

    // ========================================================================
    // MARKET ACCESS - Lazy Creation
    // ========================================================================
    /**
     * Gets or creates Market for a symbol.
     * 
     * LAZY INITIALIZATION:
     * Markets are created on first access using computeIfAbsent().
     * If symbol doesn't exist, creates new Market automatically.
     * 
     * BENEFITS:
     * - No need to pre-configure symbols
     * - Memory efficient (only create markets that are used)
     * - Simplifies dynamic symbol addition
     * 
     * EXAMPLE:
     * First order for "AAPL" → creates new Market
     * Subsequent "AAPL" orders → reuse existing Market
     * 
     * @param symbol Trading symbol
     * @return Market instance for this symbol
     */
    private Market getMarket(String symbol) {
        return markets.computeIfAbsent(symbol, k -> new Market());
    }

    // ========================================================================
    // ORDER BOOK OPERATIONS
    // ========================================================================
    // All operations delegate to the appropriate Market instance
    
    /**
     * Inserts order into appropriate market's order book.
     * 
     * PROCESS:
     * 1. Get/create Market for order's symbol
     * 2. Delegate insert to Market
     * 3. Market adds to bid or ask list based on side
     * 4. Orders sorted by price-time priority
     * 
     * EXAMPLE:
     * Order: BUY 100 AAPL @ $150.00
     * → getMarket("AAPL")
     * → market.insert(order)
     * → Added to AAPL bid list at appropriate position
     * 
     * @param order Order to insert
     * @return true if inserted successfully
     */
    public boolean insert(Order order) {
        return getMarket(order.getSymbol()).insert(order);
    }

    /**
     * Attempts to match orders for a symbol.
     * 
     * MATCHING PROCESS:
     * 1. Get Market for symbol
     * 2. Market checks if bid/ask cross
     * 3. If crossing, execute trades
     * 4. Fill orders and remove completed ones
     * 5. Add matched orders to output list
     * 
     * OUTPUT PARAMETER:
     * orders list is populated with orders that executed.
     * These orders need ExecutionReport messages sent.
     * 
     * @param symbol Symbol to match
     * @param orders Output: list of orders that executed
     */
    public void match(String symbol, ArrayList<Order> orders) {
        getMarket(symbol).match(symbol, orders);
    }

    /**
     * Finds order in market by ID and side.
     * 
     * USAGE:
     * - OrderCancelRequest: Find order to cancel
     * - OrderStatusRequest: Find order to report status
     * 
     * SEARCH:
     * Only searches specified side (bid or ask) for efficiency.
     * Uses client order ID for lookup.
     * 
     * @param symbol Symbol to search
     * @param side BUY or SELL (which book to search)
     * @param id Client order ID to find
     * @return Order if found, null otherwise
     */
    public Order find(String symbol, char side, String id) {
        return getMarket(symbol).find(symbol, side, id);
    }

    /**
     * Removes order from market's order book.
     * 
     * USAGE:
     * - Order cancelled by client
     * - Order fully filled (automatically removed during matching)
     * 
     * PROCESS:
     * 1. Get Market for order's symbol
     * 2. Remove from bid or ask list based on side
     * 
     * @param order Order to remove
     */
    public void erase(Order order) {
        getMarket(order.getSymbol()).erase(order);
    }

    // ========================================================================
    // DISPLAY - Market Data Visualization
    // ========================================================================
    
    /**
     * Displays order books for all symbols.
     * 
     * OUTPUT FORMAT:
     * MARKET: AAPL
     *   BIDS:
     *     $150.00 x 500
     *     $149.90 x 300
     *   ASKS:
     *     $150.10 x 400
     *     $150.20 x 600
     * MARKET: MSFT
     *   ...
     * 
     * USAGE:
     * Console monitoring and debugging.
     * Shows current state of all order books.
     */
    public void display() {
        for (String symbol : markets.keySet()) {
            System.out.println("MARKET: " + symbol);
            display(symbol);
        }
    }

    /**
     * Displays order book for specific symbol.
     * 
     * USAGE:
     * Focus on single symbol's market depth.
     * Called by display() for each market.
     * 
     * @param symbol Symbol to display
     */
    public void display(String symbol) {
        getMarket(symbol).display();
    }
}
