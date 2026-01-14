#pragma once

#include "common/macros.h"
#include "common/logging.h"

using namespace Common;

/*
 * FEATURE ENGINE - TRADING SIGNALS AND ALPHA GENERATION
 * ======================================================
 * 
 * PURPOSE:
 * Computes trading signals (features) from market data.
 * Drives trading strategies (market maker, liquidity taker).
 * 
 * WHAT ARE FEATURES:
 * - Quantitative signals derived from market data
 * - Examples: Fair price, momentum, volatility, imbalance
 * - Purpose: Predict short-term price movement
 * - Trading: Buy/sell decisions based on features
 * 
 * CURRENT FEATURES:
 * 
 * 1. Market Price (Fair Price):
 *    - Weighted mid price
 *    - Formula: (bid * ask_qty + ask * bid_qty) / (bid_qty + ask_qty)
 *    - Rationale: Accounts for size imbalance at BBO
 *    - Used by: Market maker (quote around fair price)
 * 
 * 2. Aggressive Trade Quantity Ratio:
 *    - Ratio of trade size to BBO size
 *    - Formula: trade_qty / (side == BUY ? ask_qty : bid_qty)
 *    - Rationale: Large trades indicate momentum
 *    - Used by: Liquidity taker (follow momentum)
 * 
 * FEATURE DESIGN PRINCIPLES:
 * - Fast: Sub-microsecond computation
 * - Simple: Easy to understand and debug
 * - Actionable: Direct trading decision
 * - Stateless: No history (for this simple example)
 * 
 * PRODUCTION FEATURES:
 * - Order book imbalance (multiple levels)
 * - Recent trade flow (buy vs sell volume)
 * - Price momentum (1-second, 10-second returns)
 * - Volatility (rolling standard deviation)
 * - Market maker spread (liquidity provision cost)
 * - Correlated assets (spread relationships)
 * - Time-of-day patterns (intraday seasonality)
 * - Machine learning models (neural networks, etc.)
 * 
 * PERFORMANCE:
 * - Computation: 10-100 ns per feature
 * - Update frequency: Every order book update (1000-10000/sec)
 * - Latency-critical: On hot path to trading decision
 * 
 * ARCHITECTURE:
 * ```
 * Market Data
 *      |
 *      v
 * Feature Engine ----> Features (fair price, momentum, etc.)
 *      |
 *      v
 * Trading Strategy (market maker, liquidity taker)
 *      |
 *      v
 * Order Manager (send orders)
 * ```
 */

namespace Trading {
  /*
   * FEATURE INVALID SENTINEL
   * =========================
   * 
   * Sentinel value indicating feature not yet computed.
   * 
   * Value: NaN (quiet not-a-number)
   * 
   * Usage:
   * ```cpp
   * double fair_price = feature_engine->getMktPrice();
   * if (fair_price != Feature_INVALID) {
   *   // Valid feature, can use
   * } else {
   *   // Not computed yet (e.g., no BBO)
   * }
   * ```
   * 
   * Why NaN:
   * - Propagates through calculations (NaN + X = NaN)
   * - Catches bugs (using invalid feature)
   * - Distinct from 0 (which may be valid value)
   * 
   * Caution:
   * - NaN != NaN (use std::isnan() for comparison)
   * - Arithmetic with NaN produces NaN
   * - Comparison with NaN always false
   */
  constexpr auto Feature_INVALID = std::numeric_limits<double>::quiet_NaN();

  class FeatureEngine {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Initializes feature engine with logger.
     * 
     * Parameters:
     * - logger: Async logger for feature values
     * 
     * Initial state:
     * - All features: Feature_INVALID (NaN)
     * - Computed on: First market data update
     */
    FeatureEngine(Common::Logger *logger)
        : logger_(logger) {
      // Features initialized to Feature_INVALID (NaN)
    }

