#include "11_orderbook_coinbase.h"
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

namespace coinbase {

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
    result.reserve(DEPTH);
    
    // Get best bid for sanitization
    double best_bid = bids_map_.empty() ? 0.0 : bids_map_.rbegin()->first;
    
    int count = 0;
    for (auto it = asks_map_.begin(); it != asks_map_.end() && count < DEPTH; ++it) {
        if (it->first > best_bid) {  // Sanitize: ask must be > best bid
            result.push_back({it->first, it->second});
            ++count;
        }
    }
    return result;
}

void OrderBook::process_message(const std::string& msg) {
    auto find_type = msg.find("\"type\"");
    if (find_type == std::string::npos) return;
    
    if (msg.find("\"snapshot\"", find_type) != std::string::npos) {
        auto parse_side = [](const std::string& text, size_t start) -> std::map<double, double> {
            std::map<double, double> levels;
            size_t pos = start;
            while (pos < text.size()) {
                if (text[pos] == '[' && pos + 1 < text.size() && text[pos + 1] == '[') {
                    pos += 2;
                    break;
                }
                ++pos;
            }
            
            while (pos < text.size()) {
                if (text[pos] == ']') break;
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
                            if (volume > 0.0) {
                                levels[price] = volume;
                            }
                        } catch (...) {}
                    }
                    pos = end + 1;
                } else {
                    ++pos;
                }
            }
            return levels;
        };
        
        auto bids_pos = msg.find("\"bids\"");
        auto asks_pos = msg.find("\"asks\"");
        
        if (bids_pos != std::string::npos && asks_pos != std::string::npos) {
            auto bids = parse_side(msg, bids_pos);
            auto asks = parse_side(msg, asks_pos);
            
            std::lock_guard lock(mutex_);
            bids_map_ = std::move(bids);
            asks_map_ = std::move(asks);
        }
    }
    else if (msg.find("\"l2update\"", find_type) != std::string::npos) {
        auto changes_pos = msg.find("\"changes\"");
        if (changes_pos == std::string::npos) return;
        
        size_t pos = changes_pos;
        while (pos < msg.size() && msg[pos] != '[') ++pos;
        if (pos >= msg.size()) return;
        ++pos;
        
        std::lock_guard lock(mutex_);
        
        while (pos < msg.size() && msg[pos] != ']') {
            if (msg[pos] == '[') {
                size_t end = msg.find(']', pos);
                if (end == std::string::npos) break;
                std::string change = msg.substr(pos + 1, end - pos - 1);
                
                std::string cleaned;
                for (char c : change) {
                    if (c != '"') cleaned += c;
                }
                
                size_t comma1 = cleaned.find(',');
                size_t comma2 = cleaned.find(',', comma1 + 1);
                
                if (comma1 != std::string::npos && comma2 != std::string::npos) {
                    try {
                        std::string side = cleaned.substr(0, comma1);
                        double price = std::stod(cleaned.substr(comma1 + 1, comma2 - comma1 - 1));
                        double size = std::stod(cleaned.substr(comma2 + 1));
                        
                        if (side == "buy") {
                            if (size == 0.0) {
                                bids_map_.erase(price);
                            } else {
                                bids_map_[price] = size;
                            }
                        } else if (side == "sell") {
                            if (size == 0.0) {
                                asks_map_.erase(price);
                            } else {
                                asks_map_[price] = size;
                            }
                        }
                    } catch (...) {}
                }
                pos = end + 1;
            } else {
                ++pos;
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
            
            auto const results = resolver.resolve("ws-feed.exchange.coinbase.com", "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "ws-feed.exchange.coinbase.com")) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "Coinbase-OrderBook-C++20");
            }));
            
            ws.handshake("ws-feed.exchange.coinbase.com", "/");
            
            std::string subscribe = R"({"type":"subscribe","product_ids":["BTC-USD"],"channels":["level2_batch"]})";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ Coinbase connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                std::string msg(static_cast<const char*>(buffer.data().data()), buffer.size());
                process_message(msg);
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "Coinbase error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace coinbase

#ifndef NO_MAIN
int main() {
    coinbase::OrderBook orderbook;
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
        std::cout << "                         Exchange: Coinbase                           \n";
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
