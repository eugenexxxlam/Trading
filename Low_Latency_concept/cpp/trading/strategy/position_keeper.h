#pragma once

#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include "common/macros.h"
#include "common/types.h"
#include "common/logging.h"

#include "exchange/order_server/client_response.h"

#include "market_order_book.h"

namespace Trading {
  /// PositionInfo tracks the position, pnl (realized and unrealized) and volume for a single trading instrument.
  struct PositionInfo {
    int32_t position_ = 0;
    double real_pnl_ = 0;
    double unreal_pnl_ = 0;
    double total_pnl_ = 0;
    std::array<double, Common::sideToIndex(Common::Side::MAX) + 1> open_vwap_{};
    Common::Qty volume_ = 0;
    const BBO *bbo_ = nullptr;

    /// Returns a string representation of the position information.
    [[nodiscard]] std::string toString() const {
      std::stringstream ss;
      ss << "Position{"
         << "pos:" << position_
         << " u-pnl:" << unreal_pnl_
         << " r-pnl:" << real_pnl_
         << " t-pnl:" << total_pnl_
         << " vol:" << Common::qtyToString(volume_)
         << " vwaps:[" << (position_ ? open_vwap_.at(Common::sideToIndex(Common::Side::BUY)) / std::abs(position_) : 0)
         << "X" << (position_ ? open_vwap_.at(Common::sideToIndex(Common::Side::SELL)) / std::abs(position_) : 0)
         << "] "
         << (bbo_ ? bbo_->toString() : "") << "}";

      return ss.str();
    }

    /// Process an execution and update the position, pnl and volume.
    void addFill(const Exchange::MEClientResponse *client_response, Common::Logger *logger) noexcept {
      const auto old_position = position_;
      const auto side_index = Common::sideToIndex(client_response->side_);
      const auto opp_side_index = Common::sideToIndex(
          client_response->side_ == Common::Side::BUY ? Common::Side::SELL : Common::Side::BUY);
      const auto side_value = Common::sideToValue(client_response->side_);
      
      position_ += client_response->exec_qty_ * side_value;
      volume_ += client_response->exec_qty_;

      if (old_position * Common::sideToValue(client_response->side_) >= 0) {
        // Opened or increased position
        open_vwap_[side_index] += (client_response->price_ * client_response->exec_qty_);
      } else {
        // Decreased position
        handlePositionDecrease(client_response, old_position, side_index, opp_side_index, side_value);
      }

      updateUnrealizedPnl(client_response->price_);
      total_pnl_ = unreal_pnl_ + real_pnl_;

      logFillUpdate(client_response, logger);
    }

    /// Process a change in top-of-book prices (BBO), and update unrealized pnl if there is an open position.
    void updateBBO(const BBO *bbo, Common::Logger *logger) noexcept {
      bbo_ = bbo;

      if (position_ && bbo->bid_price_ != Common::Price_INVALID && bbo->ask_price_ != Common::Price_INVALID) {
        const auto mid_price = (bbo->bid_price_ + bbo->ask_price_) * 0.5;
        const auto old_total_pnl = total_pnl_;
        
        updateUnrealizedPnlFromMidPrice(mid_price);
        total_pnl_ = unreal_pnl_ + real_pnl_;

        if (total_pnl_ != old_total_pnl) {
          logBBOUpdate(logger);
        }
      }
    }

  private:
    /// Handle position decrease logic (closing or flipping position).
    void handlePositionDecrease(const Exchange::MEClientResponse *client_response,
                               int32_t old_position,
                               size_t side_index,
                               size_t opp_side_index,
                               int32_t side_value) noexcept {
      const auto opp_side_vwap = open_vwap_[opp_side_index] / std::abs(old_position);
      open_vwap_[opp_side_index] = opp_side_vwap * std::abs(position_);
      
      real_pnl_ += std::min(static_cast<int32_t>(client_response->exec_qty_), std::abs(old_position)) *
                   (opp_side_vwap - client_response->price_) * side_value;
      
      if (position_ * old_position < 0) {
        // Flipped position to opposite sign
        open_vwap_[side_index] = (client_response->price_ * std::abs(position_));
        open_vwap_[opp_side_index] = 0;
      }
    }

