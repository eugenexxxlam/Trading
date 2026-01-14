#pragma once

#include <sstream>

#include "common/macros.h"
#include "common/logging.h"

#include "position_keeper.h"
#include "om_order.h"

using namespace Common;

/*
 * RISK MANAGER - PRE-TRADE RISK CHECKS
 * =====================================
 * 
 * PURPOSE:
 * Validates orders before sending to exchange (pre-trade risk).
 * Prevents excessive orders, positions, and losses.
 * 
 * WHY PRE-TRADE RISK:
 * - Regulatory: Required by SEC, MiFID II, etc.
 * - Self-protection: Prevent fat-finger errors, bugs
 * - Capital preservation: Limit losses
 * - Operational: Prevent exchange circuit breakers
 * 
 * RISK CHECKS:
 * 1. Order size: Max qty per order
 * 2. Position limit: Max net position per ticker
 * 3. Loss limit: Max loss per ticker (stop trading if exceeded)
 * 
 * RISK CHECK TIMING:
 * ```
 * Strategy Decision --> Risk Manager --> Order Manager --> Exchange
 *                            |
 *                            v (if REJECTED)
 *                         Log error, no order sent
 * ```
 * 
 * LATENCY IMPACT:
 * - Pre-trade check: 10-50 ns (simple comparisons)
 * - On critical path: Before order submission
 * - Acceptable: Negligible compared to network (microseconds)
 * 
 * PRODUCTION RISK CHECKS:
 * - Order rate: Max orders/second per ticker
 * - Order value: Max notional value per order
 * - Concentration: Max % of portfolio in one ticker
 * - Volatility: Reject orders in extreme volatility
 * - Market hours: Only trade during market hours
 * - Ticker whitelist: Only trade approved tickers
 * - Account limits: Per-account position/loss limits
 * - Aggregate limits: Firm-wide position/loss limits
 * - Pre-market: Disallow orders in pre-market/after-hours
 * - Trading halts: Check if ticker halted
 * - Credit checks: Available capital/margin
 * 
 * REGULATORY REQUIREMENTS:
 * - SEC Rule 15c3-5: Market access rule (pre-trade risk controls)
 * - MiFID II: European pre-trade risk controls
 * - Real-time: Must be real-time (not end-of-day)
 * - Audit: All risk rejections logged
 * - Kill switch: Ability to stop all trading instantly
 */

namespace Trading {
  class OrderManager;

  /*
   * RISK CHECK RESULT - OUTCOME OF PRE-TRADE RISK CHECK
   * ====================================================
   * 
   * Enumeration representing result of risk check.
   * 
   * VALUES:
   * 
   * INVALID (0):
   * - Should not occur (error state)
   * - Indicates: Bug in risk check logic
   * 
   * ORDER_TOO_LARGE (1):
   * - Order quantity exceeds max_order_size
   * - Example: Order for 1000, limit is 500
   * - Action: Reject order, log
   * 
   * POSITION_TOO_LARGE (2):
   * - Resulting position would exceed max_position
   * - Example: Long 900, Buy 200, limit is 1000
   * - Action: Reject order, log
   * 
   * LOSS_TOO_LARGE (3):
   * - Total PnL exceeds max_loss (stop-out)
   * - Example: Total PnL = -$10,000, limit is -$5,000
   * - Action: Reject order, stop trading
   * 
   * ALLOWED (4):
   * - All checks passed
   * - Order can be sent to exchange
   * 
   * DECISION FLOW:
   * ```cpp
   * auto result = risk_manager.checkPreTradeRisk(ticker, side, qty);
   * switch (result) {
   *   case ALLOWED:
   *     order_manager.newOrder(...);  // Send order
   *     break;
   *   case ORDER_TOO_LARGE:
   *   case POSITION_TOO_LARGE:
   *   case LOSS_TOO_LARGE:
   *     logger.log("Risk rejected: %", riskCheckResultToString(result));
   *     // Do not send order
   *     break;
   * }
   * ```
   */
  enum class RiskCheckResult : int8_t {
    INVALID = 0,            // Error state (should not occur)
    ORDER_TOO_LARGE = 1,    // Order qty > max_order_size
    POSITION_TOO_LARGE = 2, // Resulting position > max_position
    LOSS_TOO_LARGE = 3,     // Total PnL < max_loss (stop-out)
    ALLOWED = 4             // All checks passed
  };

  /*
   * RISK CHECK RESULT TO STRING
   * ============================
   * 
   * Converts RiskCheckResult enum to string for logging.
   * 
   * Used for:
   * - Error logging
   * - Audit trail
   * - Monitoring
   */
  inline auto riskCheckResultToString(RiskCheckResult result) {
    switch (result) {
      case RiskCheckResult::INVALID:
        return "INVALID";
      case RiskCheckResult::ORDER_TOO_LARGE:
        return "ORDER_TOO_LARGE";
      case RiskCheckResult::POSITION_TOO_LARGE:
        return "POSITION_TOO_LARGE";
      case RiskCheckResult::LOSS_TOO_LARGE:
        return "LOSS_TOO_LARGE";
      case RiskCheckResult::ALLOWED:
        return "ALLOWED";
    }

    return "";  // Should never reach (all cases covered)
  }

