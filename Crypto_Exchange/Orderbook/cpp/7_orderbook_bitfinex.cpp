#include "7_orderbook_bitfinex.h"
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <iostream>
#include <iomanip>
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

namespace bitfinex {

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
    result.reserve(DEPTH);
    
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
    result.reserve(DEPTH);
    
    // Get best bid for sanitization
    double best_bid = bids_map_.empty() ? 0.0 : bids_map_.rbegin()->first;
    
    for (const auto& [price, volume] : asks_map_) {
        if (result.size() >= DEPTH) break;
        if (price > best_bid) {  // Sanitize: ask must be > best bid
            result.push_back({price, volume});
        }
    }
    return result;
}

void OrderBook::process_message(const std::string& msg) {
    // Skip event messages
    if (msg.find("\"event\"") != std::string::npos) return;
    
    // Check if it's an array message [channel_id, data]
    if (msg[0] != '[') return;
    
    // Find first data array after channel_id
    size_t first_bracket = msg.find('[', 1);
    if (first_bracket == std::string::npos) return;
    
    // Check if it's a snapshot (nested arrays) or update (single array)
    bool is_snapshot = false;
    size_t check_pos = first_bracket + 1;
    while (check_pos < msg.size() && std::isspace(msg[check_pos])) ++check_pos;
    if (check_pos < msg.size() && msg[check_pos] == '[') {
        is_snapshot = true;
    }
    
    std::lock_guard lock(mutex_);
    
    if (is_snapshot) {
        // Snapshot: [[price, count, amount], ...]
        bids_map_.clear();
        asks_map_.clear();
        
        size_t pos = first_bracket + 1;
        while (pos < msg.size() && msg[pos] != ']') {
            if (msg[pos] == '[') {
                size_t end = msg.find(']', pos);
                if (end == std::string::npos) break;
                
                std::string entry = msg.substr(pos + 1, end - pos - 1);
                size_t comma1 = entry.find(',');
                size_t comma2 = entry.find(',', comma1 + 1);
                
                if (comma1 != std::string::npos && comma2 != std::string::npos) {
                    try {
                        double price = std::stod(entry.substr(0, comma1));
                        double amount = std::stod(entry.substr(comma2 + 1));
                        
                        if (amount > 0) {
                            bids_map_[price] = amount;
                        } else {
                            asks_map_[price] = std::abs(amount);
                        }
                    } catch (...) {}
                }
                pos = end + 1;
            } else {
                ++pos;
            }
        }
    } else {
        // Update: [price, count, amount]
        size_t end = msg.find(']', first_bracket);
        if (end == std::string::npos) return;
        
        std::string entry = msg.substr(first_bracket + 1, end - first_bracket - 1);
        size_t comma1 = entry.find(',');
        size_t comma2 = entry.find(',', comma1 + 1);
        
        if (comma1 != std::string::npos && comma2 != std::string::npos) {
            try {
                double price = std::stod(entry.substr(0, comma1));
                double count = std::stod(entry.substr(comma1 + 1, comma2 - comma1 - 1));
                double amount = std::stod(entry.substr(comma2 + 1));
                
                if (count > 0) {
                    // Update level
                    if (amount > 0) {
                        bids_map_[price] = amount;
                    } else {
                        asks_map_[price] = std::abs(amount);
                    }
                } else {
                    // Delete level (count == 0)
                    if (amount > 0) {
                        bids_map_.erase(price);
                    } else {
                        asks_map_.erase(price);
                    }
                }
            } catch (...) {}
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
            
            auto const results = resolver.resolve("api-pub.bitfinex.com", "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "api-pub.bitfinex.com")) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "Bitfinex-OrderBook-C++20");
            }));
            
            ws.handshake("api-pub.bitfinex.com", "/ws/2");
            
            std::string subscribe = R"({"event":"subscribe","channel":"book","symbol":"tBTCUSD","prec":"P0","freq":"F0","len":"25"})";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ Bitfinex connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                
                std::string msg(static_cast<const char*>(buffer.data().data()), buffer.size());
                process_message(msg);
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "Bitfinex error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace bitfinex

#ifndef NO_MAIN

int main() {
    bitfinex::OrderBook orderbook;
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
        std::cout << "                         Exchange: Bitfinex                           \n";
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
