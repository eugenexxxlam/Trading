#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

namespace cryptocom {

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
    
    // Crypto.com sends FULL SNAPSHOTS every update
    // Vector is optimal: simple bulk replacement, no incremental logic needed
    std::vector<Level> bids_;
    std::vector<Level> asks_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    mutable std::mutex mutex_;
    
    static constexpr int DEPTH = 16;
};

} // namespace cryptocom