  /*
   * RISK INFO - PER-TICKER RISK CONFIGURATION AND STATE
   * ====================================================
   * 
   * Stores risk limits and current position for risk checking.
   * 
   * FIELDS:
   * 
   * position_info_:
   * - Pointer to PositionKeeper's PositionInfo
   * - Provides: Current position, PnL
   * - Read-only: RiskManager doesn't modify position
   * 
   * risk_cfg_:
   * - Risk configuration (limits)
   * - Set at initialization (from config file)
   * - Fields: max_order_size, max_position, max_loss
   * 
   * RISK LIMITS (from risk_cfg_):
   * 
   * max_order_size:
   * - Maximum quantity per single order
   * - Example: 100 shares
   * - Purpose: Prevent fat-finger errors (accidentally 10000 instead of 100)
   * 
   * max_position:
   * - Maximum net position (absolute value)
   * - Example: +/-500 shares
   * - Purpose: Limit exposure per ticker
   * 
   * max_loss:
   * - Maximum loss (negative PnL) before stop-out
   * - Example: -$5,000
   * - Purpose: Circuit breaker (stop trading if losing too much)
   * - Action: Reject all new orders until manual reset
   * 
   * CONFIGURATION EXAMPLE:
   * ```cpp
   * RiskCfg risk_cfg = {
   *   .max_order_size_ = 100,     // 100 shares per order
   *   .max_position_ = 500,       // +/-500 shares max position
   *   .max_loss_ = -5000.0        // -$5,000 max loss
   * };
   * ```
   */
  struct RiskInfo {
    const PositionInfo *position_info_ = nullptr;  // Current position/PnL
    RiskCfg risk_cfg_;                              // Risk limits

    /*
     * CHECK PRE-TRADE RISK - VALIDATE ORDER BEFORE SENDING
     * =====================================================
     * 
     * Performs pre-trade risk checks on proposed order.
     * 
     * ALGORITHM:
     * 1. Check order size: qty <= max_order_size
     * 2. Check resulting position: |position + qty * side| <= max_position
     * 3. Check loss: total_pnl >= max_loss (not too negative)
     * 4. If all pass: Return ALLOWED
     * 5. If any fail: Return specific failure reason
     * 
     * CHECK 1: ORDER SIZE
     * - Compare: qty vs max_order_size
     * - Example: Order 1000, limit 500 -> ORDER_TOO_LARGE
     * - Purpose: Prevent accidentally large orders
     * 
     * CHECK 2: POSITION LIMIT
     * - Calculate resulting position: position + qty * side_value
     * - Compare: |resulting_position| vs max_position
     * - Example:
     *   - Current: Long 400
     *   - Order: Buy 200
     *   - Resulting: Long 600
     *   - Limit: 500
     *   - Result: POSITION_TOO_LARGE
     * 
     * CHECK 3: LOSS LIMIT
     * - Compare: total_pnl vs max_loss
     * - Example: PnL = -$6,000, limit = -$5,000 -> LOSS_TOO_LARGE
     * - Purpose: Stop trading if losing too much (circuit breaker)
     * 
     * UNLIKELY MACRO:
     * - Hint: Failures are rare (checks usually pass)
     * - Branch prediction: Optimize for ALLOWED path
     * - Performance: Sub-nanosecond branch hint
     * 
     * Parameters:
     * - side: BUY or SELL
     * - qty: Order quantity
     * 
     * Returns:
     * - ALLOWED: All checks passed
     * - ORDER_TOO_LARGE: Order qty too large
     * - POSITION_TOO_LARGE: Resulting position too large
     * - LOSS_TOO_LARGE: Already lost too much
     * 
     * PERFORMANCE:
     * - 3 comparisons: 3-10 ns
     * - Branch hints: UNLIKELY optimizes hot path
     * - Total: 10-50 ns (negligible)
     * 
     * NOEXCEPT:
     * - No exceptions: On critical path
     */
    auto checkPreTradeRisk(Side side, Qty qty) const noexcept {
      // CHECK 1: Order size limit
      // Reject if order quantity exceeds max order size
      if (UNLIKELY(qty > risk_cfg_.max_order_size_))
        return RiskCheckResult::ORDER_TOO_LARGE;
      
      // CHECK 2: Position limit
      // Calculate resulting position after this order
      // position + (qty * side_value), where side_value = +1 (BUY) or -1 (SELL)
      // Check if absolute value exceeds max position
      if (UNLIKELY(std::abs(position_info_->position_ + sideToValue(side) * static_cast<int32_t>(qty)) > 
                   static_cast<int32_t>(risk_cfg_.max_position_)))
        return RiskCheckResult::POSITION_TOO_LARGE;
      
      // CHECK 3: Loss limit
      // Reject if total PnL is below max loss (too negative)
      // Example: PnL = -$6,000, max_loss = -$5,000 -> reject
      if (UNLIKELY(position_info_->total_pnl_ < risk_cfg_.max_loss_))
        return RiskCheckResult::LOSS_TOO_LARGE;

      // All checks passed
      return RiskCheckResult::ALLOWED;
    }

