#include "14_orderbook_kucoin.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
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

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace kucoin {

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

std::string OrderBook::get_websocket_token() {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        ctx.set_default_verify_paths();
        
        tcp::resolver resolver{ioc};
        beast::ssl_stream<tcp::socket> stream{ioc, ctx};
        
        auto const results = resolver.resolve("api.kucoin.com", "443");
        net::connect(stream.next_layer(), results);
        
        if (!SSL_set_tlsext_host_name(stream.native_handle(), "api.kucoin.com")) {
            throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                net::error::get_ssl_category()), "Failed to set SNI");
        }
        
        stream.handshake(ssl::stream_base::client);
        
        http::request<http::string_body> req{http::verb::post, "/api/v1/bullet-public", 11};
        req.set(http::field::host, "api.kucoin.com");
        req.set(http::field::user_agent, "KuCoin-OrderBook-C++20");
        req.set(http::field::content_type, "application/json");
        
        http::write(stream, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        beast::error_code ec;
        stream.shutdown(ec);
        
        std::string body = res.body();
        auto data_pos = body.find("\"data\"");
        if (data_pos == std::string::npos) return "";
        
        auto token_pos = body.find("\"token\":\"", data_pos);
        if (token_pos == std::string::npos) return "";
        token_pos += 9;
        
        auto token_end = body.find("\"", token_pos);
        if (token_end == std::string::npos) return "";
        
        std::string token = body.substr(token_pos, token_end - token_pos);
        
        auto endpoint_pos = body.find("\"endpoint\":\"", data_pos);
        if (endpoint_pos == std::string::npos) return "";
        endpoint_pos += 12;
        
        auto endpoint_end = body.find("\"", endpoint_pos);
        if (endpoint_end == std::string::npos) return "";
        
        std::string endpoint = body.substr(endpoint_pos, endpoint_end - endpoint_pos);
        
        // Extract host and path from endpoint (wss://host/path)
        size_t host_start = endpoint.find("://") + 3;
        size_t path_start = endpoint.find("/", host_start);
        std::string host = endpoint.substr(host_start, path_start - host_start);
        std::string path = endpoint.substr(path_start) + "?token=" + token;
        
        return host + "|" + path;  // Return as "host|path"
        
    } catch (std::exception const& e) {
        std::cerr << "KuCoin token error: " << e.what() << "\n";
        return "";
    }
}

void OrderBook::process_message(const std::string& msg) {
    auto parse_levels = [](const std::string& text, size_t start) -> std::vector<Level> {
        std::vector<Level> levels;
        size_t pos = start;
        while (pos < text.size() && text[pos] != ']') {
            if (text[pos] == '[') {
                size_t end = text.find(']', pos);
                if (end == std::string::npos) break;
                std::string pair = text.substr(pos + 1, end - pos - 1);
                
                // Remove quotes
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
                            levels.push_back({price, volume});
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
    
    if (bids_pos == std::string::npos || asks_pos == std::string::npos) return;
    
    auto bids_start = msg.find('[', bids_pos);
    auto asks_start = msg.find('[', asks_pos);
    
    if (bids_start == std::string::npos || asks_start == std::string::npos) return;
    
    auto bids = parse_levels(msg, bids_start + 1);
    auto asks = parse_levels(msg, asks_start + 1);
    
    std::sort(bids.begin(), bids.end(), [](auto& a, auto& b) { return a.price > b.price; });
    std::sort(asks.begin(), asks.end(), [](auto& a, auto& b) { return a.price < b.price; });
    
    // Sanitize crossed levels
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
            std::string token_info = get_websocket_token();
            if (token_info.empty()) {
                std::cerr << "Failed to get KuCoin token, retrying...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            
            size_t sep = token_info.find('|');
            std::string host = token_info.substr(0, sep);
            std::string path = token_info.substr(sep + 1);
            
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            ctx.set_default_verify_paths();
            
            tcp::resolver resolver{ioc};
            websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};
            
            auto const results = resolver.resolve(host, "443");
            auto ep = net::connect(get_lowest_layer(ws), results);
            
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
                throw beast::system_error(beast::error_code(static_cast<int>(::ERR_get_error()),
                    net::error::get_ssl_category()), "Failed to set SNI");
            }
            
            ws.next_layer().handshake(ssl::stream_base::client);
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(http::field::user_agent, "KuCoin-OrderBook-C++20");
            }));
            
            ws.handshake(host, path);
            
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::string subscribe = "{\"id\":" + std::to_string(now_ms) + 
                ",\"type\":\"subscribe\",\"topic\":\"/spotMarket/level2Depth5:BTC-USDT\",\"response\":true}";
            ws.write(net::buffer(subscribe));
            
            std::cout << "✓ KuCoin connected\n";
            
            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                
                std::string decompressed(static_cast<const char*>(buffer.data().data()), buffer.size());
                
                if (decompressed.find("\"type\":\"ping\"") != std::string::npos) {
                    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    std::string pong = "{\"id\":\"" + std::to_string(now_ms) + "\",\"type\":\"pong\"}";
                    ws.write(net::buffer(pong));
                    continue;
                }
                
                if (decompressed.find("\"type\":\"message\"") != std::string::npos &&
                    decompressed.find("\"data\"") != std::string::npos) {
                    process_message(decompressed);
                }
            }
            
        } catch (std::exception const& e) {
            if (running_) {
                std::cerr << "KuCoin error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

} // namespace kucoin

#ifndef NO_MAIN
int main() {
    kucoin::OrderBook orderbook;
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
        std::cout << "                         Exchange: KuCoin                             \n";
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
