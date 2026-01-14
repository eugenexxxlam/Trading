#include <csignal>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <span>
#include <concepts>
#include <chrono>
#include <thread>
#include <random>
#include <algorithm>
#include <iostream>
#include <format> // Fallback to <sstream> if not available

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"
#include "common/logging.h"

namespace Trading::Modern {

// Modern concepts for type safety
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<typename T>
concept StringLike = std::convertible_to<T, std::string_view>;

// Modern configuration structure with designated initializers
struct TradingConfig {
    Common::ClientId client_id;
    AlgoType algo_type;
    std::chrono::milliseconds sleep_time{20};
    
    struct NetworkConfig {
        std::string order_gw_ip = "127.0.0.1";
        std::string order_gw_iface = "lo";
        int order_gw_port = 12345;
        std::string mkt_data_iface = "lo";
        std::string snapshot_ip = "233.252.14.1";
        int snapshot_port = 20000;
        std::string incremental_ip = "233.252.14.3";
        int incremental_port = 20001;
    } network;
};

// Modern RAII wrapper for trading components
class TradingSystem {
private:
    std::unique_ptr<Common::Logger> logger_;
    std::unique_ptr<Trading::TradeEngine> trade_engine_;
    std::unique_ptr<Trading::MarketDataConsumer> market_data_consumer_;
    std::unique_ptr<Trading::OrderGateway> order_gateway_;
    
    // Lock-free queues
    std::unique_ptr<Exchange::ClientRequestLFQueue> client_requests_;
    std::unique_ptr<Exchange::ClientResponseLFQueue> client_responses_;
    std::unique_ptr<Exchange::MEMarketUpdateLFQueue> market_updates_;
    
    TradingConfig config_;

    // Helper for formatting (fallback if std::format not available)
    template<typename... Args>
    std::string format_string(const std::string& fmt, Args&&... args) {
        // If std::format is available, use it; otherwise fallback to sstream
        #ifdef __cpp_lib_format
            return std::format(fmt, std::forward<Args>(args)...);
        #else
            std::ostringstream oss;
            oss << "Trading System Message"; // Simplified fallback
            return oss.str();
        #endif
    }

public:
    explicit TradingSystem(TradingConfig config) 
        : config_{std::move(config)},
          client_requests_{std::make_unique<Exchange::ClientRequestLFQueue>(ME_MAX_CLIENT_UPDATES)},
          client_responses_{std::make_unique<Exchange::ClientResponseLFQueue>(ME_MAX_CLIENT_UPDATES)},
          market_updates_{std::make_unique<Exchange::MEMarketUpdateLFQueue>(ME_MAX_MARKET_UPDATES)} {
        
        initialize_logger();
    }
    
    // Non-copyable, movable
    TradingSystem(const TradingSystem&) = delete;
    TradingSystem& operator=(const TradingSystem&) = delete;
    TradingSystem(TradingSystem&&) = default;
    TradingSystem& operator=(TradingSystem&&) = default;

    void initialize_logger() {
        auto log_filename = "trading_main_" + std::to_string(config_.client_id) + ".log";
        logger_ = std::make_unique<Common::Logger>(std::move(log_filename));
    }

    void start(const TradeEngineCfgHashMap& ticker_cfg) {
        log_info("Starting Trade Engine...");
        trade_engine_ = std::make_unique<Trading::TradeEngine>(
            config_.client_id, 
            config_.algo_type,
            ticker_cfg,
            client_requests_.get(),
            client_responses_.get(),
            market_updates_.get()
        );
        trade_engine_->start();

        log_info("Starting Order Gateway...");
        order_gateway_ = std::make_unique<Trading::OrderGateway>(
            config_.client_id, 
            client_requests_.get(), 
            client_responses_.get(), 
            config_.network.order_gw_ip, 
            config_.network.order_gw_iface, 
            config_.network.order_gw_port
        );
        order_gateway_->start();

        log_info("Starting Market Data Consumer...");
        market_data_consumer_ = std::make_unique<Trading::MarketDataConsumer>(
            config_.client_id, 
            market_updates_.get(), 
            config_.network.mkt_data_iface, 
            config_.network.snapshot_ip, 
            config_.network.snapshot_port, 
            config_.network.incremental_ip, 
            config_.network.incremental_port
        );
        market_data_consumer_->start();

        // Wait for initialization
        std::this_thread::sleep_for(std::chrono::seconds{10});
        trade_engine_->initLastEventTime();
    }

    void stop() {
        log_info("Stopping trading system...");
        
        if (trade_engine_) trade_engine_->stop();
        if (market_data_consumer_) market_data_consumer_->stop();
        if (order_gateway_) order_gateway_->stop();
        
        std::this_thread::sleep_for(std::chrono::seconds{10});
    }

