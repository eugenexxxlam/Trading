#include <csignal>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>
#include <concepts>
#include <span>

#include "matcher/matching_engine.h"
#include "market_data/market_data_publisher.h"
#include "order_server/order_server.h"

namespace Exchange::Modern {

// Modern concepts for type safety
template<typename T>
concept StringLike = std::convertible_to<T, std::string_view>;

// Modern configuration structure
struct ExchangeConfig {
    std::string log_filename = "exchange_main.log";
    std::chrono::microseconds sleep_time{100'000}; // 100ms in microseconds
    
    struct NetworkConfig {
        std::string order_gw_iface = "lo";
        int order_gw_port = 12345;
        std::string mkt_pub_iface = "lo";
        std::string snap_pub_ip = "233.252.14.1";
        std::string inc_pub_ip = "233.252.14.3";
        int snap_pub_port = 20000;
        int inc_pub_port = 20001;
    } network;
};

// Modern RAII Exchange System
class ExchangeSystem {
private:
    std::unique_ptr<Common::Logger> logger_;
    std::unique_ptr<Exchange::MatchingEngine> matching_engine_;
    std::unique_ptr<Exchange::MarketDataPublisher> market_data_publisher_;
    std::unique_ptr<Exchange::OrderServer> order_server_;
    
    // Lock-free queues - managed with RAII
    std::unique_ptr<Exchange::ClientRequestLFQueue> client_requests_;
    std::unique_ptr<Exchange::ClientResponseLFQueue> client_responses_;
    std::unique_ptr<Exchange::MEMarketUpdateLFQueue> market_updates_;
    
    ExchangeConfig config_;
    std::atomic<bool> should_stop_{false};

public:
    explicit ExchangeSystem(ExchangeConfig config = {}) 
        : config_{std::move(config)},
          client_requests_{std::make_unique<Exchange::ClientRequestLFQueue>(ME_MAX_CLIENT_UPDATES)},
          client_responses_{std::make_unique<Exchange::ClientResponseLFQueue>(ME_MAX_CLIENT_UPDATES)},
          market_updates_{std::make_unique<Exchange::MEMarketUpdateLFQueue>(ME_MAX_MARKET_UPDATES)} {
        
        initialize_logger();
        setup_signal_handling();
    }
    
    // Non-copyable, movable
    ExchangeSystem(const ExchangeSystem&) = delete;
    ExchangeSystem& operator=(const ExchangeSystem&) = delete;
    ExchangeSystem(ExchangeSystem&&) = default;
    ExchangeSystem& operator=(ExchangeSystem&&) = default;

    ~ExchangeSystem() {
        shutdown();
    }

private:
    void initialize_logger() {
        logger_ = std::make_unique<Common::Logger>(config_.log_filename);
    }

    // Modern signal handling with lambda
    void setup_signal_handling() {
        // Note: We need to store a reference to this object for signal handler
        // This is a compromise for signal safety
        std::signal(SIGINT, [](int) {
            // In a real system, you'd use a more sophisticated approach
            // like self-pipe trick or signalfd for true async-signal safety
            std::exit(EXIT_SUCCESS);
        });
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

public:
    void start() {
        log_info("Starting Matching Engine...");
        matching_engine_ = std::make_unique<Exchange::MatchingEngine>(
            client_requests_.get(), 
            client_responses_.get(), 
            market_updates_.get()
        );
        matching_engine_->start();

        log_info("Starting Market Data Publisher...");
        market_data_publisher_ = std::make_unique<Exchange::MarketDataPublisher>(
            market_updates_.get(), 
            config_.network.mkt_pub_iface, 
            config_.network.snap_pub_ip, 
            config_.network.snap_pub_port, 
            config_.network.inc_pub_ip, 
            config_.network.inc_pub_port
        );
        market_data_publisher_->start();

        log_info("Starting Order Server...");
        order_server_ = std::make_unique<Exchange::OrderServer>(
            client_requests_.get(), 
            client_responses_.get(), 
            config_.network.order_gw_iface, 
            config_.network.order_gw_port
        );
        order_server_->start();

        log_info("Exchange System started successfully!");
    }

    void run() {
        using namespace std::chrono_literals;
        
        while (!should_stop_.load(std::memory_order_relaxed)) {
            log_info("Exchange running - sleeping for a few milliseconds...");
            std::this_thread::sleep_for(config_.sleep_time);
        }
    }

    void stop() {
        should_stop_.store(true, std::memory_order_relaxed);
    }

    void shutdown() {
        using namespace std::chrono_literals;
        
        log_info("Shutting down Exchange System...");
        
        if (order_server_) {
            order_server_->stop();
            order_server_.reset();
        }
        
        if (market_data_publisher_) {
            market_data_publisher_->stop(); 
            market_data_publisher_.reset();
        }
        
        if (matching_engine_) {
            matching_engine_->stop();
            matching_engine_.reset();
        }
        
        std::this_thread::sleep_for(10s);
        log_info("Exchange System shutdown complete");
    }
};

// Global instance for signal handling (necessary evil for C signal API)
std::unique_ptr<ExchangeSystem> g_exchange_system = nullptr;

} // namespace Exchange::Modern

// Modern signal handler
void modern_signal_handler(int signal) {
    using namespace std::chrono_literals;
    
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully...\n";
    
    if (Exchange::Modern::g_exchange_system) {
        Exchange::Modern::g_exchange_system->stop();
    }
    
    std::this_thread::sleep_for(1s);
    std::exit(EXIT_SUCCESS);
}

// Modern main function
int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    using namespace Exchange::Modern;
    
    try {
        // Configure the exchange
        ExchangeConfig config{
            .log_filename = "exchange_main.log",
            .sleep_time = std::chrono::milliseconds{100}
        };
        
        // Create and store global reference for signal handling
        g_exchange_system = std::make_unique<ExchangeSystem>(std::move(config));
        
        // Setup modern signal handling
        std::signal(SIGINT, modern_signal_handler);
        std::signal(SIGTERM, modern_signal_handler);
        
        // Start the exchange
        g_exchange_system->start();
        
        // Run the main loop
        g_exchange_system->run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}