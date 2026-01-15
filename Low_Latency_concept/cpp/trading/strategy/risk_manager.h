#pragma once

#include "common/macros.h"
#include "common/logging.h"
#include "position_keeper.h"
#include "om_order.h"

namespace Trading {
  class OrderManager;

  /**
   * @brief Enumeration of pre-trade risk check outcomes
   * 
   * Represents all possible results when validating an order against risk limits.
   * Used throughout the trading system to communicate why orders are rejected
   * or approved before being sent to the exchange.
   */
  enum class RiskCheckResult : int8_t {
    INVALID = 0,          ///< Uninitialized state or system error
    ORDER_TOO_LARGE = 1,  ///< Order quantity exceeds configured maximum
    POSITION_TOO_LARGE = 2, ///< Resulting position would violate size limits
    LOSS_TOO_LARGE = 3,   ///< Current unrealized losses exceed threshold
    ALLOWED = 4           ///< Order passes all risk validation checks
  };

  /**
   * @brief Converts risk check result to human-readable string
   * 
   * @param result Risk check enumeration value to convert
   * @return Pointer to null-terminated string literal
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
    return "UNKNOWN";
  }

  /**
   * @brief Risk validation logic and configuration for a single trading instrument
   * 
   * Encapsulates all risk-related data and validation logic for one financial
   * instrument. Combines current position information with configured risk limits
   * to make real-time trading decisions in microsecond timeframes.
   */
  struct RiskInfo {
    /// Non-owning pointer to current position and P&L data for this instrument
    const PositionInfo *position_info_ = nullptr;
    
    /// Risk configuration parameters (order limits, position limits, loss thresholds)
    RiskCfg risk_cfg_;

    /**
     * @brief Validates proposed order against all configured risk limits
     * 
     * Performs three sequential checks optimized for performance:
     * 1. Order size validation (fastest check)
     * 2. Position limit validation (includes arithmetic)
     * 3. Loss limit validation (P&L threshold check)
     * 
     * @param side Order direction (BUY or SELL)
     * @param qty Number of shares/contracts to trade
     * @return Risk validation result indicating approval or specific rejection reason
     */
    auto checkPreTradeRisk(Common::Side side, Common::Qty qty) const noexcept -> RiskCheckResult {
      // First check: Validate order size against maximum allowed
      if (UNLIKELY(qty > risk_cfg_.max_order_size_))
        return RiskCheckResult::ORDER_TOO_LARGE;
      
      // Second check: Calculate hypothetical new position and validate limits
      if (UNLIKELY(std::abs(position_info_->position_ + Common::sideToValue(side) * static_cast<int32_t>(qty)) > static_cast<int32_t>(risk_cfg_.max_position_)))
        return RiskCheckResult::POSITION_TOO_LARGE;
      
      // Third check: Ensure current losses haven't exceeded maximum threshold
      if (UNLIKELY(position_info_->total_pnl_ < risk_cfg_.max_loss_))
        return RiskCheckResult::LOSS_TOO_LARGE;

      return RiskCheckResult::ALLOWED;
    }

    /**
     * @brief Generates string representation for logging and debugging
     */
    auto toString() const noexcept -> std::string {
      std::stringstream ss;
      ss << "RiskInfo" << "["
         << "pos:" << position_info_->toString() << " "
         << risk_cfg_.toString()
         << "]";
      return ss.str();
    }
  };

  /**
   * @brief Type alias for high-performance ticker-to-risk-info mapping
   * 
   * Uses std::array for O(1) access without hash computation overhead.
   * Array indexing by ticker ID provides predictable performance.
   */
  using TickerRiskInfoHashMap = std::array<RiskInfo, ME_MAX_TICKERS>;

  /**
   * @brief Central orchestrator for risk management across all trading instruments
   * 
   * Manages risk validation for all supported financial instruments, integrating
   * with position tracking and order management to prevent trades that violate
   * risk parameters. Designed for microsecond-level response times.
   */
  class RiskManager {
  public:
    /**
     * @brief Initializes risk management system with configuration and dependencies
     * 
     * @param logger Non-owning pointer to logging system (lifetime managed externally)
     * @param position_keeper Non-owning pointer to position tracking system
     * @param ticker_cfg Configuration map containing risk parameters per instrument
     */
    RiskManager(Common::Logger *logger, const PositionKeeper *position_keeper, const TradeEngineCfgHashMap &ticker_cfg);

    /**
     * @brief Primary interface for real-time order risk validation
     * 
     * Validates a proposed order against all applicable risk limits.
     * Called for every order before submission to exchange.
     * 
     * @param ticker_id Unique identifier for the trading instrument
     * @param side Order direction (BUY or SELL)
     * @param qty Number of shares/contracts to trade
     * @return Risk validation result for order routing decisions
     */
    auto checkPreTradeRisk(Common::TickerId ticker_id, Common::Side side, Common::Qty qty) const noexcept -> RiskCheckResult {
      return ticker_risk_.at(ticker_id).checkPreTradeRisk(side, qty);
    }

    // Prevent copying to avoid accidental duplication of risk management state
    RiskManager() = delete;
    RiskManager(const RiskManager &) = delete;
    RiskManager(const RiskManager &&) = delete;
    RiskManager &operator=(const RiskManager &) = delete;
    RiskManager &operator=(const RiskManager &&) = delete;

  private:
    /// Pre-allocated buffer for timestamp formatting in log messages
    std::string time_str_;
    
    /// Non-owning pointer to logging system (lifetime managed by parent)
    Common::Logger *logger_ = nullptr;
    
    /// High-performance storage for per-instrument risk validation logic
    TickerRiskInfoHashMap ticker_risk_;
  };
}