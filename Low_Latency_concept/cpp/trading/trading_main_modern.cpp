#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
#include <csignal>

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"
#include "common/logging.h"

using namespace std::chrono_literals;

//=============================================================================
// CONFIGURATION CONSTANTS - Type-safe, Named, Documented
//=============================================================================

namespace Config {
    // Timing constants
    inline constexpr auto INITIALIZATION_WAIT = 10s;
    inline constexpr auto SILENCE_THRESHOLD = 60s;
    inline constexpr auto POLLING_INTERVAL = 30s;
    inline constexpr auto SHUTDOWN_GRACE_PERIOD = 10s;
    inline constexpr auto RANDOM_ORDER_INTERVAL = 20ms;
    
    // Capacity constants
    inline constexpr size_t RANDOM_ORDER_COUNT = 10'000;
    inline constexpr size_t CLIENT_ID_MULTIPLIER = 1000;
    
    // Connection parameters (could be moved to config file)
    inline constexpr std::string_view ORDER_GATEWAY_IP = "127.0.0.1";
    inline constexpr std::string_view ORDER_GATEWAY_IFACE = "lo";
    inline constexpr int ORDER_GATEWAY_PORT = 12345;
    
    inline constexpr std::string_view MARKET_DATA_IFACE = "lo";
    inline constexpr std::string_view SNAPSHOT_IP = "233.252.14.1";
    inline constexpr int SNAPSHOT_PORT = 20000;
    inline constexpr std::string_view INCREMENTAL_IP = "233.252.14.3";
    inline constexpr int INCREMENTAL_PORT = 20001;
    
    // Random trading parameters
    inline constexpr Price RANDOM_BASE_PRICE_MIN = 100;
    inline constexpr Price RANDOM_BASE_PRICE_MAX = 200;
    inline constexpr Price RANDOM_PRICE_OFFSET_MAX = 10;
    inline constexpr Qty RANDOM_QTY_MIN = 1;
    inline constexpr Qty RANDOM_QTY_MAX = 100;
}

//=============================================================================
// COMMAND-LINE CONFIGURATION - Type-safe parsing
//=============================================================================

struct TickerConfig {
    Qty clip;
    double threshold;
    RiskCfg risk_cfg;
    
    // Validate configuration
    [[nodiscard]] auto isValid() const noexcept -> bool {
        return clip > 0 
            && threshold > 0.0 
            && risk_cfg.max_order_size_ > 0
            && risk_cfg.max_position_ > 0;
    }
};

struct ProgramConfig {
    Common::ClientId client_id;
    AlgoType algo_type;
    std::vector<TickerConfig> ticker_configs;
    
    [[nodiscard]] auto isValid() const noexcept -> bool {
        if (ticker_configs.empty()) return false;
        for (const auto& cfg : ticker_configs) {
            if (!cfg.isValid()) return false;
        }
        return true;
    }
};

//=============================================================================
// COMMAND-LINE PARSER - Modern C++20 with error handling
//=============================================================================

class CommandLineParser {
public:
    [[nodiscard]] static auto parse(std::span<const char* const> args) 
        -> std::optional<ProgramConfig> {
        
        // Need at least: program_name client_id algo_type
        if (args.size() < 3) {
            printUsage();
            return std::nullopt;
        }
        
        ProgramConfig config;
        
        // Parse client ID
        if (auto client_id = parseClientId(args[1])) {
            config.client_id = *client_id;
        } else {
            return std::nullopt;
        }
        
        // Parse algorithm type
        if (auto algo = parseAlgoType(args[2])) {
            config.algo_type = *algo;
        } else {
            return std::nullopt;
        }
        
        // Parse ticker configurations (groups of 5)
        if (auto configs = parseTickerConfigs(args.subspan(3))) {
            config.ticker_configs = std::move(*configs);
        } else {
            return std::nullopt;
        }
        
        return config.isValid() ? std::optional{config} : std::nullopt;
    }
    
private:
    [[nodiscard]] static auto parseClientId(const char* arg) 
        -> std::optional<Common::ClientId> {
        try {
            int value = std::stoi(arg);
            if (value < 0 || value > 255) {
                std::cerr << "Error: CLIENT_ID must be 0-255\n";
                return std::nullopt;
            }
            return static_cast<Common::ClientId>(value);
        } catch (const std::exception& e) {
            std::cerr << "Error parsing CLIENT_ID: " << e.what() << "\n";
            return std::nullopt;
        }
    }
    