    /*
     * ON ORDER BOOK UPDATE - COMPUTE FAIR PRICE
     * ==========================================
     * 
     * Called when order book changes (ADD, CANCEL, MODIFY).
     * Computes fair market price (weighted mid).
     * 
     * ALGORITHM:
     * 1. Get BBO (best bid, best ask)
     * 2. Validate BBO (both sides present)
     * 3. Compute weighted mid price:
     *    fair = (bid * ask_qty + ask * bid_qty) / (bid_qty + ask_qty)
     * 4. Store in mkt_price_
     * 5. Log feature values
     * 
     * WEIGHTED MID PRICE:
     * - Standard mid: (bid + ask) / 2
     * - Weighted mid: Accounts for size imbalance
     * - Example:
     *   - BBO: 10@100.00 X 100.50@100
     *   - Standard mid: (100.00 + 100.50) / 2 = 100.25
     *   - Weighted mid: (100.00*100 + 100.50*10) / (10+100) = 100.045
     *   - Interpretation: More sellers (larger ask), price likely to fall
     * 
     * RATIONALE:
     * - Size matters: Large bid suggests buying pressure
     * - Predictive: Weighted mid better predictor than simple mid
     * - Simple: Fast computation (one division, two multiplications)
     * 
     * MARKET MAKER USAGE:
     * - Quote near fair price (not simple mid)
     * - Threshold: Only quote if BBO far from fair (inefficiency)
     * - Example: If fair = 100.04, BBO = 100.00/100.50
     *   - Bid at 99.99 (penny worse than BBO, near fair)
     *   - Ask at 100.51 (penny worse than BBO, near fair)
     * 
     * PERFORMANCE:
     * - BBO access: O(1) (cached pointer)
     * - Computation: 20-50 ns (floating-point arithmetic)
     * - Logging: Async (not on hot path)
     * 
     * Parameters:
     * - ticker_id: Instrument identifier
     * - price: Price that changed (for logging)
     * - side: Side that changed (for logging)
     * - book: Order book pointer (for BBO access)
     * 
     * NOEXCEPT:
     * - No exceptions: Performance-critical
     * - Errors: Logged, not thrown
     */
    auto onOrderBookUpdate(TickerId ticker_id, Price price, Side side, MarketOrderBook* book) noexcept -> void {
      // Get current BBO (best bid and offer)
      const auto bbo = book->getBBO();
      
      // Validate BBO (both bid and ask present)
      if(LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID)) {
        // Compute weighted mid price
        // Formula: (bid * ask_qty + ask * bid_qty) / (bid_qty + ask_qty)
        // Rationale: Larger side pulls fair price toward that side
        mkt_price_ = (bbo->bid_price_ * bbo->ask_qty_ + bbo->ask_price_ * bbo->bid_qty_) / 
                     static_cast<double>(bbo->bid_qty_ + bbo->ask_qty_);
      }

      // Log feature update (async, not on hot path)
      logger_->log("%:% %() % ticker:% price:% side:% mkt-price:% agg-trade-ratio:%\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), 
                   ticker_id, 
                   Common::priceToString(price).c_str(),
                   Common::sideToString(side).c_str(), 
                   mkt_price_,               // Fair price
                   agg_trade_qty_ratio_);    // Momentum signal
    }

    /*
     * ON TRADE UPDATE - COMPUTE AGGRESSIVE TRADE RATIO
     * =================================================
     * 
     * Called when trade occurs (market order executed).
     * Computes aggressive trade quantity ratio (momentum signal).
     * 
     * ALGORITHM:
     * 1. Get BBO (for size comparison)
     * 2. Validate BBO
     * 3. Compute ratio:
     *    ratio = trade_qty / (trade side == BUY ? ask_qty : bid_qty)
     * 4. Store in agg_trade_qty_ratio_
     * 5. Log feature values
     * 
     * AGGRESSIVE TRADE QUANTITY RATIO:
     * - Measures: Size of trade relative to available liquidity
     * - Formula: trade_qty / opposite_side_BBO_qty
     * - Range: 0.0 to >1.0
     * - Interpretation:
     *   - < 0.3: Small trade (noise)
     *   - 0.3-0.7: Medium trade (moderate signal)
     *   - > 0.7: Large trade (strong momentum)
     * 
     * RATIONALE:
     * - Large trades: Informed traders (momentum)
     * - Small trades: Noise (random)
     * - Relative size: 10 shares matters more if BBO is 10 than 1000
     * 
     * EXAMPLE:
     * - BBO: 100@100.00 X 100.50@50
     * - Trade: 40 shares BUY at 100.50 (aggressive BUY)
     * - Ratio: 40 / 50 = 0.8 (consumed 80% of ask)
     * - Signal: Strong buy momentum
     * - Action: Liquidity taker may follow (send aggressive BUY)
     * 
     * LIQUIDITY TAKER USAGE:
     * - Threshold: Only trade if ratio > 0.7 (strong momentum)
     * - Direction: Follow the trade direction (BUY -> BUY)
     * - Size: Clip size (fixed, e.g., 10 shares)
     * 
     * PERFORMANCE:
     * - BBO access: O(1)
     * - Computation: 10-20 ns (one division)
     * - Hot path: Trade updates less frequent than book updates
     * 
     * Parameters:
     * - market_update: Trade event (price, qty, side)
     * - book: Order book pointer (for BBO access)
     * 
     * NOEXCEPT:
     * - No exceptions: Performance-critical
     */
    auto onTradeUpdate(const Exchange::MEMarketUpdate *market_update, MarketOrderBook* book) noexcept -> void {
      // Get current BBO
      const auto bbo = book->getBBO();
      
      // Validate BBO
      if(LIKELY(bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID)) {
        // Compute aggressive trade quantity ratio
        // Trade BUY: Compare to ask_qty (liquidity consumed)
        // Trade SELL: Compare to bid_qty (liquidity consumed)
        agg_trade_qty_ratio_ = static_cast<double>(market_update->qty_) / 
                               (market_update->side_ == Side::BUY ? bbo->ask_qty_ : bbo->bid_qty_);
      }

      // Log trade and features
      logger_->log("%:% %() % % mkt-price:% agg-trade-ratio:%\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   market_update->toString().c_str(), 
                   mkt_price_,              // Fair price
                   agg_trade_qty_ratio_);   // Just computed
    }

