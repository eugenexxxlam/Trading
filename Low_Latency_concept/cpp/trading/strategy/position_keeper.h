#pragma once

#include <sstream>

#include "common/macros.h"
#include "common/types.h"
#include "common/logging.h"

#include "exchange/order_server/client_response.h"

#include "market_order_book.h"

using namespace Common;

/*
 * POSITION KEEPER - POSITION, PNL, AND VOLUME TRACKING
 * =====================================================
 * 
 * PURPOSE:
 * Tracks trading positions, profit/loss (realized and unrealized), and volume.
 * Essential for risk management and trading decisions.
 * 
 * KEY RESPONSIBILITIES:
 * 1. Position tracking (long/short/flat)
 * 2. PnL calculation (realized + unrealized)
 * 3. Volume tracking (total traded)
 * 4. VWAP tracking (volume-weighted average price)
 * 
 * POSITION:
 * - Long: position > 0 (bought more than sold)
 * - Short: position < 0 (sold more than bought)
 * - Flat: position = 0 (balanced)
 * - Example: Buy 100, Sell 30 -> Position = +70 (long 70 shares)
 * 
 * REALIZED PNL:
 * - Profit from closed positions
 * - Locked in: Cannot change (already executed)
 * - Example: Buy at 100, Sell at 105 -> Realized PnL = +5/share
 * 
 * UNREALIZED PNL:
 * - Profit from open positions (mark-to-market)
 * - Changes: With market price movement
 * - Example: Long 100 at avg 100, market at 102 -> Unrealized PnL = +200
 * 
 * VWAP (Volume-Weighted Average Price):
 * - Average price paid/received (weighted by quantity)
 * - Used for: PnL calculation
 * - Example: Buy 10 at 100, Buy 20 at 102 -> VWAP = (10*100 + 20*102) / 30 = 101.33
 * 
 * ARCHITECTURE:
 * ```
 * Fills (from exchange)
 *       |
 *       v
 * PositionKeeper::addFill() --> Update position, PnL, VWAP
 *       |
 *       v
 * Market Data (BBO changes)
 *       |
 *       v
 * PositionKeeper::updateBBO() --> Update unrealized PnL
 * ```
 * 
 * PERFORMANCE:
 * - Add fill: 100-500 ns (VWAP calc, PnL update)
 * - Update BBO: 50-100 ns (unrealized PnL recalc)
 * - Not hot path: Less frequent than order book updates
 * - Acceptable: Sub-microsecond is fast enough
 */

namespace Trading {
  /*
   * POSITION INFO - PER-TICKER POSITION/PNL TRACKING
   * =================================================
   * 
   * Tracks position, PnL, and volume for a single trading instrument.
   * 
   * FIELDS:
   * 
   * position_:
   * - Current position (int32_t, signed)
   * - Positive: Long (more buys than sells)
   * - Negative: Short (more sells than buys)
   * - Zero: Flat (balanced)
   * 
   * real_pnl_:
   * - Realized profit/loss (closed positions)
   * - Accumulated: Sum of all closed position PnL
   * - Cannot decrease: Only increases when closing positions
   * 
   * unreal_pnl_:
   * - Unrealized profit/loss (open positions, mark-to-market)
   * - Changes: With market price (BBO mid)
   * - Volatile: Can go positive/negative quickly
   * 
   * total_pnl_:
   * - Total PnL = realized + unrealized
   * - Overall performance metric
   * 
   * open_vwap_:
   * - Volume-weighted average price for open position
   * - Array: [BUY_VWAP, SELL_VWAP]
   * - Used for: PnL calculation
   * - Example: Long position uses BUY_VWAP as cost basis
   * 
   * volume_:
   * - Total quantity traded (buys + sells)
   * - Metric: Trading activity level
   * 
   * bbo_:
   * - Pointer to current BBO (for unrealized PnL calc)
   * - Updated: On every BBO change
   * 
   * PNL EXAMPLES:
   * 
   * Example 1: Simple Long
   * - Buy 100 at 100 -> Position = +100, Cost basis = 100
   * - Market at 105 -> Unrealized PnL = (105 - 100) * 100 = +500
   * - Sell 100 at 105 -> Realized PnL = +500, Position = 0
   * 
   * Example 2: Multiple Fills (VWAP)
   * - Buy 10 at 100 -> Position = +10, VWAP = 100
   * - Buy 20 at 102 -> Position = +30, VWAP = (10*100 + 20*102) / 30 = 101.33
   * - Market at 103 -> Unrealized PnL = (103 - 101.33) * 30 = +50
   * 
   * Example 3: Partial Close
   * - Position = +30, VWAP = 101.33
   * - Sell 10 at 104 -> Realized PnL = (104 - 101.33) * 10 = +26.7
   * - Position = +20, VWAP = 101.33 (unchanged for remaining position)
   * 
   * Example 4: Flip Position
   * - Long 30 at 101.33
   * - Sell 50 at 104 -> Close long + open short
   *   - Realized: (104 - 101.33) * 30 = +80
   *   - New position: -20, VWAP = 104
   */
  struct PositionInfo {
    int32_t position_ = 0;          // Current position (signed: +long, -short, 0=flat)
    double real_pnl_ = 0;            // Realized PnL (locked in)
    double unreal_pnl_ = 0;          // Unrealized PnL (mark-to-market)
    double total_pnl_ = 0;           // Total PnL (realized + unrealized)
    
