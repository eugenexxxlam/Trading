#include "9_orderbook_hyperliquid.h"
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

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace hyperliquid {

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
    return bids_;
}

auto OrderBook::get_asks() const -> std::vector<Level> {
    std::lock_guard lock(mutex_);
    return asks_;
}

void OrderBook::process_message(const std::string& msg) {
    // Skip subscription responses
    if (msg.find("\"subscriptionResponse\"") != std::string::npos) return;
    
    // Only process l2Book channel messages
    if (msg.find("\"l2Book\"") == std::string::npos) return;
    if (msg.find("\"data\"") == std::string::npos) return;
    if (msg.find("\"levels\"") == std::string::npos) return;
    
    // Parse levels array - format: "levels":[[{bids}],[{asks}]]
    auto levels_pos = msg.find("\"levels\"");
    if (levels_pos == std::string::npos) return;
    
    auto levels_start = msg.find('[', levels_pos);
    if (levels_start == std::string::npos) return;
    
    // Find the two main arrays: bids and asks
    size_t first_bracket = msg.find('[', levels_start + 1);
    if (first_bracket == std::string::npos) return;
    
    // Parse bids (first array)
    std::vector<Level> bids;
    size_t pos = first_bracket + 1;
    while (pos < msg.size() && msg[pos] != ']') {
        if (msg[pos] == '{') {
            size_t px_pos = msg.find("\"px\"", pos);
            size_t sz_pos = msg.find("\"sz\"", pos);
            size_t end = msg.find('}', pos);
            
            if (px_pos != std::string::npos && sz_pos != std::string::npos && 
                end != std::string::npos && px_pos < end && sz_pos < end) {
                
                size_t px_colon = msg.find(':', px_pos);
                size_t px_end = msg.find_first_of(",}", px_colon);
                size_t sz_colon = msg.find(':', sz_pos);
                size_t sz_end = msg.find_first_of(",}", sz_colon);
                
                if (px_colon != std::string::npos && px_end != std::string::npos &&
                    sz_colon != std::string::npos && sz_end != std::string::npos) {
                    try {
                        std::string px_str = msg.substr(px_colon + 1, px_end - px_colon - 1);
                        std::string sz_str = msg.substr(sz_colon + 1, sz_end - sz_colon - 1);
                        
                        // Remove quotes if present
                        auto clean = [](std::string& s) {
                            s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
                            s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
                        };
                        clean(px_str);
                        clean(sz_str);
                        
                        double price = std::stod(px_str);
                        double volume = std::stod(sz_str);
                        if (volume > 0.0) {
                            bids.push_back({price, volume});
                        }
                    } catch (...) {}
                }
                pos = end + 1;
            } else {
                ++pos;
            }
        } else {
            ++pos;
        }
    }
    
    // Find second array (asks)
    size_t second_bracket = msg.find('[', pos);
    if (second_bracket == std::string::npos) return;
    
    std::vector<Level> asks;
    pos = second_bracket + 1;
    while (pos < msg.size() && msg[pos] != ']') {
        if (msg[pos] == '{') {
            size_t px_pos = msg.find("\"px\"", pos);
            size_t sz_pos = msg.find("\"sz\"", pos);
            size_t end = msg.find('}', pos);
            
            if (px_pos != std::string::npos && sz_pos != std::string::npos && 
                end != std::string::npos && px_pos < end && sz_pos < end) {
                
                size_t px_colon = msg.find(':', px_pos);
                size_t px_end = msg.find_first_of(",}", px_colon);
                size_t sz_colon = msg.find(':', sz_pos);
                size_t sz_end = msg.find_first_of(",}", sz_colon);
                
                if (px_colon != std::string::npos && px_end != std::string::npos &&
                    sz_colon != std::string::npos && sz_end != std::string::npos) {
                    try {
                        std::string px_str = msg.substr(px_colon + 1, px_end - px_colon - 1);
                        std::string sz_str = msg.substr(sz_colon + 1, sz_end - sz_colon - 1);
                        
                        auto clean = [](std::string& s) {
                            s.erase(std::remove(s.begin(), s.end(), '"'), s.end());
                            s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
                        };
                        clean(px_str);
                        clean(sz_str);
                        
                        double price = std::stod(px_str);
                        double volume = std::stod(sz_str);
                        if (volume > 0.0) {
                            asks.push_back({price, volume});
                        }
                    } catch (...) {}
                }
                pos = end + 1;
            } else {
                ++pos;
            }
        } else {
            ++pos;
        }
    }
    
    // Sort and sanitize
    std::sort(bids.begin(), bids.end(), [](auto& a, auto& b) { return a.price > b.price; });
    std::sort(asks.begin(), asks.end(), [](auto& a, auto& b) { return a.price < b.price; });
    
    if (!bids.empty() && !asks.empty()) {
        bids.erase(std::remove_if(bids.begin(), bids.end(), 
            [&](auto& b) { return b.price >= asks[0].price; }), bids.end());
        asks.erase(std::remove_if(asks.begin(), asks.end(), 
            [&](auto& a) { return a.price <= (bids.empty() ? 0.0 : bids[0].price); }), asks.end());
    }
    
    if (!bids.empty() && !asks.empty()) {
        std::lock_guard lock(mutex_);
        bids_ = std::move(bids);
        asks_ = std::move(asks);
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
            
            auto const results = resolver.resolve("api.hyperliquid.xyz", "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "api.hyperliquid.xyz")) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "Hyperliquid-OrderBook-C++20");
            }));
            
            ws.handshake("api.hyperliquid.xyz", "/ws");
            
            std::string subscribe = R"({"method":"subscribe","subscription":{"type":"l2Book","coin":"BTC"}})";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ Hyperliquid connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                
                std::string message(static_cast<const char*>(buffer.data().data()), buffer.size());
                process_message(message);
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "Hyperliquid error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace hyperliquid

#ifndef NO_MAIN
int main() {
    hyperliquid::OrderBook orderbook;
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
        std::cout << "                     Exchange: Hyperliquid DEX                        \n";
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