    /*
     * TO STRING - FORMAT RISK INFO
     * =============================
     * 
     * Formats RiskInfo for debugging and monitoring.
     * 
     * Example output:
     * "RiskInfo[pos:Position{...} risk_cfg:RiskCfg{...}]"
     * 
     * Includes:
     * - Current position and PnL
     * - Risk configuration (limits)
     */
    auto toString() const {
      std::stringstream ss;
      ss << "RiskInfo" << "["
         << "pos:" << position_info_->toString() << " "
         << risk_cfg_.toString()
         << "]";

      return ss.str();
    }
  };

  /*
   * TICKER RISK INFO HASH MAP
   * =========================
   * 
   * Array of RiskInfo (one per ticker).
   * 
   * Structure:
   * - std::array indexed by TickerId
   * - Size: ME_MAX_TICKERS (e.g., 256)
   * - Access: O(1) by ticker ID
   * 
   * Usage:
   * ```cpp
   * auto result = ticker_risk_[ticker_id].checkPreTradeRisk(side, qty);
   * ```
   */
  typedef std::array<RiskInfo, ME_MAX_TICKERS> TickerRiskInfoHashMap;

  /*
   * RISK MANAGER - TOP-LEVEL RISK MANAGEMENT
   * =========================================
   * 
   * Manages pre-trade risk checks across all tickers.
   * 
   * RESPONSIBILITIES:
   * - Initialize risk limits (from config)
   * - Link to PositionKeeper (for current position/PnL)
   * - Provide risk check interface
   * 
   * INITIALIZATION:
   * - Read risk config (max order size, position, loss)
   * - Link each RiskInfo to corresponding PositionInfo
   * - Ready for risk checks
   */
  class RiskManager {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Initializes risk manager with position keeper and config.
     * 
     * ALGORITHM:
     * 1. Store logger pointer
     * 2. For each ticker:
     *    a. Link RiskInfo to PositionInfo (from PositionKeeper)
     *    b. Copy risk config from ticker_cfg
     * 3. Ready for risk checks
     * 
     * Parameters:
     * - logger: Async logger (for risk rejections)
     * - position_keeper: Position tracker (for current state)
     * - ticker_cfg: Configuration map (risk limits per ticker)
     * 
     * Implemented in risk_manager.cpp.
     */
    RiskManager(Common::Logger *logger, const PositionKeeper *position_keeper, const TradeEngineCfgHashMap &ticker_cfg);

    /*
     * CHECK PRE-TRADE RISK - VALIDATE ORDER
     * ======================================
     * 
     * Delegates risk check to RiskInfo::checkPreTradeRisk().
     * 
     * Parameters:
     * - ticker_id: Ticker to check
     * - side: BUY or SELL
     * - qty: Order quantity
     * 
     * Returns:
     * - ALLOWED: Can send order
     * - ORDER_TOO_LARGE: Order rejected (too large)
     * - POSITION_TOO_LARGE: Order rejected (position limit)
     * - LOSS_TOO_LARGE: Order rejected (loss limit, stop-out)
     * 
     * Usage:
     * ```cpp
     * auto result = risk_manager.checkPreTradeRisk(0, Side::BUY, 100);
     * if (result == RiskCheckResult::ALLOWED) {
     *   // Send order
     * } else {
     *   // Log rejection
     * }
     * ```
     */
    auto checkPreTradeRisk(TickerId ticker_id, Side side, Qty qty) const noexcept {
      return ticker_risk_.at(ticker_id).checkPreTradeRisk(side, qty);
    }

    // Deleted constructors (prevent accidental copies)
    RiskManager() = delete;
    RiskManager(const RiskManager &) = delete;
    RiskManager(const RiskManager &&) = delete;
    RiskManager &operator=(const RiskManager &) = delete;
    RiskManager &operator=(const RiskManager &&) = delete;

  private:
    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger (risk rejections logged)
    Common::Logger *logger_ = nullptr;

    // Array of RiskInfo (one per ticker)
    // Linked to PositionKeeper for current state
    TickerRiskInfoHashMap ticker_risk_;
  };
}