    // VWAP (volume-weighted average price) for open position
    // [sideToIndex(BUY)] = total cost of all buys (qty * price)
    // [sideToIndex(SELL)] = total proceeds of all sells (qty * price)
    // Divide by position size to get average price
    std::array<double, sideToIndex(Side::MAX) + 1> open_vwap_;
    
    Qty volume_ = 0;                 // Total volume traded (buys + sells)
    const BBO *bbo_ = nullptr;       // Pointer to current BBO (for unrealized PnL)

    /*
     * TO STRING - FORMAT POSITION FOR DISPLAY
     * ========================================
     * 
     * Formats PositionInfo for human-readable display.
     * 
     * Example output:
     * "Position{pos:+50 u-pnl:+123.45 r-pnl:+67.89 t-pnl:+191.34 vol:150 vwaps:[101.20X0.00] BBO{...}}"
     * 
     * Fields:
     * - pos: Current position (+long, -short)
     * - u-pnl: Unrealized PnL
     * - r-pnl: Realized PnL
     * - t-pnl: Total PnL
     * - vol: Total volume
     * - vwaps: [buy_vwap X sell_vwap] (per share)
     * - BBO: Current market (if available)
     */
    auto toString() const {
      std::stringstream ss;
      ss << "Position{"
         << "pos:" << position_
         << " u-pnl:" << unreal_pnl_
         << " r-pnl:" << real_pnl_
         << " t-pnl:" << total_pnl_
         << " vol:" << qtyToString(volume_)
         << " vwaps:[" << (position_ ? open_vwap_.at(sideToIndex(Side::BUY)) / std::abs(position_) : 0)
         << "X" << (position_ ? open_vwap_.at(sideToIndex(Side::SELL)) / std::abs(position_) : 0)
         << "] "
         << (bbo_ ? bbo_->toString() : "") << "}";

      return ss.str();
    }

