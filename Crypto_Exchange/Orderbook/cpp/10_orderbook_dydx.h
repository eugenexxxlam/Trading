#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <map>
#include <mutex>

namespace dydx {

class OrderBook {
public:
    struct Level { double price, volume; };
    
    OrderBook();
    ~OrderBook();
    
    void start();
    void stop();
    
    auto get_bids() const -> std::vector<Level>;
    auto get_asks() const -> std::vector<Level>;
    
private:
    void run_loop();
    void process_message(const std::string& msg);
    
    // dYdX sends INCREMENTAL UPDATES (snapshot + channel_data updates)
    // Map is required: O(log n) insert/update/delete, maintains sorted order automatically
    // Cannot use vector: would require O(n) search + O(n log n) sort on every update
    std::map<double, double> bids_map_;
    std::map<double, double> asks_map_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    mutable std::mutex mutex_;
    
    static constexpr int DEPTH = 16;
};

} // namespace dydx