/*
 * RISK MANAGER DESIGN CONSIDERATIONS
 * ===================================
 * 
 * 1. PRE-TRADE VS POST-TRADE:
 *    - Pre-trade: Before order sent (prevent bad orders)
 *    - Post-trade: After execution (too late, damage done)
 *    - Critical: Pre-trade is regulatory requirement
 * 
 * 2. LATENCY:
 *    - On critical path: Before order submission
 *    - Must be fast: 10-50 ns
 *    - Simple checks: Comparisons only (no complex logic)
 * 
 * 3. FAIL-SAFE:
 *    - Default: Reject if uncertain
 *    - Better: Reject valid order than allow invalid order
 *    - Conservative: Protect capital
 * 
 * 4. CONFIGURATION:
 *    - Per-ticker: Different limits for different instruments
 *    - Dynamic: Could update limits during trading day
 *    - Centralized: Config file, not hardcoded
 * 
 * 5. LOSS LIMIT (STOP-OUT):
 *    - Circuit breaker: Stop trading if losing too much
 *    - Manual reset: Requires human intervention
 *    - Prevents: Runaway losses from bugs
 * 
 * PRODUCTION ENHANCEMENTS:
 * 
 * A) Order Rate Limiting:
 *    - Max orders per second per ticker
 *    - Prevent: Runaway order spam (bug)
 *    - Implementation: Token bucket algorithm
 * 
 * B) Notional Value Limits:
 *    - Max $ value per order (qty * price)
 *    - Example: $100,000 per order
 *    - Purpose: Limit large value orders
 * 
 * C) Concentration Limits:
 *    - Max % of portfolio in one ticker
 *    - Example: 10% of total capital
 *    - Purpose: Diversification
 * 
 * D) Volatility Checks:
 *    - Reject orders if volatility > threshold
 *    - Example: Don't trade if bid-ask spread > 5%
 *    - Purpose: Avoid trading in chaotic markets
 * 
 * E) Time-Based Limits:
 *    - Max position during close (reduce overnight risk)
 *    - Scaled limits: Tighter near market close
 *    - Example: Max position 500 (normal), 100 (last 15 min)
 * 
 * F) Correlation Limits:
 *    - Aggregate position across correlated tickers
 *    - Example: Long SPY + Long QQQ = high correlation
 *    - Purpose: True risk accounting
 * 
 * G) VaR (Value at Risk):
 *    - Statistical measure of potential loss
 *    - 95% confidence: Max loss with 95% probability
 *    - Complex: Requires historical data, covariance
 * 
 * H) Margin/Capital Checks:
 *    - Ensure sufficient capital for order
 *    - Margin requirement: Broker/exchange specific
 *    - Real-time: Query from risk system
 * 
 * I) Kill Switch:
 *    - Immediate stop all trading
 *    - Cancel all open orders
 *    - Flatten all positions (optional)
 *    - Trigger: Manual or automatic (extreme loss)
 * 
 * REGULATORY REQUIREMENTS:
 * - SEC Rule 15c3-5 (Market Access Rule):
 *   • Pre-trade risk controls
 *   • Order size limits
 *   • Capital limits
 *   • Annual review and testing
 * - MiFID II (Europe):
 *   • Pre-trade position limits
 *   • Price collars (reject orders far from market)
 *   • Throttles (max message rate)
 * - CFTC (Futures):
 *   • Position limits per contract
 *   • Accountability levels
 * 
 * COMMON RISK FAILURES:
 * - Fat finger: Accidentally 10000 instead of 100
 * - Runaway algorithm: Bug causing excessive orders
 * - Market impact: Too large order moves market
 * - Concentration: All capital in one ticker
 * - Overnight gap: Large loss from after-hours news
 * - Flash crash: Extreme volatility, wide spreads
 * 
 * RISK REJECTION HANDLING:
 * - Log: Every rejection (audit trail)
 * - Alert: Notify risk team (if unusual pattern)
 * - Analyze: Daily review of rejections
 * - Tune: Adjust limits based on trading patterns
 * 
 * TESTING:
 * - Unit tests: Each risk check independently
 * - Integration: Full order flow with risk
 * - Stress test: Extreme scenarios (large orders, losses)
 * - Regression: Past risk failures (prevent recurrence)
 * - Daily: Verify risk limits still appropriate
 * 
 * MONITORING:
 * - Dashboard: Real-time position/PnL per ticker
 * - Alerts: Approaching limits (80% of max)
 * - Trends: Position/PnL over time
 * - Risk score: Aggregate risk metric
 * - Attribution: PnL per strategy, per trader
 * 
 * DEBUGGING:
 * - Log all risk checks (even ALLOWEDs for analysis)
 * - Compare: Expected vs actual risk check result
 * - Simulate: Replay historical orders with risk checks
 * - Backtesting: How often would limits have been hit?
 */