    /*
     * ADD FILL - PROCESS EXECUTION AND UPDATE POSITION/PNL
     * =====================================================
     * 
     * Processes execution report and updates position, PnL, VWAP, volume.
     * Core position tracking logic - handles all execution scenarios.
     * 
     * ALGORITHM:
     * 
     * 1. Store old position (for PnL calculation)
     * 2. Update position: position += exec_qty * side_value (+1 for BUY, -1 for SELL)
     * 3. Update volume: volume += exec_qty
     * 4. Determine if opening or closing position:
     *    a. Opening: old_position * side_value >= 0 (same direction)
     *       - Add to VWAP cost basis
     *    b. Closing: old_position * side_value < 0 (opposite direction)
     *       - Calculate realized PnL
     *       - Reduce VWAP cost basis
     *    c. Flipping: Position crosses zero (close + open opposite)
     *       - Realize PnL on closed portion
     *       - Start new VWAP for opposite direction
     * 5. Update unrealized PnL (if position still open)
     * 6. Update total PnL = realized + unrealized
     * 7. Log position update
     * 
     * VWAP UPDATE (OPENING POSITION):
     * - Add to cost basis: vwap[side] += price * exec_qty
     * - Example: Long 10 at 100, Buy 20 at 102
     *   - vwap[BUY] = 10*100 + 20*102 = 3040
     *   - position = 30
     *   - Avg price = 3040 / 30 = 101.33
     * 
     * REALIZED PNL (CLOSING POSITION):
     * - Calculate PnL: (opposite_vwap - exec_price) * exec_qty * side_value
     * - Example: Long 30 at 101.33, Sell 10 at 104
     *   - PnL = (101.33 - 104) * 10 * (-1) = +26.7
     *   - Realized PnL += 26.7
     * 
     * FLIPPING POSITION:
     * - Close old position (realize PnL)
     * - Open new position opposite direction (new VWAP)
     * - Example: Long 20 at 100, Sell 30 at 105
     *   - Close long 20: PnL = (105 - 100) * 20 = +100
     *   - Open short 10: VWAP = 105 * 10 = 1050
     * 
     * FLAT POSITION (ZERO):
     * - Clear all VWAPs
     * - Unrealized PnL = 0
     * - Only realized PnL remains
     * 
     * UNREALIZED PNL CALCULATION:
     * - Long: (current_price - avg_buy_price) * position
     * - Short: (avg_sell_price - current_price) * |position|
     * - Uses: Execution price as current price (optimistic)
     * - Later: Updated by updateBBO() with market price
     * 
     * Parameters:
     * - client_response: Execution report (FILLED)
     * - logger: For logging position updates
     * 
     * NOEXCEPT:
     * - No exceptions: Position tracking is critical
     * - Errors: Would be logged, not thrown
     * 
     * PERFORMANCE:
     * - Arithmetic: 50-100 ns (additions, multiplications)
     * - Conditionals: Branch prediction (usually consistent direction)
     * - Logging: Async (not on hot path)
     * - Total: 100-500 ns (acceptable, not ultra-latency-critical)
     */
    auto addFill(const Exchange::MEClientResponse *client_response, Logger *logger) noexcept {
      // Store old position (needed for PnL calculation)
      const auto old_position = position_;
      
      // Get side information
      const auto side_index = sideToIndex(client_response->side_);  // 0=BUY, 1=SELL
      const auto opp_side_index = sideToIndex(client_response->side_ == Side::BUY ? Side::SELL : Side::BUY);
      const auto side_value = sideToValue(client_response->side_);  // +1=BUY, -1=SELL
      
      // Update position: BUY adds, SELL subtracts
      position_ += client_response->exec_qty_ * side_value;
      
      // Update volume (total traded, regardless of direction)
      volume_ += client_response->exec_qty_;

      // CASE 1: OPENING OR INCREASING POSITION (same direction)
      // old_position and new fill have same sign (or old_position was zero)
      if (old_position * sideToValue(client_response->side_) >= 0) {
        // Add to VWAP cost basis
        // Example: Long 10 at 100, buy 20 at 102 -> vwap[BUY] = 10*100 + 20*102 = 3040
        open_vwap_[side_index] += (client_response->price_ * client_response->exec_qty_);
      } 
      // CASE 2: CLOSING OR REDUCING POSITION (opposite direction)
      else {
        // Calculate opposite side VWAP (cost basis for old position)
        const auto opp_side_vwap = open_vwap_[opp_side_index] / std::abs(old_position);
        
        // Reduce cost basis proportionally (position getting smaller)
        open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
        
        // Realize PnL on closed portion
        // PnL = (avg_price - exec_price) * closed_qty * side_value
        // Example: Long 30 at 101.33, Sell 10 at 104
        //   -> PnL = (101.33 - 104) * min(10, 30) * (-1) = +26.7
        real_pnl_ += std::min(static_cast<int32_t>(client_response->exec_qty_), std::abs(old_position)) *
                     (opp_side_vwap - client_response->price_) * sideToValue(client_response->side_);
        
        // CASE 2a: FLIPPED POSITION (crossed zero to opposite side)
        if (position_ * old_position < 0) {
          // Start new VWAP for opposite direction
          // Remaining qty after closing = exec_qty - |old_position|
          open_vwap_[side_index] = (client_response->price_ * std::abs(position_));
          
          // Clear opposite side VWAP (position fully closed)
          open_vwap_[opp_side_index] = 0;
        }
      }

      // FLAT POSITION (exactly zero)
      if (!position_) {
        // Clear all VWAPs (no open position)
        open_vwap_[sideToIndex(Side::BUY)] = open_vwap_[sideToIndex(Side::SELL)] = 0;
        
        // No unrealized PnL (no open position)
        unreal_pnl_ = 0;
      } 
      // OPEN POSITION (non-zero)
      else {
        // Calculate unrealized PnL based on execution price
        // (This is optimistic; will be updated by updateBBO() with market price)
        if (position_ > 0) {
          // Long position: PnL = (current - avg_buy) * qty
          unreal_pnl_ =
              (client_response->price_ - open_vwap_[sideToIndex(Side::BUY)] / std::abs(position_)) *
              std::abs(position_);
        } else {
          // Short position: PnL = (avg_sell - current) * qty
          unreal_pnl_ =
              (open_vwap_[sideToIndex(Side::SELL)] / std::abs(position_) - client_response->price_) *
              std::abs(position_);
        }
      }

      // Update total PnL (realized + unrealized)
      total_pnl_ = unreal_pnl_ + real_pnl_;

      // Log position update
      std::string time_str;
      logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                  toString(), client_response->toString().c_str());
    }