    /*
     * GET MARKET PRICE - RETRIEVE FAIR PRICE
     * =======================================
     * 
     * Returns: Current fair market price (weighted mid)
     * 
     * Return value:
     * - double: Fair price
     * - Feature_INVALID (NaN): Not computed yet
     * 
     * Usage:
     * ```cpp
     * auto fair = feature_engine->getMktPrice();
     * if (fair != Feature_INVALID) {
     *   // Use fair price for trading decision
     * }
     * ```
     */
    auto getMktPrice() const noexcept {
      return mkt_price_;
    }

    /*
     * GET AGGRESSIVE TRADE QTY RATIO - RETRIEVE MOMENTUM
     * ===================================================
     * 
     * Returns: Current aggressive trade quantity ratio
     * 
     * Return value:
     * - double: Ratio (0.0 to >1.0)
     * - Feature_INVALID (NaN): Not computed yet
     * 
     * Usage:
     * ```cpp
     * auto ratio = feature_engine->getAggTradeQtyRatio();
     * if (ratio != Feature_INVALID && ratio > 0.7) {
     *   // Strong momentum, follow the trade
     * }
     * ```
     */
    auto getAggTradeQtyRatio() const noexcept {
      return agg_trade_qty_ratio_;
    }

    // Deleted constructors (prevent accidental copies)
    FeatureEngine() = delete;
    FeatureEngine(const FeatureEngine &) = delete;
    FeatureEngine(const FeatureEngine &&) = delete;
    FeatureEngine &operator=(const FeatureEngine &) = delete;
    FeatureEngine &operator=(const FeatureEngine &&) = delete;

  private:
    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (feature values logged for analysis)
    Common::Logger *logger_ = nullptr;

    /*
     * FEATURES
     * ========
     * 
     * mkt_price_ (Fair Market Price):
     * - Weighted mid price
     * - Updated: On every order book change
     * - Used by: Market maker (quote around this price)
     * 
     * agg_trade_qty_ratio_ (Momentum Signal):
     * - Aggressive trade quantity ratio
     * - Updated: On every trade
     * - Used by: Liquidity taker (follow momentum)
     * 
     * Both initialized to Feature_INVALID (NaN).
     */
    double mkt_price_ = Feature_INVALID, agg_trade_qty_ratio_ = Feature_INVALID;
  };
}

