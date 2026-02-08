#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <simdjson.h>

class OKXOrderBook {
public:
    using Price = double;
    using Volume = double;
    using Level = std::pair<Price, Volume>;
    
    explicit OKXOrderBook(const std::string& symbol = "BTC-USDT", int depth = 20);
    ~OKXOrderBook();
    
    void start();
    void stop();
    
    [[nodiscard]] const std::vector<Level>& bids() const { return m_bids; }
    [[nodiscard]] const std::vector<Level>& asks() const { return m_asks; }
    [[nodiscard]] bool has_data() const { return m_has_data.load(); }
    
    bool process_message(const std::string& msg);
    
private:
    void run_loop();
    
    std::string m_symbol;
    int m_depth;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_has_data{false};
    std::thread m_thread;
    
    std::vector<Level> m_bids;
    std::vector<Level> m_asks;
    
    simdjson::ondemand::parser m_parser;
    std::vector<char> m_buffer;
};