    void run_random_algorithm() {
        if (config_.algo_type != AlgoType::RANDOM) return;

        Common::OrderId order_id = config_.client_id * 1000;
        std::vector<Exchange::MEClientRequest> client_requests_vec;
        
        // Modern random number generation
        std::random_device rd;
        std::mt19937 gen{rd()};
        std::uniform_int_distribution<> ticker_dist{0, Common::ME_MAX_TICKERS - 1};
        std::uniform_int_distribution<> price_offset{1, 10};
        std::uniform_int_distribution<> qty_dist{1, 100};
        std::uniform_int_distribution<> side_dist{0, 1};
        std::uniform_int_distribution<> base_price{100, 199};

        // Initialize base prices
        std::array<Price, ME_MAX_TICKERS> ticker_base_price;
        std::generate(ticker_base_price.begin(), ticker_base_price.end(), 
                     [&] { return base_price(gen); });

        constexpr size_t max_iterations = 10000;
        for (size_t i = 0; i < max_iterations; ++i) {
            const auto ticker_id = static_cast<Common::TickerId>(ticker_dist(gen));
            const Price price = ticker_base_price[ticker_id] + price_offset(gen);
            const Qty qty = static_cast<Qty>(qty_dist(gen));
            const auto side = side_dist(gen) ? Common::Side::BUY : Common::Side::SELL;

            Exchange::MEClientRequest new_request{
                Exchange::ClientRequestType::NEW,     // type_
                config_.client_id,                    // client_id_
                ticker_id,                           // ticker_id_
                order_id++,                          // order_id_
                side,                                // side_
                price,                              // price_
                qty                                 // qty_
            };

            trade_engine_->sendClientRequest(&new_request);
            std::this_thread::sleep_for(config_.sleep_time);

            client_requests_vec.push_back(new_request);
            
            if (!client_requests_vec.empty()) {
                std::uniform_int_distribution<> cancel_dist{0, 
                    static_cast<int>(client_requests_vec.size() - 1)};
                auto cxl_index = cancel_dist(gen);
                auto cxl_request = client_requests_vec[cxl_index];
                cxl_request.type_ = Exchange::ClientRequestType::CANCEL;
                trade_engine_->sendClientRequest(&cxl_request);
                std::this_thread::sleep_for(config_.sleep_time);
            }

            if (trade_engine_->silentSeconds() >= 60) {
                log_info("Stopping early - been silent for 60+ seconds");
                break;
            }
        }
    }

    void wait_for_completion() {
        using namespace std::chrono_literals;
        
        while (trade_engine_->silentSeconds() < 60) {
            log_info("Waiting for activity to stop - silent for " + 
                    std::to_string(trade_engine_->silentSeconds()) + " seconds");
            std::this_thread::sleep_for(30s);
        }
    }

    template<StringLike T>
    void log_info(T&& message) {
        if (logger_) {
            std::string time_str;
            logger_->log("%:% %() % %\n", 
                __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str), 
                std::forward<T>(message));
        }
    }
};

// Modern argument parsing
class ArgumentParser {
public:
    static std::optional<TradingConfig> parse_basic_config(std::span<const char*> args) {
        if (args.size() < 3) return std::nullopt;
        
        TradingConfig config{
            .client_id = static_cast<Common::ClientId>(std::atoi(args[1])),
            .algo_type = stringToAlgoType(args[2])
        };
        
        return config;
    }
    
    static TradeEngineCfgHashMap parse_ticker_config(std::span<const char*> args) {
        TradeEngineCfgHashMap ticker_cfg;
        
        // Skip program name, client_id, algo_type
        size_t ticker_id = 0;
        for (size_t i = 3; i + 4 < args.size(); i += 5) {
            ticker_cfg.at(ticker_id++) = {
                static_cast<Qty>(std::atoi(args[i])),
                std::atof(args[i + 1]),
                {static_cast<Qty>(std::atoi(args[i + 2])),
                 static_cast<Qty>(std::atoi(args[i + 3])),
                 std::atof(args[i + 4])}
            };
        }
        
        return ticker_cfg;
    }
};

} // namespace Trading::Modern

// Modern main function
int main(int argc, char** argv) {
    using namespace Trading::Modern;
    
    // Convert to modern span
    auto args = std::span{const_cast<const char**>(argv), static_cast<size_t>(argc)};
    
    // Parse configuration
    auto config_opt = ArgumentParser::parse_basic_config(args);
    if (!config_opt) {
        std::cerr << "USAGE: " << args[0] 
                  << " CLIENT_ID ALGO_TYPE [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] ...\n";
        return EXIT_FAILURE;
    }
    
    auto config = *config_opt;
    std::srand(static_cast<unsigned int>(config.client_id));
    
    auto ticker_cfg = ArgumentParser::parse_ticker_config(args);
    
    try {
        // RAII - everything is automatically cleaned up
        TradingSystem trading_system{std::move(config)};
        
        trading_system.start(ticker_cfg);
        trading_system.run_random_algorithm();
        trading_system.wait_for_completion();
        trading_system.stop();
        
        std::this_thread::sleep_for(std::chrono::seconds{10});
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}