/*
 * FEATURE ENGINE DESIGN CONSIDERATIONS
 * =====================================
 * 
 * 1. FEATURE SELECTION:
 *    - Current: Two simple features (educational)
 *    - Production: 10-100 features (complex models)
 *    - Trade-off: More features = more predictive, slower
 *    - Key: Fast computation (<1 μs per feature)
 * 
 * 2. WEIGHTED MID PRICE:
 *    - Better than simple mid (accounts for imbalance)
 *    - Example: 1@100 X 100.10@1000
 *      - Simple mid: 100.05
 *      - Weighted mid: 100.099 (closer to ask)
 *      - Interpretation: Heavy sell pressure
 *    - Research: Weighted mid better predictor of next trade
 * 
 * 3. AGGRESSIVE TRADE RATIO:
 *    - Measures momentum strength
 *    - Large trade (>70% of BBO): Strong signal
 *    - Small trade (<30% of BBO): Noise
 *    - Direction: Follow the trade (BUY -> buy more)
 * 
 * 4. STATELESS DESIGN:
 *    - Current: No history (instant features)
 *    - Production: Time-series features (moving averages, etc.)
 *    - Trade-off: Stateless = simple, stateful = more predictive
 * 
 * 5. PERFORMANCE:
 *    - Floating-point: Fast on modern CPUs (1-2 cycles)
 *    - No branches: LIKELY() hint for branch prediction
 *    - No allocation: Reuse time_str_ buffer
 *    - Noexcept: No exception handling overhead
 * 
 * PRODUCTION FEATURES:
 * 
 * A) Order Book Imbalance:
 *    - Sum: bid_qty at multiple levels vs ask_qty
 *    - Formula: (bid_sum - ask_sum) / (bid_sum + ask_sum)
 *    - Range: -1.0 (all asks) to +1.0 (all bids)
 *    - Predictive: Strong imbalance -> price movement
 * 
 * B) Recent Trade Flow:
 *    - Count: Buy trades vs sell trades (last N seconds)
 *    - Formula: (buy_volume - sell_volume) / total_volume
 *    - Interpretation: Buy pressure vs sell pressure
 * 
 * C) Price Momentum:
 *    - Return: (current_price - price_1sec_ago) / price_1sec_ago
 *    - Multiple timeframes: 1s, 10s, 60s
 *    - Trend: Positive momentum -> continue up
 * 
 * D) Volatility:
 *    - Standard deviation of price changes
 *    - Rolling window: Last 60 seconds
 *    - Risk: High volatility = higher risk
 * 
 * E) Spread:
 *    - ask - bid
 *    - Normalized: spread / mid_price
 *    - Liquidity: Tight spread = liquid market
 * 
 * F) Microstructure:
 *    - Order arrival rate
 *    - Cancel rate
 *    - Modify rate
 *    - Quote updates per second
 * 
 * G) Cross-Asset:
 *    - Correlated instruments (ETF vs components)
 *    - Spread trading (long one, short another)
 *    - Lead-lag relationships
 * 
 * H) Machine Learning:
 *    - Neural networks (LSTM, Transformer)
 *    - Gradient boosting (XGBoost, LightGBM)
 *    - Feature engineering pipeline
 *    - Online learning (update model in real-time)
 * 
 * FEATURE COMPUTATION LATENCY:
 * - Simple arithmetic: 10-50 ns
 * - Moving average: 50-100 ns (ring buffer)
 * - Standard deviation: 100-200 ns
 * - Neural network: 1-10 μs (small model)
 * - Budget: Total <1 μs (maintain low-latency)
 * 
 * FEATURE VALIDATION:
 * - Backtesting: Test on historical data
 * - Live monitoring: Track feature predictive power
 * - Decay: Features lose predictiveness over time (market regime change)
 * - Refresh: Retrain models periodically (daily, weekly)
 * 
 * ALTERNATIVE DESIGNS:
 * 
 * A) Event-Driven Features:
 *    - Trigger: Only compute on specific events
 *    - Advantage: Save computation
 *    - Disadvantage: May miss updates
 * 
 * B) Cached Features:
 *    - Store: Multiple feature values (history)
 *    - Advantage: Time-series analysis
 *    - Disadvantage: Memory, staleness
 * 
 * C) Parallel Computation:
 *    - Multiple threads: Compute features in parallel
 *    - Advantage: Faster (for complex features)
 *    - Disadvantage: Synchronization overhead
 * 
 * COMMON PITFALLS:
 * - Look-ahead bias: Using future information
 * - Overfitting: Features work in backtest, fail live
 * - Staleness: Using old feature values
 * - Numerical issues: Division by zero, overflow
 * - NaN propagation: Arithmetic with invalid features
 * 
 * DEBUGGING:
 * - Log all feature values
 * - Plot feature time series
 * - Correlate with price movements
 * - Check for NaN/Inf
 * - Validate against known good values
 */
