#include "12_orderbook_kraken.h"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <mutex>
#include <limits>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace kraken {

OrderBook::OrderBook() = default;
OrderBook::~OrderBook() { stop(); }

void OrderBook::start() {
    running_ = true;
    thread_ = std::thread([this] { run_loop(); });
}

void OrderBook::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

auto OrderBook::get_bids() const -> std::vector<Level> {
    std::lock_guard lock(mutex_);
    std::vector<Level> result;
    
    // Get best ask for sanitization
    double best_ask = asks_map_.empty() ? std::numeric_limits<double>::max() : asks_map_.begin()->first;
    
    for (auto it = bids_map_.rbegin(); it != bids_map_.rend() && result.size() < DEPTH; ++it) {
        if (it->first < best_ask) {  // Sanitize: bid must be < best ask
            result.push_back({it->first, it->second});
        }
    }
    return result;
}

auto OrderBook::get_asks() const -> std::vector<Level> {
    std::lock_guard lock(mutex_);
    std::vector<Level> result;
    
    // Get best bid for sanitization
    double best_bid = bids_map_.empty() ? 0.0 : bids_map_.rbegin()->first;
    
    for (auto it = asks_map_.begin(); it != asks_map_.end() && result.size() < DEPTH; ++it) {
        if (it->first > best_bid) {  // Sanitize: ask must be > best bid
            result.push_back({it->first, it->second});
        }
    }
    return result;
}

