#pragma once

#include <cstdint>
#include <limits>
#include <sstream>
#include <array>

#include "common/macros.h"

/*
 * TRADING SYSTEM CORE TYPES - TYPE DEFINITIONS FOR HFT MATCHING ENGINE
 * =====================================================================
 * 
 * PURPOSE:
 * Defines all fundamental types used throughout the trading system. These types
 * form the vocabulary of the exchange - orders, prices, quantities, etc.
 * 
 * WHY STRONG TYPES MATTER:
 * - Type safety: OrderId != ClientId (compiler catches bugs)
 * - Fixed-width: Exact size on all platforms (int64_t, uint32_t)
 * - Performance: No overhead vs raw integers (typedef, enum class)
 * - Self-documenting: Code reads like domain language
 * 
 * DESIGN PRINCIPLES:
 * 1. Fixed-width integers: uint32_t, int64_t (not int, long - platform-dependent)
 * 2. Invalid sentinels: Maximum value = INVALID (never used as real data)
 * 3. Type aliases: typedef for clarity (OrderId, not uint64_t)
 * 4. Enum classes: Strong typing (Side::BUY, not just 1)
 * 5. Compile-time constants: constexpr for fixed limits
 */

namespace Common {
  /*
   * SYSTEM CAPACITY CONSTANTS
   * =========================
   * These define the maximum scale of the trading system.
   * Sized for a demonstration exchange, production would be 100-1000x larger.
   */
  
  // Maximum number of trading instruments (stocks, futures, options)
  // Example: AAPL, MSFT, GOOGL, etc.
  // Production: 10,000-100,000 instruments (all US equities, derivatives)
  constexpr size_t ME_MAX_TICKERS = 8;  // Demo: 8 instruments
                                        // Used to size arrays: Order[ME_MAX_TICKERS]

  // Maximum lock-free queue sizes for inter-component communication
  // 256K = 262,144 messages buffered
  // Must handle burst traffic without dropping messages
  constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;   // Orders/cancels from clients
  constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;   // Market data to clients

  // Maximum number of simultaneous trading clients
  // Production: 1,000-10,000 clients (trading firms, market makers)
  constexpr size_t ME_MAX_NUM_CLIENTS = 256;  // Demo: 256 clients

  // Maximum orders per client (order ID space per client)
  // 1M orders = room for high-frequency traders
  // Production: Can be billions (cumulative over time)
  constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;  // 1 million orders per client

  // Maximum price levels in order book (depth)
  // 256 price levels each side (bid/ask) = 512 total
  // Production: 1,000-10,000 levels (deep books)
  constexpr size_t ME_MAX_PRICE_LEVELS = 256;

  /*
   * ORDER ID TYPE
   * =============
   * Unique identifier for each order in the system.
   * 
   * WHY uint64_t?
   * - Range: 0 to 18,446,744,073,709,551,615 (18 quintillion)
   * - Lifetime: Can handle 1 billion orders/second for 584 years
   * - Uniqueness: Never need to recycle IDs
   * - No collision: Even with millions of clients
   * 
   * GENERATION:
   * - Sequential: 1, 2, 3, ... (simple, predictable)
   * - Or timestamp-based: High bits = timestamp, low bits = counter
   * - Never reuse: Once assigned, never used again
   * 
   * USAGE:
   * - Client order ID: Client assigns (tracks their orders)
   * - Market order ID: Exchange assigns (global unique ID)
   */
  typedef uint64_t OrderId;  // 64-bit unsigned integer for order IDs
  constexpr auto OrderId_INVALID = std::numeric_limits<OrderId>::max();  // Max value = invalid (18446744073709551615)
                                                                          // Used for: uninitialized, deleted, not found

  inline auto orderIdToString(OrderId order_id) -> std::string {
    if (UNLIKELY(order_id == OrderId_INVALID)) {  // Check if invalid (rare case)
      return "INVALID";  // Human-readable for logs
    }
    return std::to_string(order_id);  // Convert to decimal string: "12345"
  }

  /*
   * TICKER ID TYPE
   * ==============
   * Identifier for trading instrument (stock, future, option, etc.)
   * 
   * WHY uint32_t?
   * - Range: 0 to 4,294,967,295 (4.3 billion instruments)
   * - Sufficient: Even for all global securities
   * - Compact: Half size of uint64_t (cache-friendly)
   * - Fast indexing: array[ticker_id] for O(1) lookup
   * 
   * MAPPING:
   * - 0: AAPL (Apple stock)
   * - 1: MSFT (Microsoft stock)
   * - 2: ESH25 (E-mini S&P 500 March 2025)
   * - etc.
   * 
   * USAGE:
   * - Index into order books: order_books[ticker_id]
   * - Route orders: Which instrument is this order for?
   * - Market data: Which instrument was traded?
   */
  typedef uint32_t TickerId;  // 32-bit unsigned for instrument IDs
  constexpr auto TickerId_INVALID = std::numeric_limits<TickerId>::max();  // 4294967295 = invalid

