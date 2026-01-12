// ============================================================================
// MARKET DATA PROVIDER INTERFACE
// ============================================================================
// This interface defines the contract for obtaining market prices needed
// for order execution. It provides a pluggable architecture for market data.
//
// PURPOSE:
// - Abstract market data source from execution logic
// - Allow different implementations (real-time, simulated, fixed)
// - Support testing with mock data providers
// - Enable easy switching between data sources
//
// IMPLEMENTATIONS:
// 1. Fixed Price: Returns constant price (for testing)
// 2. Random Price: Generates random prices within range (for simulation)
// 3. Live Feed: Connects to real market data (production)
// 4. Historical: Replays past prices (for backtesting)
//
// DESIGN PATTERN:
// Strategy Pattern - Different pricing strategies can be plugged in
// without changing the Executor code.
//
// USAGE IN EXECUTOR:
// When MARKET order received:
// - BUY order: Execute at getAsk(symbol) price
// - SELL order: Execute at getBid(symbol) price
//
// When LIMIT order with AlwaysFillLimitOrders=N:
// - Check if limit price crosses market (executable)
// - BUY: limit >= ask, SELL: limit <= bid
//
// PRODUCTION CONSIDERATIONS:
// Real implementations would:
// - Subscribe to exchange feeds (multicast UDP)
// - Maintain order book depth
// - Handle stale data and circuit breakers
// - Provide last trade price, volume, time
// - Support multiple venues for best execution
// ============================================================================

package quickfix.examples.executor;

// ============================================================================
// MARKET DATA PROVIDER INTERFACE
// ============================================================================
public interface MarketDataProvider {
    
    /**
     * Returns the current ask (offer) price for a symbol.
     * 
     * ASK PRICE:
     * The lowest price at which sellers are willing to sell.
     * Buyers pay the ask price (they "lift the offer").
     * 
     * USAGE:
     * - Execute BUY market orders at ask price
     * - Check if BUY limit orders are executable (limit >= ask)
     * 
     * PRODUCTION CONSIDERATIONS:
     * - Should return best ask across all venues
     * - Must handle missing/stale data gracefully
     * - Consider adding size available at ask
     * - May need bid-ask spread validation
     * 
     * @param symbol Trading instrument (e.g., "AAPL", "MSFT")
     * @return Ask price as double
     */
    public double getAsk(String symbol);
    
    /**
     * Returns the current bid price for a symbol.
     * 
     * BID PRICE:
     * The highest price at which buyers are willing to buy.
     * Sellers receive the bid price (they "hit the bid").
     * 
     * USAGE:
     * - Execute SELL market orders at bid price
     * - Check if SELL limit orders are executable (limit <= bid)
     * 
     * PRODUCTION CONSIDERATIONS:
     * - Should return best bid across all venues
     * - Must be synchronized with ask updates
     * - Consider locked/crossed markets (bid > ask)
     * - May need to include bid size
     * 
     * @param symbol Trading instrument (e.g., "AAPL", "MSFT")
     * @return Bid price as double
     */
    public double getBid(String symbol);
}