void OrderBook::process_message(const std::string& msg) {
    // Kraken sends arrays: [channelID, {data}, "book-10", "XBT/USD"]
    // Snapshot: {"as": [["price","vol","timestamp"],...], "bs": [...]}
    // Update: {"a": [["price","vol","timestamp"],...], "b": [...]}
    
    // Check if it's a snapshot (contains "as" and "bs")
    auto as_pos = msg.find("\"as\"");
    auto bs_pos = msg.find("\"bs\"");
    bool is_snapshot = (as_pos != std::string::npos && bs_pos != std::string::npos);
    
    // Check if it's an update (contains "a" or "b" but not "as"/"bs")
    auto a_pos = msg.find("\"a\"");
    auto b_pos = msg.find("\"b\"");
    bool is_update = ((a_pos != std::string::npos && msg.substr(a_pos, 4) == "\"a\":") ||
                      (b_pos != std::string::npos && msg.substr(b_pos, 4) == "\"b\":"));
    
    if (!is_snapshot && !is_update) return;
    
    auto parse_levels = [](const std::string& text, size_t start) -> std::map<double, double> {
        std::map<double, double> levels;
        size_t pos = start;
        
        while (pos < text.size()) {
            // Find opening bracket of array: ["price","vol","timestamp"]
            size_t open_bracket = text.find('[', pos);
            if (open_bracket == std::string::npos || open_bracket > text.find(']', pos)) break;
            
            // Find the matching closing bracket
            size_t close_bracket = text.find(']', open_bracket);
            if (close_bracket == std::string::npos) break;
            
            // Extract the array content
            std::string array_content = text.substr(open_bracket + 1, close_bracket - open_bracket - 1);
            
            // Parse price and volume (both are quoted strings)
            size_t first_quote = array_content.find('"');
            if (first_quote == std::string::npos) {
                pos = close_bracket + 1;
                continue;
            }
            size_t second_quote = array_content.find('"', first_quote + 1);
            if (second_quote == std::string::npos) {
                pos = close_bracket + 1;
                continue;
            }
            
            std::string price_str = array_content.substr(first_quote + 1, second_quote - first_quote - 1);
            
            size_t third_quote = array_content.find('"', second_quote + 1);
            if (third_quote == std::string::npos) {
                pos = close_bracket + 1;
                continue;
            }
            size_t fourth_quote = array_content.find('"', third_quote + 1);
            if (fourth_quote == std::string::npos) {
                pos = close_bracket + 1;
                continue;
            }
            
            std::string vol_str = array_content.substr(third_quote + 1, fourth_quote - third_quote - 1);
            
            try {
                double price = std::stod(price_str);
                double volume = std::stod(vol_str);
                levels[price] = volume;
            } catch (...) {}
            
            pos = close_bracket + 1;
            
            // Check if we're at the end of the outer array
            size_t next_bracket = text.find_first_of("[],", pos);
            if (next_bracket == std::string::npos || text[next_bracket] == ']') break;
        }
        
        return levels;
    };
    
    std::lock_guard lock(mutex_);
    
    if (is_snapshot) {
        // Clear existing orderbook and load snapshot
        bids_map_.clear();
        asks_map_.clear();
        
        if (bs_pos != std::string::npos) {
            size_t bs_start = msg.find('[', bs_pos);
            if (bs_start != std::string::npos) {
                bids_map_ = parse_levels(msg, bs_start);
            }
        }
        
        if (as_pos != std::string::npos) {
            size_t as_start = msg.find('[', as_pos);
            if (as_start != std::string::npos) {
                asks_map_ = parse_levels(msg, as_start);
            }
        }
        
        snapshot_received_ = true;
        
    } else if (is_update && snapshot_received_) {
        // Apply incremental updates
        if (b_pos != std::string::npos && msg.substr(b_pos, 4) == "\"b\":") {
            size_t b_start = msg.find('[', b_pos);
            if (b_start != std::string::npos) {
                auto updates = parse_levels(msg, b_start);
                for (const auto& [price, volume] : updates) {
                    if (volume == 0.0) {
                        bids_map_.erase(price);
                    } else {
                        bids_map_[price] = volume;
                    }
                }
            }
        }
        
        if (a_pos != std::string::npos && msg.substr(a_pos, 4) == "\"a\":") {
            size_t a_start = msg.find('[', a_pos);
            if (a_start != std::string::npos) {
                auto updates = parse_levels(msg, a_start);
                for (const auto& [price, volume] : updates) {
                    if (volume == 0.0) {
                        asks_map_.erase(price);
                    } else {
                        asks_map_[price] = volume;
                    }
                }
            }
        }
    }
    
    // Sanitize crossed levels (critical for incremental updates)
    if (!bids_map_.empty() && !asks_map_.empty()) {
        double best_ask = asks_map_.begin()->first;
        double best_bid = bids_map_.rbegin()->first;
        
        // Remove bids >= best ask
        auto bid_it = bids_map_.lower_bound(best_ask);
        if (bid_it != bids_map_.end()) {
            bids_map_.erase(bid_it, bids_map_.end());
        }
        
        // Remove asks <= best bid (recalculate best_bid after cleaning)
        if (!bids_map_.empty()) {
            best_bid = bids_map_.rbegin()->first;
            auto ask_it = asks_map_.begin();
            while (ask_it != asks_map_.end() && ask_it->first <= best_bid) {
                ask_it = asks_map_.erase(ask_it);
            }
        }
    }
}

void OrderBook::run_loop() {
    while (running_) {
        try {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            ctx.set_default_verify_paths();
            
            tcp::resolver resolver{ioc};
            websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};
            
            auto const results = resolver.resolve("ws.kraken.com", "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "ws.kraken.com")) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "Kraken-OrderBook-C++20");
            }));
            
            ws.handshake("ws.kraken.com", "/");
            
            std::string subscribe = R"({"event":"subscribe","pair":["XBT/USD"],"subscription":{"name":"book","depth":10}})";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ Kraken connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                
                std::string msg(static_cast<const char*>(buffer.data().data()), buffer.size());
                
                // Skip non-data messages (subscriptionStatus, heartbeat, etc.)
                if (msg.find("\"event\"") != std::string::npos) continue;
                
                process_message(msg);
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "Kraken error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace kraken