  inline auto tickerIdToString(TickerId ticker_id) -> std::string {
    if (UNLIKELY(ticker_id == TickerId_INVALID)) {
      return "INVALID";
    }
    return std::to_string(ticker_id);
  }

  /*
   * CLIENT ID TYPE
   * ==============
   * Identifier for trading client (firm, trader, algorithm)
   * 
   * WHY uint32_t?
   * - Range: 4.3 billion clients (more than enough)
   * - Compact: Small memory footprint
   * - Fast: Indexing and lookups
   * 
   * USAGE:
   * - Authorization: Which client sent this order?
   * - Risk management: Track positions per client
   * - Billing: Who to charge fees?
   * - Market data: Who gets what data?
   */
  typedef uint32_t ClientId;  // 32-bit unsigned for client IDs
  constexpr auto ClientId_INVALID = std::numeric_limits<ClientId>::max();

  inline auto clientIdToString(ClientId client_id) -> std::string {
    if (UNLIKELY(client_id == ClientId_INVALID)) {
      return "INVALID";
    }
    return std::to_string(client_id);
  }

  /*
   * PRICE TYPE
   * ==========
   * Price representation using fixed-point arithmetic.
   * 
   * WHY int64_t (NOT double)?
   * - Precision: No floating-point rounding errors (critical!)
   * - Determinism: Same calculation, same result (always)
   * - Performance: Integer ops faster than floating-point
   * - Comparison: Exact equality checks (price1 == price2)
   * 
   * FIXED-POINT REPRESENTATION:
   * - Example: $100.50 stored as 10050 (cents, 2 decimal places)
   * - Or: $100.5000 stored as 1005000 (4 decimal places)
   * - Division by 100 or 10000 converts to dollars
   * 
   * FINANCIAL ACCURACY:
   * - float: 7 significant digits (insufficient for finance)
   * - double: 15 significant digits (can have rounding errors)
   * - int64_t: Exact (no rounding, perfect for money)
   * 
   * EXAMPLE:
   * - Price 12345 with 2 decimals = $123.45
   * - Price 12345678 with 4 decimals = $1234.5678
   * 
   * WHY SIGNED (int64_t not uint64_t)?
   * - Allows negative prices (rare but exists: negative oil prices 2020)
   * - Price differences can be negative: (bid_price - ask_price)
   */
  typedef int64_t Price;  // 64-bit signed integer for prices (fixed-point)
  constexpr auto Price_INVALID = std::numeric_limits<Price>::max();  // Max = invalid

  inline auto priceToString(Price price) -> std::string {
    if (UNLIKELY(price == Price_INVALID)) {
      return "INVALID";
    }
    return std::to_string(price);  // Raw value (need to divide by scale for dollars)
  }

  /*
   * QUANTITY TYPE
   * =============
   * Order quantity (number of shares, contracts, lots)
   * 
   * WHY uint32_t?
   * - Range: 0 to 4.3 billion shares/contracts
   * - Sufficient: Even for large institutional orders
   * - Compact: Saves memory in order book
   * - Unsigned: Quantity never negative
   * 
   * EXAMPLES:
   * - Stock: 100 shares of AAPL
   * - Future: 10 contracts of ESH25
   * - Options: 5 option contracts
   * 
   * TYPICAL SIZES:
   * - Retail: 1-1000 shares
   * - Institutional: 10,000-1,000,000 shares
   * - HFT: Often small (1-100) but very frequent
   */
  typedef uint32_t Qty;  // 32-bit unsigned for quantities
  constexpr auto Qty_INVALID = std::numeric_limits<Qty>::max();

  inline auto qtyToString(Qty qty) -> std::string {
    if (UNLIKELY(qty == Qty_INVALID)) {
      return "INVALID";
    }
    return std::to_string(qty);
  }

