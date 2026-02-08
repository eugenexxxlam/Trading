#include "10_orderbook_dydx.h"
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
#include <limits>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace dydx {

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
    
    int count = 0;
    for (auto it = bids_map_.rbegin(); it != bids_map_.rend() && count < DEPTH; ++it) {
        if (it->first < best_ask) {  // Sanitize: bid must be < best ask
            result.push_back({it->first, it->second});
            ++count;
        }
    }
    return result;
}

auto OrderBook::get_asks() const -> std::vector<Level> {
    std::lock_guard lock(mutex_);
    std::vector<Level> result;
    
    // Get best bid for sanitization
    double best_bid = bids_map_.empty() ? 0.0 : bids_map_.rbegin()->first;
    
    int count = 0;
    for (const auto& [price, volume] : asks_map_) {
        if (count >= DEPTH) break;
        if (price > best_bid) {  // Sanitize: ask must be > best bid
            result.push_back({price, volume});
            ++count;
        }
    }
    return result;
}

void OrderBook::process_message(const std::string& msg) {
    auto parse_levels = [](const std::string& text, size_t start) -> std::vector<std::pair<double, double>> {
        std::vector<std::pair<double, double>> levels;
        size_t pos = start;
        while (pos < text.size() && text[pos] != ']') {
            if (text[pos] == '[') {
                size_t end = text.find(']', pos);
                if (end == std::string::npos) break;
                std::string pair = text.substr(pos + 1, end - pos - 1);
                
                std::string cleaned;
                for (char c : pair) {
                    if (c != '"') cleaned += c;
                }
                
                size_t comma = cleaned.find(',');
                if (comma != std::string::npos) {
                    try {
                        double price = std::stod(cleaned.substr(0, comma));
                        double volume = std::stod(cleaned.substr(comma + 1));
                        levels.push_back({price, volume});
                    } catch (...) {}
                }
                pos = end + 1;
            } else {
                ++pos;
            }
        }
        return levels;
    };
    
    if (msg.find("\"type\":\"subscribed\"") != std::string::npos) return;
    if (msg.find("\"type\":\"channel_data\"") == std::string::npos) return;
    if (msg.find("\"contents\"") == std::string::npos) return;
    
    auto bids_pos = msg.find("\"bids\"");
    auto asks_pos = msg.find("\"asks\"");
    
    std::lock_guard lock(mutex_);
    
    if (bids_pos != std::string::npos) {
        auto bids_start = msg.find('[', bids_pos);
        if (bids_start != std::string::npos) {
            auto levels = parse_levels(msg, bids_start + 1);
            for (const auto& [price, volume] : levels) {
                if (volume == 0.0) {
                    bids_map_.erase(price);
                } else {
                    bids_map_[price] = volume;
                }
            }
        }
    }
    
    if (asks_pos != std::string::npos) {
        auto asks_start = msg.find('[', asks_pos);
        if (asks_start != std::string::npos) {
            auto levels = parse_levels(msg, asks_start + 1);
            for (const auto& [price, volume] : levels) {
                if (volume == 0.0) {
                    asks_map_.erase(price);
                } else {
                    asks_map_[price] = volume;
                }
            }
        }
    }
    
    if (!bids_map_.empty() && !asks_map_.empty()) {
        double best_bid = bids_map_.rbegin()->first;
        double best_ask = asks_map_.begin()->first;
        
        for (auto it = bids_map_.begin(); it != bids_map_.end(); ) {
            if (it->first >= best_ask) {
                it = bids_map_.erase(it);
            } else {
                ++it;
            }
        }
        
        for (auto it = asks_map_.begin(); it != asks_map_.end(); ) {
            if (!bids_map_.empty() && it->first <= bids_map_.rbegin()->first) {
                it = asks_map_.erase(it);
            } else {
                ++it;
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
            
            auto const results = resolver.resolve("indexer.dydx.trade", "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "indexer.dydx.trade")) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "dYdX-OrderBook-C++20");
            }));
            
            ws.handshake("indexer.dydx.trade", "/v4/ws");
            
            std::string subscribe = R"({"type":"subscribe","channel":"v4_orderbook","id":"BTC-USD"})";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ dYdX connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                std::string msg(static_cast<const char*>(buffer.data().data()), buffer.size());
                process_message(msg);
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "dYdX error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace dydx

#ifndef NO_MAIN
int main() {
    dydx::OrderBook orderbook;
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
        std::cout << "                       Exchange: dYdX DEX                             \n";
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