    /*
     * UPDATE BBO - RECALCULATE UNREALIZED PNL
     * ========================================
     * 
     * Called when BBO (best bid/offer) changes.
     * Recalculates unrealized PnL based on current market price.
     * 
     * ALGORITHM:
     * 1. Store BBO pointer (for display)
     * 2. If position open and valid BBO:
     *    a. Calculate mid price: (bid + ask) / 2
     *    b. If long: unreal_pnl = (mid - avg_buy) * qty
     *    c. If short: unreal_pnl = (avg_sell - mid) * qty
     * 3. Update total PnL = realized + unrealized
     * 4. Log if PnL changed
     * 
     * MID PRICE:
     * - Use: (bid + ask) / 2 for mark-to-market
     * - Fair: Mid point between buy and sell
     * - Alternative: Could use bid (long) or ask (short) for conservative
     * 
     * EXAMPLE:
     * - Position: Long 100 at avg 100.50
     * - BBO: 100@101.00 X 101.10@50
     * - Mid: (101.00 + 101.10) / 2 = 101.05
     * - Unrealized PnL: (101.05 - 100.50) * 100 = +55
     * 
     * PERFORMANCE:
     * - Mid calc: 10 ns
     * - PnL calc: 20-30 ns
     * - Total: 50-100 ns (very fast)
     * 
     * UPDATE FREQUENCY:
     * - Every BBO change (1000-10000/sec)
     * - Only if position open
     * - Only if BBO valid (both bid and ask present)
     * 
     * Parameters:
     * - bbo: Current BBO (best bid and offer)
     * - logger: For logging PnL changes
     * 
     * NOEXCEPT:
     * - No exceptions: Performance-critical
     */
    auto updateBBO(const BBO *bbo, Logger *logger) noexcept {
      std::string time_str;
      
      // Store BBO pointer (for toString())
      bbo_ = bbo;

      // Only update if position open and BBO valid
      if (position_ && bbo->bid_price_ != Price_INVALID && bbo->ask_price_ != Price_INVALID) {
        // Calculate mid price (mark-to-market price)
        const auto mid_price = (bbo->bid_price_ + bbo->ask_price_) * 0.5;
        
        // Calculate unrealized PnL based on position direction
        if (position_ > 0) {
          // Long position: PnL = (current_mid - avg_buy_price) * qty
          unreal_pnl_ =
              (mid_price - open_vwap_[sideToIndex(Side::BUY)] / std::abs(position_)) *
              std::abs(position_);
        } else {
          // Short position: PnL = (avg_sell_price - current_mid) * qty
          unreal_pnl_ =
              (open_vwap_[sideToIndex(Side::SELL)] / std::abs(position_) - mid_price) *
              std::abs(position_);
        }

        // Store old total PnL (for change detection)
        const auto old_total_pnl = total_pnl_;
        
        // Update total PnL
        total_pnl_ = unreal_pnl_ + real_pnl_;

        // Log only if PnL changed (reduce logging volume)
        if (total_pnl_ != old_total_pnl)
          logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                      toString(), bbo_->toString());
      }
    }
  };

  /*
   * POSITION KEEPER - MULTI-TICKER POSITION TRACKER
   * ================================================
   * 
   * Top-level class managing positions across all trading instruments.
   * 
   * RESPONSIBILITIES:
   * - Track position/PnL for each ticker
   * - Aggregate portfolio-level metrics
   * - Provide position queries
   * 
   * STRUCTURE:
   * - Array: One PositionInfo per ticker
   * - Index: TickerId (0, 1, 2, ...)
   * - Fast: O(1) access by ticker
   */
  class PositionKeeper {
  public:
    /*
     * CONSTRUCTOR
     * ===========
     * 
     * Initializes position keeper with logger.
     * 
     * Parameters:
     * - logger: Async logger for position updates
     * 
     * Initial state:
     * - All positions: Zero (flat)
     * - All PnL: Zero
     */
    PositionKeeper(Common::Logger *logger)
        : logger_(logger) {
      // All PositionInfo initialized to zero
    }

    // Deleted constructors (prevent accidental copies)
    PositionKeeper() = delete;
    PositionKeeper(const PositionKeeper &) = delete;
    PositionKeeper(const PositionKeeper &&) = delete;
    PositionKeeper &operator=(const PositionKeeper &) = delete;
    PositionKeeper &operator=(const PositionKeeper &&) = delete;

  private:
    // Temporary string for logging (reuse to avoid allocation)
    std::string time_str_;
    
    // Async logger
    Common::Logger *logger_ = nullptr;

    // Array of PositionInfo (one per ticker)
    // Index: TickerId
    // Size: ME_MAX_TICKERS (e.g., 256)
    std::array<PositionInfo, ME_MAX_TICKERS> ticker_position_;

  public:
    /*
     * ADD FILL - PROCESS EXECUTION FOR TICKER
     * ========================================
     * 
     * Delegates execution processing to PositionInfo::addFill().
     * 
     * Parameters:
     * - client_response: Execution report (must be FILLED type)
     * 
     * Usage:
     * ```cpp
     * if (client_response->type_ == ClientResponseType::FILLED) {
     *   position_keeper.addFill(client_response);
     * }
     * ```
     */
    auto addFill(const Exchange::MEClientResponse *client_response) noexcept {
      ticker_position_.at(client_response->ticker_id_).addFill(client_response, logger_);
    }

    /*
     * UPDATE BBO - UPDATE UNREALIZED PNL FOR TICKER
     * ==============================================
     * 
     * Delegates BBO update to PositionInfo::updateBBO().
     * 
     * Parameters:
     * - ticker_id: Ticker to update
     * - bbo: Current BBO
     * 
     * Called: On every order book update affecting BBO
     */
    auto updateBBO(TickerId ticker_id, const BBO *bbo) noexcept {
      ticker_position_.at(ticker_id).updateBBO(bbo, logger_);
    }

    /*
     * GET POSITION INFO - QUERY POSITION FOR TICKER
     * ==============================================
     * 
     * Returns: Pointer to PositionInfo for specified ticker
     * 
     * Used by:
     * - Risk manager (position limit checks)
     * - Trading strategies (position awareness)
     * - Monitoring (display positions)
     * 
     * Returns: const PositionInfo* (read-only access)
     */
    auto getPositionInfo(TickerId ticker_id) const noexcept {
      return &(ticker_position_.at(ticker_id));
    }

    /*
     * TO STRING - FORMAT ALL POSITIONS
     * =================================
     * 
     * Formats all ticker positions and portfolio totals.
     * 
     * Output:
     * - Per-ticker positions
     * - Total portfolio PnL
     * - Total volume
     * 
     * Used for:
     * - End-of-day reporting
     * - Position reconciliation
     * - Performance analysis
     */
    auto toString() const {
      double total_pnl = 0;    // Portfolio total PnL
      Qty total_vol = 0;       // Portfolio total volume

      std::stringstream ss;
      
      // Print each ticker's position
      for(TickerId i = 0; i < ticker_position_.size(); ++i) {
        ss << "TickerId:" << tickerIdToString(i) << " " << ticker_position_.at(i).toString() << "\n";

        // Aggregate portfolio totals
        total_pnl += ticker_position_.at(i).total_pnl_;
        total_vol += ticker_position_.at(i).volume_;
      }
      
      // Print portfolio totals
      ss << "Total PnL:" << total_pnl << " Vol:" << total_vol << "\n";

      return ss.str();
    }
  };
}