    [[nodiscard]] static auto parseAlgoType(const char* arg) 
        -> std::optional<AlgoType> {
        auto type = stringToAlgoType(arg);
        if (type == AlgoType::INVALID) {
            std::cerr << "Error: ALGO_TYPE must be MAKER, TAKER, or RANDOM\n";
            return std::nullopt;
        }
        return type;
    }
    
    [[nodiscard]] static auto parseTickerConfigs(std::span<const char* const> args)
        -> std::optional<std::vector<TickerConfig>> {
        
        // Must be groups of 5
        if (args.size() % 5 != 0) {
            std::cerr << "Error: Ticker config must be groups of 5 values\n";
            std::cerr << "       [CLIP THRESH MAX_ORDER MAX_POS MAX_LOSS] ...\n";
            return std::nullopt;
        }
        
        std::vector<TickerConfig> configs;
        configs.reserve(args.size() / 5);
        
        try {
            for (size_t i = 0; i < args.size(); i += 5) {
                TickerConfig cfg{
                    .clip = static_cast<Qty>(std::stoi(args[i])),
                    .threshold = std::stod(args[i + 1]),
                    .risk_cfg = {
                        .max_order_size_ = static_cast<Qty>(std::stoi(args[i + 2])),
                        .max_position_ = static_cast<Qty>(std::stoi(args[i + 3])),
                        .max_loss_ = std::stod(args[i + 4])
                    }
                };
                
                if (!cfg.isValid()) {
                    std::cerr << "Error: Invalid ticker config at index " << i/5 << "\n";
                    return std::nullopt;
                }
                
                configs.push_back(cfg);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing ticker config: " << e.what() << "\n";
            return std::nullopt;
        }
        
        return configs;
    }
    
    static auto printUsage() -> void {
        std::cerr << 
            "USAGE: trading_main CLIENT_ID ALGO_TYPE [TICKER_CONFIGS...]\n"
            "\n"
            "Arguments:\n"
            "  CLIENT_ID      : Unique client identifier (0-255)\n"
            "  ALGO_TYPE      : Trading algorithm (MAKER, TAKER, RANDOM)\n"
            "  TICKER_CONFIGS : Repeating groups of 5 values per ticker:\n"
            "                   CLIP THRESH MAX_ORDER MAX_POS MAX_LOSS\n"
            "\n"
            "Example:\n"
            "  ./trading_main 1 MAKER 10 0.25 100 500 -5000.0 20 0.30 200 1000 -10000.0\n"
            "\n"
            "  Client ID: 1\n"
            "  Algorithm: Market Maker\n"
            "  Ticker 0: clip=10, thresh=0.25, max_order=100, max_pos=500, max_loss=-5000\n"
            "  Ticker 1: clip=20, thresh=0.30, max_order=200, max_pos=1000, max_loss=-10000\n";
    }
};

//=============================================================================
// TRADING APPLICATION - RAII-based, Exception-safe
//=============================================================================

class TradingApplication {
public:
    explicit TradingApplication(ProgramConfig config) 
        : config_(std::move(config)) {
        
        // Seed RNG for RANDOM algo (deterministic per client)
        srand(config_.client_id);
        
        // Initialize logger
        logger_ = std::make_unique<Common::Logger>(
            "trading_main_" + std::to_string(config_.client_id) + ".log"
        );
        
        // Create lock-free queues (on stack, passed by pointer to components)
        // Note: These are NOT owned by components, so raw pointers are correct
        client_requests_ = std::make_unique<Exchange::ClientRequestLFQueue>(ME_MAX_CLIENT_UPDATES);
        client_responses_ = std::make_unique<Exchange::ClientResponseLFQueue>(ME_MAX_CLIENT_UPDATES);
        market_updates_ = std::make_unique<Exchange::MEMarketUpdateLFQueue>(ME_MAX_MARKET_UPDATES);
        
        // Convert vector of TickerConfig to TradeEngineCfgHashMap
        auto ticker_cfg_map = createTickerConfigMap();
        
        // Create components (smart pointers for ownership)
        trade_engine_ = std::make_unique<Trading::TradeEngine>(
            config_.client_id,
            config_.algo_type,
            ticker_cfg_map,
            client_requests_.get(),   // Raw pointer: non-owning reference
            client_responses_.get(),  // Raw pointer: non-owning reference
            market_updates_.get()     // Raw pointer: non-owning reference
        );
        
        order_gateway_ = std::make_unique<Trading::OrderGateway>(
            config_.client_id,
            client_requests_.get(),
            client_responses_.get(),
            std::string(Config::ORDER_GATEWAY_IP),
            std::string(Config::ORDER_GATEWAY_IFACE),
            Config::ORDER_GATEWAY_PORT
        );
        
        market_data_consumer_ = std::make_unique<Trading::MarketDataConsumer>(
            config_.client_id,
            market_updates_.get(),
            std::string(Config::MARKET_DATA_IFACE),
            std::string(Config::SNAPSHOT_IP),
            Config::SNAPSHOT_PORT,
            std::string(Config::INCREMENTAL_IP),
            Config::INCREMENTAL_PORT
        );
    }
    