  /*
   * PRIORITY TYPE
   * =============
   * Position in FIFO queue at a price level (price-time priority).
   * 
   * PRICE-TIME PRIORITY:
   * At same price, earlier orders have priority (first in, first out).
   * Priority determines matching order when multiple orders at same price.
   * 
   * WHY uint64_t?
   * - Large range: Can handle billions of orders at same price
   * - Monotonic: Increment for each order (never decreases)
   * - Comparison: Lower priority = earlier order
   * 
   * EXAMPLE:
   * Price $100.00:
   * - Order A: priority 1 (first)
   * - Order B: priority 2 (second)
   * - Order C: priority 3 (third)
   * If incoming sell at $100, matches A first (lowest priority = earliest)
   */
  typedef uint64_t Priority;  // 64-bit for time priority
  constexpr auto Priority_INVALID = std::numeric_limits<Priority>::max();

  inline auto priorityToString(Priority priority) -> std::string {
    if (UNLIKELY(priority == Priority_INVALID)) {
      return "INVALID";
    }
    return std::to_string(priority);
  }

  /*
   * SIDE ENUMERATION
   * ================
   * Order side: Buy or Sell
   * 
   * WHY enum class (not plain enum)?
   * - Type safety: Can't accidentally use int where Side expected
   * - Scoped: Side::BUY (not just BUY, avoids name collisions)
   * - Explicit: Must cast to/from int (prevents mistakes)
   * 
   * WHY int8_t (not default int)?
   * - Memory: 1 byte vs 4 bytes (3 bytes saved per order)
   * - Cache: More orders fit in cache line
   * - Sufficient: Only need 4 values (INVALID, BUY, SELL, MAX)
   * 
   * VALUE CHOICES:
   * - BUY = 1, SELL = -1: Mathematical convenience
   * - Buy qty * (+1) = positive flow
   * - Sell qty * (-1) = negative flow
   * - Position = sum of (qty * side) across all trades
   */
  enum class Side : int8_t {  // Strongly typed enum (1 byte)
    INVALID = 0,    // Uninitialized or error
    BUY = 1,        // Bid side (want to buy)
    SELL = -1,      // Ask side (want to sell)
    MAX = 2         // Sentinel for iteration
  };

  inline auto sideToString(Side side) -> std::string {
    switch (side) {
      case Side::BUY:
        return "BUY";
      case Side::SELL:
        return "SELL";
      case Side::INVALID:
        return "INVALID";
      case Side::MAX:
        return "MAX";
    }
    return "UNKNOWN";  // Should never happen (compiler warning if not all cases covered)
  }

  // Convert Side to array index: SELL=-1 -> index 0, INVALID=0 -> index 1, BUY=1 -> index 2
  // Allows: std::array<Data, 3> per_side; per_side[sideToIndex(Side::BUY)] = ...
  inline constexpr auto sideToIndex(Side side) noexcept {
    return static_cast<size_t>(side) + 1;  // +1 shifts range: -1,0,1 -> 0,1,2
  }

  // Get numeric value: BUY=1, SELL=-1 (for position calculations)
  inline constexpr auto sideToValue(Side side) noexcept {
    return static_cast<int>(side);  // Cast enum to underlying int: BUY->1, SELL->-1
  }

  /*
   * ALGO TYPE ENUMERATION
   * =====================
   * Type of trading algorithm/strategy.
   * 
   * TRADING STRATEGIES:
   * - RANDOM: Random orders (testing/simulation)
   * - MAKER: Market making (provide liquidity, earn spread)
   * - TAKER: Liquidity taking (cross spread, aggressive)
   * 
   * MARKET MAKER:
   * - Posts bids and asks (passive orders)
   * - Earns bid-ask spread: Buy at $100.00, sell at $100.01
   * - Provides liquidity to market
   * - Example: Citadel, Virtu, Jane Street
   * 
   * LIQUIDITY TAKER:
   * - Sends marketable orders (aggressive)
   * - Crosses spread: Hit the ask or lift the bid
   * - Consumes liquidity, pays fees
   * - Example: Execution algorithms, arbitrageurs
   */
  enum class AlgoType : int8_t {  // Trading algorithm type
    INVALID = 0,
    RANDOM = 1,   // Random trading (testing)
    MAKER = 2,    // Market maker (passive)
    TAKER = 3,    // Liquidity taker (aggressive)
    MAX = 4       // Sentinel
  };

  inline auto algoTypeToString(AlgoType type) -> std::string {
    switch (type) {
      case AlgoType::RANDOM:
        return "RANDOM";
      case AlgoType::MAKER:
        return "MAKER";
      case AlgoType::TAKER:
        return "TAKER";
      case AlgoType::INVALID:
        return "INVALID";
      case AlgoType::MAX:
        return "MAX";
    }
    return "UNKNOWN";
  }

