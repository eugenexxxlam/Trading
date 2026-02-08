#include "0_CLOB_MM.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace clob {

// Get current time in microseconds
uint64_t CLOBAggregator::get_time_us() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// Calculate confidence score (direct port from Python)
double CLOBAggregator::calculate_confidence(double age_seconds) {
    if (age_seconds < 3.0) {
        return 1.0;  // Full confidence
    } else if (age_seconds < 8.0) {
        return 1.0 - (age_seconds - 3.0) * 0.12;  // Decay 100% → 40%
    } else if (age_seconds < 15.0) {
        return 0.4 - (age_seconds - 8.0) * 0.0429;  // Decay 40% → 10%
    } else {
        return 0.05;  // Stale data
    }
}

CLOBAggregator::CLOBAggregator(const CLOBConfig& config) 
    : config_(config) {
    // Initialize lock-free queues for each exchange
    for (const auto& [exchange, weight] : config_.weights) {
        queues_[exchange] = std::make_unique<Common::LFQueue<OrderBookUpdate>>(100);
    }
    
    // Pre-allocate output vectors
    merged_bids_.reserve(config_.depth);
    merged_asks_.reserve(config_.depth);
}

CLOBAggregator::~CLOBAggregator() {
    stop();
}

void CLOBAggregator::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    aggregator_thread_ = std::make_unique<std::thread>(&CLOBAggregator::aggregation_loop, this);
    
    std::cout << "✓ CLOB Aggregator started (" << config_.aggregation_hz << " Hz)\n";
}

void CLOBAggregator::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    if (aggregator_thread_ && aggregator_thread_->joinable()) {
        aggregator_thread_->join();
    }
}

void CLOBAggregator::update_exchange(const std::string& exchange, 
                                     const Levels& bids, 
                                     const Levels& asks) {
    auto it = queues_.find(exchange);
    if (it == queues_.end()) {
        return;  // Unknown exchange
    }
    
    // Get write slot in lock-free queue
    auto* slot = it->second->getNextToWriteTo();
    if (!slot) {
        return;  // Queue full (should never happen with proper sizing)
    }
    
    // Write update
    slot->bids = bids;
    slot->asks = asks;
    slot->timestamp_us = get_time_us();
    
    // Publish to consumer
    it->second->updateWriteIndex();
}