    /// Update unrealized PnL based on current position and price.
    void updateUnrealizedPnl(Common::Price price) noexcept {
      if (!position_) {
        // Flat position
        open_vwap_[Common::sideToIndex(Common::Side::BUY)] = 0;
        open_vwap_[Common::sideToIndex(Common::Side::SELL)] = 0;
        unreal_pnl_ = 0;
      } else {
        if (position_ > 0) {
          unreal_pnl_ = (price - open_vwap_[Common::sideToIndex(Common::Side::BUY)] / std::abs(position_)) *
                        std::abs(position_);
        } else {
          unreal_pnl_ = (open_vwap_[Common::sideToIndex(Common::Side::SELL)] / std::abs(position_) - price) *
                        std::abs(position_);
        }
      }
    }

    /// Update unrealized PnL from mid-price.
    void updateUnrealizedPnlFromMidPrice(double mid_price) noexcept {
      if (position_ > 0) {
        unreal_pnl_ = (mid_price - open_vwap_[Common::sideToIndex(Common::Side::BUY)] / std::abs(position_)) *
                      std::abs(position_);
      } else {
        unreal_pnl_ = (open_vwap_[Common::sideToIndex(Common::Side::SELL)] / std::abs(position_) - mid_price) *
                      std::abs(position_);
      }
    }

    /// Log fill update (extracted to reduce code in hot path).
    void logFillUpdate(const Exchange::MEClientResponse *client_response, Common::Logger *logger) noexcept {
      std::string time_str;
      logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, 
                  Common::getCurrentTimeStr(&time_str),
                  toString(), client_response->toString().c_str());
    }

    /// Log BBO update (extracted to reduce code in hot path).
    void logBBOUpdate(Common::Logger *logger) noexcept {
      std::string time_str;
      logger->log("%:% %() % % %\n", __FILE__, __LINE__, __FUNCTION__, 
                  Common::getCurrentTimeStr(&time_str),
                  toString(), bbo_->toString());
    }
  };

  /// Top level position keeper class to compute position, pnl and volume for all trading instruments.
  class PositionKeeper {
  public:
    explicit PositionKeeper(Common::Logger *logger)
        : logger_(logger) {
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    PositionKeeper() = delete;
    PositionKeeper(const PositionKeeper &) = delete;
    PositionKeeper(PositionKeeper &&) = delete;
    PositionKeeper &operator=(const PositionKeeper &) = delete;
    PositionKeeper &operator=(PositionKeeper &&) = delete;

    /// Process a client fill response and update position tracking.
    void addFill(const Exchange::MEClientResponse *client_response) noexcept {
      ticker_position_.at(client_response->ticker_id_).addFill(client_response, logger_);
    }

    /// Update the best bid/offer for a given ticker.
    void updateBBO(Common::TickerId ticker_id, const BBO *bbo) noexcept {
      ticker_position_.at(ticker_id).updateBBO(bbo, logger_);
    }

    /// Get position information for a specific ticker.
    [[nodiscard]] const PositionInfo* getPositionInfo(Common::TickerId ticker_id) const noexcept {
      return &(ticker_position_.at(ticker_id));
    }

    /// Returns a string representation of all positions and aggregate PnL.
    [[nodiscard]] std::string toString() const {
      double total_pnl = 0;
      Common::Qty total_vol = 0;

      std::stringstream ss;
      for(Common::TickerId i = 0; i < ticker_position_.size(); ++i) {
        ss << "TickerId:" << Common::tickerIdToString(i) << " " 
           << ticker_position_.at(i).toString() << "\n";

        total_pnl += ticker_position_.at(i).total_pnl_;
        total_vol += ticker_position_.at(i).volume_;
      }
      ss << "Total PnL:" << total_pnl << " Vol:" << total_vol << "\n";

      return ss.str();
    }

  private:
    std::string time_str_;
    Common::Logger *logger_ = nullptr;

    /// Container mapping TickerId -> PositionInfo.
    std::array<PositionInfo, ME_MAX_TICKERS> ticker_position_;
  };
}