  // Parse string to AlgoType (for configuration files)
  inline auto stringToAlgoType(const std::string &str) -> AlgoType {
    for (auto i = static_cast<int>(AlgoType::INVALID); i <= static_cast<int>(AlgoType::MAX); ++i) {
      const auto algo_type = static_cast<AlgoType>(i);
      if (algoTypeToString(algo_type) == str)  // String match
        return algo_type;
    }
    return AlgoType::INVALID;  // Not found
  }

  /*
   * RISK CONFIGURATION STRUCTURE
   * ============================
   * Risk limits for trading (per instrument or per client).
   * 
   * PRE-TRADE RISK CHECKS:
   * Before accepting order, verify it doesn't violate limits:
   * - Order size: Single order not too large
   * - Position: Total position within bounds
   * - Loss: Cumulative loss not exceeded
   * 
   * REGULATORY REQUIREMENT:
   * Exchanges must have risk controls to prevent:
   * - Fat finger errors (accidentally large orders)
   * - Rogue algorithms (runaway trading)
   * - Position blow-ups (unlimited exposure)
   */
  struct RiskCfg {
    Qty max_order_size_ = 0;     // Maximum shares/contracts per order
    Qty max_position_ = 0;       // Maximum net position (long or short)
    double max_loss_ = 0;        // Maximum dollar loss allowed

    auto toString() const {
      std::stringstream ss;
      ss << "RiskCfg{"
         << "max-order-size:" << qtyToString(max_order_size_) << " "
         << "max-position:" << qtyToString(max_position_) << " "
         << "max-loss:" << max_loss_
         << "}";
      return ss.str();
    }
  };

  /*
   * TRADE ENGINE CONFIGURATION
   * ==========================
   * Configuration for trading algorithm (per instrument).
   * 
   * PARAMETERS:
   * - clip: Order size (how many shares/contracts to trade)
   * - threshold: Minimum edge before trading (profitability threshold)
   * - risk_cfg: Risk limits for this instrument
   * 
   * USAGE:
   * Each instrument (AAPL, MSFT, etc.) has own configuration.
   * Allows different strategies per instrument.
   */
  struct TradeEngineCfg {
    Qty clip_ = 0;               // Order size (quantity to trade)
    double threshold_ = 0;       // Minimum edge (profit threshold)
    RiskCfg risk_cfg_;           // Risk limits

    auto toString() const {
      std::stringstream ss;
      ss << "TradeEngineCfg{"
         << "clip:" << qtyToString(clip_) << " "
         << "thresh:" << threshold_ << " "
         << "risk:" << risk_cfg_.toString()
         << "}";
      return ss.str();
    }
  };

  // Fast lookup: TickerId -> Configuration
  // Array (not map): O(1) lookup, cache-friendly, fixed size
  typedef std::array<TradeEngineCfg, ME_MAX_TICKERS> TradeEngineCfgHashMap;
}

/*
 * DESIGN PHILOSOPHY - TYPE SAFETY IN TRADING SYSTEMS
 * ===================================================
 * 
 * 1. STRONG TYPING:
 *    - OrderId != ClientId (compiler catches swapped arguments)
 *    - Side::BUY != 1 (must explicitly cast)
 *    - Prevents entire classes of bugs
 * 
 * 2. FIXED-WIDTH INTEGERS:
 *    - int64_t, uint32_t (not int, long)
 *    - Same size on all platforms (32-bit, 64-bit, ARM, x86)
 *    - Portable binary protocols
 * 
 * 3. SENTINEL VALUES:
 *    - _INVALID = max value
 *    - Never used as real data
 *    - Easy to check: if (id == OrderId_INVALID)
 * 
 * 4. FIXED-POINT ARITHMETIC:
 *    - Price as int64_t (not double)
 *    - No floating-point errors
 *    - Exact decimal representations
 * 
 * 5. MEMORY EFFICIENCY:
 *    - int8_t for enums (1 byte vs 4)
 *    - uint32_t for IDs when range sufficient
 *    - Every byte matters (millions of orders in memory)
 * 
 * 6. CACHE FRIENDLINESS:
 *    - Compact types pack more in cache line
 *    - 64-byte cache line holds more orders
 *    - Fewer cache misses = faster execution
 * 
 * PRODUCTION ENHANCEMENTS:
 * - More instruments: ME_MAX_TICKERS = 100000
 * - Larger queues: ME_MAX_CLIENT_UPDATES = 10 million
 * - Symbol table: map<string, TickerId> ("AAPL" -> 0)
 * - Price scales: Different decimal places per instrument
 * - More algo types: VWAP, TWAP, Iceberg, etc.
 */