void CLOBAggregator::aggregation_loop() {
    const auto interval_us = std::chrono::microseconds(1'000'000 / config_.aggregation_hz);
    constexpr double stale_threshold = 20.0;  // Drop data older than 20s
    
    while (running_.load()) {
        auto start = std::chrono::high_resolution_clock::now();
        auto now_us = get_time_us();
        
        // Step 1: Drain all queues
        for (auto& [exchange, queue] : queues_) {
            const auto* update = queue->getNextToRead();
            if (update) {
                latest_updates_[exchange] = *update;
                queue->updateReadIndex();
            }
        }
        
        // Step 2: Filter stale data
        std::unordered_map<std::string, OrderBookUpdate> active_updates;
        for (const auto& [exchange, update] : latest_updates_) {
            double age_seconds = (now_us - update.timestamp_us) / 1'000'000.0;
            if (age_seconds < stale_threshold) {
                active_updates[exchange] = update;
            }
        }
        
        // Step 3: Rebuild aggregated CLOB
        rebuild_clob(active_updates);
        
        // Step 4: Sleep to maintain frequency
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        auto sleep_time = interval_us - std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

void CLOBAggregator::rebuild_clob(const std::unordered_map<std::string, OrderBookUpdate>& updates) {
    std::vector<PriceLevel> all_bids;
    std::vector<PriceLevel> all_asks;
    
    // Pre-allocate for efficiency
    all_bids.reserve(updates.size() * config_.depth);
    all_asks.reserve(updates.size() * config_.depth);
    
    // Calculate price multipliers from basis points
    const double bid_mult = 1.0 + config_.bid_markup_bps / 10000.0;
    const double ask_mult = 1.0 + config_.ask_markup_bps / 10000.0;
    const double min_notional = config_.min_notional;
    
    auto now_us = get_time_us();
    
    // Step 1: Collect all levels with confidence weighting
    for (const auto& [exchange, update] : updates) {
        // Calculate age and confidence
        double age_seconds = (now_us - update.timestamp_us) / 1'000'000.0;
        double confidence = calculate_confidence(age_seconds);
        
        // Get base weight for this exchange
        double base_weight = config_.weights.count(exchange) ? 
                            config_.weights.at(exchange) : 1.0;
        
        // Effective weight = base weight * confidence
        double effective_weight = base_weight * confidence;
        
        // Process BIDS
        for (const auto& [price, volume] : update.bids) {
            double p_adj = price * bid_mult;
            double v_weighted = volume * effective_weight;
            
            // Filter by minimum notional
            if (p_adj * v_weighted >= min_notional) {
                all_bids.emplace_back(p_adj, v_weighted, exchange);
            }
        }
        
        // Process ASKS
        for (const auto& [price, volume] : update.asks) {
            double p_adj = price * ask_mult;
            double v_weighted = volume * effective_weight;
            
            if (p_adj * v_weighted >= min_notional) {
                all_asks.emplace_back(p_adj, v_weighted, exchange);
            }
        }
    }
    
    // Step 2: Sort by price
    std::sort(all_bids.begin(), all_bids.end(), 
        [](const PriceLevel& a, const PriceLevel& b) {
            return a.price > b.price;  // Descending (highest bid first)
        });
    
    std::sort(all_asks.begin(), all_asks.end(),
        [](const PriceLevel& a, const PriceLevel& b) {
            return a.price < b.price;  // Ascending (lowest ask first)
        });
    
    // Step 3: Enforce spread floor and remove crossed levels
    if (!all_bids.empty() && !all_asks.empty()) {
        double best_bid = all_bids[0].price;
        double best_ask = all_asks[0].price;
        double spread_bps = (best_ask / best_bid - 1.0) * 10000.0;
        
        if (spread_bps < config_.spread_floor_bps) {
            // Widen spread artificially
            double mid = (best_bid + best_ask) / 2.0;
            double half_spread = mid * config_.spread_floor_bps / 10000.0 / 2.0;
            
            // Filter levels
            all_bids.erase(
                std::remove_if(all_bids.begin(), all_bids.end(),
                    [mid, half_spread](const PriceLevel& l) {
                        return l.price > mid - half_spread;
                    }),
                all_bids.end());
            
            all_asks.erase(
                std::remove_if(all_asks.begin(), all_asks.end(),
                    [mid, half_spread](const PriceLevel& l) {
                        return l.price < mid + half_spread;
                    }),
                all_asks.end());
        } else {
            // Remove crossed levels
            all_bids.erase(
                std::remove_if(all_bids.begin(), all_bids.end(),
                    [best_ask](const PriceLevel& l) {
                        return l.price >= best_ask;
                    }),
                all_bids.end());
            
            all_asks.erase(
                std::remove_if(all_asks.begin(), all_asks.end(),
                    [best_bid](const PriceLevel& l) {
                        return l.price <= best_bid;
                    }),
                all_asks.end());
        }
    }
    
    // Step 4: Take top N levels
    merged_bids_.clear();
    merged_asks_.clear();
    
    size_t bid_count = std::min(static_cast<size_t>(config_.depth), all_bids.size());
    size_t ask_count = std::min(static_cast<size_t>(config_.depth), all_asks.size());
    
    merged_bids_.assign(all_bids.begin(), all_bids.begin() + bid_count);
    merged_asks_.assign(all_asks.begin(), all_asks.begin() + ask_count);
    
    // Pad with empty levels for consistent output
    while (merged_bids_.size() < static_cast<size_t>(config_.depth)) {
        merged_bids_.emplace_back(0.0, 0.0, "");
    }
    while (merged_asks_.size() < static_cast<size_t>(config_.depth)) {
        merged_asks_.emplace_back(0.0, 0.0, "");
    }
}

std::pair<std::vector<PriceLevel>, std::vector<PriceLevel>> 
CLOBAggregator::get_orderbook() const {
    return {merged_bids_, merged_asks_};
}

void CLOBAggregator::update_config(const CLOBConfig& new_config) {
    config_ = new_config;
}

} // namespace clob
