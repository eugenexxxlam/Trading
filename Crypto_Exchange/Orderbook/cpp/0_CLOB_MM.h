#pragma once

#include <vector>
#include <string>
#include <utility>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "lfqueue.h"

namespace clob {

using Level = std::pair<double, double>;  // price, volume
using Levels = std::vector<Level>;

// Price level with exchange source
struct PriceLevel {
    double price;
    double volume;
    std::string exchange;
    
    PriceLevel() : price(0.0), volume(0.0), exchange("") {}
    PriceLevel(double p, double v, const std::string& ex) 
        : price(p), volume(v), exchange(ex) {}
};

// Order book update from an exchange
struct OrderBookUpdate {
    Levels bids;
    Levels asks;
    uint64_t timestamp_us;
    
    OrderBookUpdate() : timestamp_us(0) {}
};

// Configuration matching Python CONTROLS
struct CLOBConfig {
    double min_notional = 10000.0;
    int depth = 32;
    int aggregation_hz = 100;
    double bid_markup_bps = 0.8;
    double ask_markup_bps = 0.1;
    double spread_floor_bps = 0.0;
    
    std::unordered_map<std::string, double> weights = {
        {"binance", 1.5},    // Tier 1: Global leader, tight spreads
        {"coinbase", 1.5},   // Tier 1: US institutional, tight spreads
        {"okx", 0.3}         // Tier 4: Wide spreads, reduced influence
    };
};

/**
 * @brief Central Limit Order Book Aggregator
 * 
 * Aggregates order books from multiple exchanges with:
 * - Lock-free queues (one per exchange)
 * - Confidence-based weighting (time decay)
 * - Price adjustment (spread tightening)
 * - Liquidity filtering
 */
class CLOBAggregator {
public:
    explicit CLOBAggregator(const CLOBConfig& config = CLOBConfig());
    ~CLOBAggregator();
    
    void start();
    void stop();
    
    // Exchange updates (called by exchange threads)
    void update_exchange(const std::string& exchange, 
                        const Levels& bids, 
                        const Levels& asks);
    
    // Get aggregated orderbook (thread-safe read)
    std::pair<std::vector<PriceLevel>, std::vector<PriceLevel>> get_orderbook() const;
    
    // Hot reload configuration (thread-safe)
    void update_config(const CLOBConfig& new_config);
    
    // For simple display compatibility
    const std::vector<PriceLevel>& bids() const { return merged_bids_; }
    const std::vector<PriceLevel>& asks() const { return merged_asks_; }

private:
    void aggregation_loop();
    void rebuild_clob(const std::unordered_map<std::string, OrderBookUpdate>& updates);
    
    static double calculate_confidence(double age_seconds);
    static uint64_t get_time_us();
    
    CLOBConfig config_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> aggregator_thread_;
    
    // Lock-free queues (one per exchange)
    std::unordered_map<std::string, std::unique_ptr<Common::LFQueue<OrderBookUpdate>>> queues_;
    
    // Aggregated output (read by display thread)
    std::vector<PriceLevel> merged_bids_;
    std::vector<PriceLevel> merged_asks_;
    
    // Latest updates from each exchange
    std::unordered_map<std::string, OrderBookUpdate> latest_updates_;
};

} // namespace clob