    // Destructor automatically cleans up all resources (RAII)
    ~TradingApplication() {
        // Components stopped in reverse order of creation
        // Smart pointers automatically delete in correct order
    }
    
    // No copy/move (application is unique)
    TradingApplication(const TradingApplication&) = delete;
    TradingApplication& operator=(const TradingApplication&) = delete;
    TradingApplication(TradingApplication&&) = delete;
    TradingApplication& operator=(TradingApplication&&) = delete;
    
    auto run() -> int {
        try {
            startComponents();
            waitForInitialization();
            
            if (config_.algo_type == AlgoType::RANDOM) {
                runRandomTrading();
            }
            
            waitForSilence();
            stopComponents();
            
            return EXIT_SUCCESS;
            
        } catch (const std::exception& e) {
            std::cerr << "Fatal error: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
    
private:
    auto createTickerConfigMap() -> TradeEngineCfgHashMap {
        TradeEngineCfgHashMap map;
        for (size_t i = 0; i < config_.ticker_configs.size(); ++i) {
            const auto& cfg = config_.ticker_configs[i];
            map.at(i) = TradeEngineCfg{
                .clip_ = cfg.clip,
                .threshold_ = cfg.threshold,
                .risk_cfg_ = cfg.risk_cfg
            };
        }
        return map;
    }
    
    auto startComponents() -> void {
        std::string time_str;
        
        logger_->log("%:% %() % Starting Trade Engine...\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str));
        trade_engine_->start();
        
        logger_->log("%:% %() % Starting Order Gateway...\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str));
        order_gateway_->start();
        
        logger_->log("%:% %() % Starting Market Data Consumer...\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str));
        market_data_consumer_->start();
    }
    
    auto waitForInitialization() -> void {
        std::this_thread::sleep_for(Config::INITIALIZATION_WAIT);
        trade_engine_->initLastEventTime();
    }
    
    auto runRandomTrading() -> void {
        std::string time_str;
        Common::OrderId order_id = config_.client_id * Config::CLIENT_ID_MULTIPLIER;
        std::vector<Exchange::MEClientRequest> sent_orders;
        sent_orders.reserve(Config::RANDOM_ORDER_COUNT);
        
        // Random base prices per ticker
        std::array<Price, ME_MAX_TICKERS> base_prices{};
        for (auto& price : base_prices) {
            price = (rand() % (Config::RANDOM_BASE_PRICE_MAX - Config::RANDOM_BASE_PRICE_MIN)) 
                  + Config::RANDOM_BASE_PRICE_MIN;
        }
        
        for (size_t i = 0; i < Config::RANDOM_ORDER_COUNT; ++i) {
            // Generate random order
            const auto ticker_id = static_cast<Common::TickerId>(rand() % ME_MAX_TICKERS);
            const auto price = base_prices[ticker_id] + (rand() % Config::RANDOM_PRICE_OFFSET_MAX) + 1;
            const auto qty = static_cast<Qty>(Config::RANDOM_QTY_MIN + (rand() % Config::RANDOM_QTY_MAX));
            const auto side = (rand() % 2) ? Common::Side::BUY : Common::Side::SELL;
            
            Exchange::MEClientRequest new_order{
                .type_ = Exchange::ClientRequestType::NEW,
                .client_id_ = config_.client_id,
                .ticker_id_ = ticker_id,
                .order_id_ = order_id++,
                .side_ = side,
                .price_ = price,
                .qty_ = qty
            };
            
            trade_engine_->sendClientRequest(&new_order);
            std::this_thread::sleep_for(Config::RANDOM_ORDER_INTERVAL);
            
            sent_orders.push_back(new_order);
            
            // Random cancel
            if (!sent_orders.empty()) {
                const auto cancel_idx = rand() % sent_orders.size();
                auto cancel_order = sent_orders[cancel_idx];
                cancel_order.type_ = Exchange::ClientRequestType::CANCEL;
                
                trade_engine_->sendClientRequest(&cancel_order);
                std::this_thread::sleep_for(Config::RANDOM_ORDER_INTERVAL);
            }
            
            // Check for silence
            if (trade_engine_->silentSeconds() >= Config::SILENCE_THRESHOLD.count()) {
                logger_->log("%:% %() % Stopping early - silent for % seconds\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str),
                           trade_engine_->silentSeconds());
                break;
            }
        }
    }
    
    auto waitForSilence() -> void {
        std::string time_str;
        
        while (trade_engine_->silentSeconds() < Config::SILENCE_THRESHOLD.count()) {
            logger_->log("%:% %() % Waiting for silence - been silent for % seconds\n",
                        __FILE__, __LINE__, __FUNCTION__,
                        Common::getCurrentTimeStr(&time_str),
                        trade_engine_->silentSeconds());
            std::this_thread::sleep_for(Config::POLLING_INTERVAL);
        }
    }
    
    auto stopComponents() -> void {
        trade_engine_->stop();
        market_data_consumer_->stop();
        order_gateway_->stop();
        
        // Grace period for shutdown
        std::this_thread::sleep_for(Config::SHUTDOWN_GRACE_PERIOD);
    }
    
    // Configuration
    ProgramConfig config_;
    
    // Components (smart pointers for ownership - zero overhead!)
    std::unique_ptr<Common::Logger> logger_;
    std::unique_ptr<Trading::TradeEngine> trade_engine_;
    std::unique_ptr<Trading::OrderGateway> order_gateway_;
    std::unique_ptr<Trading::MarketDataConsumer> market_data_consumer_;
    
    // Lock-free queues (owned by application, passed to components)
    std::unique_ptr<Exchange::ClientRequestLFQueue> client_requests_;
    std::unique_ptr<Exchange::ClientResponseLFQueue> client_responses_;
    std::unique_ptr<Exchange::MEMarketUpdateLFQueue> market_updates_;
};

//=============================================================================
// MAIN ENTRY POINT - Clean and simple
//=============================================================================

auto main(int argc, char** argv) -> int {
    // Parse command line
    auto config = CommandLineParser::parse(
        std::span{const_cast<const char* const*>(argv), static_cast<size_t>(argc)}
    );
    
    if (!config) {
        return EXIT_FAILURE;
    }
    
    // Create and run application (RAII handles all cleanup)
    TradingApplication app(std::move(*config));
    return app.run();
}