/*
 * POSITION KEEPER DESIGN CONSIDERATIONS
 * ======================================
 * 
 * 1. VWAP CALCULATION:
 *    - Accurate: Accounts for all fills at different prices
 *    - Example: Buy 10@100, 20@102, 30@104 -> VWAP = 102.67
 *    - Critical: Correct PnL calculation
 * 
 * 2. REALIZED VS UNREALIZED PNL:
 *    - Realized: Locked in (cannot change)
 *    - Unrealized: Mark-to-market (fluctuates)
 *    - Total: Sum of both (overall performance)
 * 
 * 3. POSITION FLIPPING:
 *    - Complex: Long -> Short (or vice versa) in one fill
 *    - Handling: Close old position + open new position
 *    - Correct: Realize PnL on closed portion
 * 
 * 4. PARTIAL FILLS:
 *    - Common: Orders filled in multiple pieces
 *    - VWAP: Accumulated correctly
 *    - Position: Updated incrementally
 * 
 * 5. MARK-TO-MARKET:
 *    - Mid price: Fair value for PnL calculation
 *    - Conservative: Could use bid (long) or ask (short)
 *    - Standard: Mid price is industry standard
 * 
 * PNL CALCULATION ACCURACY:
 * - Floating-point: Double precision (64-bit)
 * - Rounding: ~15 decimal digits precision
 * - Acceptable: For typical trading (~$0.01 accuracy)
 * - Large positions: May accumulate rounding errors
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Fee tracking: Commission, exchange fees
 * - Trade attribution: Per-strategy PnL
 * - Intraday vs overnight: Separate tracking
 * - High-water mark: Track peak PnL (drawdown analysis)
 * - Sharpe ratio: Risk-adjusted performance
 * - Max drawdown: Largest peak-to-trough decline
 * - Win rate: Percentage of profitable trades
 * - Avg win/loss: Mean profit per trade
 * - Risk metrics: VaR, expected shortfall
 * 
 * REGULATORY REQUIREMENTS:
 * - Position limits: SEC, CFTC, exchange-specific
 * - Reporting: Real-time position reporting
 * - Reconciliation: Daily position reconciliation with broker
 * - Audit trail: All position changes logged
 * 
 * COMMON BUGS:
 * - VWAP overflow: Large qty * large price
 * - Division by zero: Position zero but accessing VWAP
 * - Sign errors: Long/short confusion
 * - Flipping logic: Incorrect PnL on position flip
 * - Rounding errors: Accumulated over many trades
 * 
 * DEBUGGING:
 * - Log every fill: Inspect addFill() logic
 * - Manual calculation: Verify PnL by hand
 * - Position reconciliation: Compare with broker
 * - PnL attribution: Track per-trade PnL
 * - Unit tests: Test edge cases (flip, partial, etc.)
 */