#ifndef NO_MAIN
int main() {
    kraken::OrderBook orderbook;
    orderbook.start();
    
    constexpr int DEPTH = 16;
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto bids = orderbook.get_bids();
        auto asks = orderbook.get_asks();
        
        if (bids.empty() || asks.empty()) continue;
        
        bids.resize(std::min(bids.size(), size_t(DEPTH)));
        asks.resize(std::min(asks.size(), size_t(DEPTH)));
        
        while (bids.size() < DEPTH) bids.push_back({0.0, 0.0});
        while (asks.size() < DEPTH) asks.push_back({0.0, 0.0});
        
        double spread = asks[0].price - bids[0].price;
        double mid = (bids[0].price + asks[0].price) / 2.0;
        
        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        auto gmt = std::gmtime(&now_t);
        auto gmt8_t = now_t + 8 * 3600;
        auto gmt8 = std::gmtime(&gmt8_t);
        
        std::cout << "\033[2J\033[H\n";
        std::cout << "======================================================================\n";
        std::cout << "                         Exchange: Kraken                             \n";
        std::cout << "                     GMT: " << std::put_time(gmt, "%Y-%m-%d %H:%M:%S") << "                     \n";
        std::cout << "                   GMT+8: " << std::put_time(gmt8, "%Y-%m-%d %H:%M:%S") << "                   \n";
        std::cout << "              Spread: $" << std::fixed << std::setprecision(2) << spread 
                  << "  |  Mid: $" << std::setprecision(2) << mid << "              \n";
        std::cout << "======================================================================\n\n";
        
        std::cout << "                        ASKS (Sell Orders)                            \n";
        std::cout << "      Price         BTC       USDT    Cum BTC   Cum USDT\n";
        std::cout << "----------------------------------------------------------------------\n";
        
        double cum_btc_ask = 0.0, cum_usdt_ask = 0.0;
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
            if (it->price > 0) {
                double usdt = it->price * it->volume;
                cum_btc_ask += it->volume;
                cum_usdt_ask += usdt;
                std::cout << "\033[91m" << std::setw(11) << std::fixed << std::setprecision(2) << it->price << "\033[0m  "
                          << std::setw(10) << std::setprecision(7) << it->volume << "  "
                          << std::setw(9) << std::setprecision(0) << usdt << "  "
                          << std::setw(10) << std::setprecision(4) << cum_btc_ask << "  "
                          << std::setw(9) << std::setprecision(0) << cum_usdt_ask << "\n";
            } else {
                std::cout << std::setw(11) << "---" << "  "
                          << std::setw(10) << "---" << "  "
                          << std::setw(9) << "---" << "  "
                          << std::setw(10) << std::fixed << std::setprecision(4) << cum_btc_ask << "  "
                          << std::setw(9) << std::setprecision(0) << cum_usdt_ask << "\n";
            }
        }
        
        std::cout << "\n                        BIDS (Buy Orders)                             \n";
        std::cout << "      Price         BTC       USDT    Cum BTC   Cum USDT\n";
        std::cout << "----------------------------------------------------------------------\n";
        
        double cum_btc_bid = 0.0, cum_usdt_bid = 0.0;
        for (const auto& bid : bids) {
            if (bid.price > 0) {
                double usdt = bid.price * bid.volume;
                cum_btc_bid += bid.volume;
                cum_usdt_bid += usdt;
                std::cout << "\033[92m" << std::setw(11) << std::fixed << std::setprecision(2) << bid.price << "\033[0m  "
                          << std::setw(10) << std::setprecision(7) << bid.volume << "  "
                          << std::setw(9) << std::setprecision(0) << usdt << "  "
                          << std::setw(10) << std::setprecision(4) << cum_btc_bid << "  "
                          << std::setw(9) << std::setprecision(0) << cum_usdt_bid << "\n";
            } else {
                std::cout << std::setw(11) << "---" << "  "
                          << std::setw(10) << "---" << "  "
                          << std::setw(9) << "---" << "  "
                          << std::setw(10) << std::fixed << std::setprecision(4) << cum_btc_bid << "  "
                          << std::setw(9) << std::setprecision(0) << cum_usdt_bid << "\n";
            }
        }
        
        std::cout << "======================================================================\n";
    }
    
    return 0;
}

#endif // NO_MAIN
