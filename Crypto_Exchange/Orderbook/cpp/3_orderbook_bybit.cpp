#include "3_orderbook_bybit.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <csignal>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

BybitOrderBook::BybitOrderBook(const std::string& symbol, int depth)
    : m_symbol(symbol), m_depth(depth) {
    m_bids.reserve(depth);
    m_asks.reserve(depth);
}

BybitOrderBook::~BybitOrderBook() {
    stop();
}

void BybitOrderBook::start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&BybitOrderBook::run_loop, this);
}

void BybitOrderBook::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

bool BybitOrderBook::process_message(const std::string& msg) {
    // Simple string-based JSON parsing with full state maintenance
    try {
        // Quick checks
        if (msg.find("\"data\"") == std::string::npos || 
            msg.find("\"b\"") == std::string::npos || 
            msg.find("\"a\"") == std::string::npos) return false;
        
        bool is_snapshot = msg.find("\"type\":\"snapshot\"") != std::string::npos;
        if (!is_snapshot && msg.find("\"type\":\"delta\"") == std::string::npos) return false;
        
        // Lambda: parse ["price","vol"] pairs from JSON
        auto parse = [](const std::string& json, const char* key) {
            std::vector<Level> result;
            size_t pos = json.find(std::string("\"") + key + "\":[");
            if (pos == std::string::npos) return result;
            pos += 5;  // Skip "x":[
            
            while (pos < json.length() && json[pos] != ']') {
                if (json[pos++] != '[') continue;
                while (json[pos] == ' ' || json[pos] == '"') pos++;
                size_t p_start = pos;
                while (json[pos] != '"') pos++;
                auto price = std::stod(json.substr(p_start, pos++ - p_start));
                while (json[pos] != '"') pos++;
                size_t v_start = ++pos;
                while (json[pos] != '"') pos++;
                auto vol = std::stod(json.substr(v_start, pos - v_start));
                result.emplace_back(price, vol);
                while (json[pos] != ']') pos++;
            }
            return result;
        };
        
        auto new_bids = parse(msg, "b");
        auto new_asks = parse(msg, "a");
        
        // Helper: apply delta updates
        auto apply = [](std::vector<Level>& book, const std::vector<Level>& updates) {
            for (auto [p, v] : updates) {
                auto it = std::find_if(book.begin(), book.end(), 
                    [p](auto& l) { return l.first == p; });
                if (v == 0.0) {
                    if (it != book.end()) book.erase(it);
                } else if (it != book.end()) {
                    it->second = v;
                } else {
                    book.emplace_back(p, v);
                }
            }
        };
        
        if (is_snapshot) {
            m_bids = std::move(new_bids);
            m_asks = std::move(new_asks);
        } else {
            apply(m_bids, new_bids);
            apply(m_asks, new_asks);
        }
        
        if (m_bids.empty() || m_asks.empty()) return false;
        
        // Sort and sanitize
        std::sort(m_bids.begin(), m_bids.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::sort(m_asks.begin(), m_asks.end(), [](auto& a, auto& b) { return a.first < b.first; });
        
        double best_ask = m_asks[0].first, best_bid = m_bids[0].first;
        m_bids.erase(std::remove_if(m_bids.begin(), m_bids.end(), 
            [best_ask](auto& l) { return l.first >= best_ask; }), m_bids.end());
        m_asks.erase(std::remove_if(m_asks.begin(), m_asks.end(), 
            [best_bid](auto& l) { return l.first <= best_bid; }), m_asks.end());
        
        m_has_data = !m_bids.empty() && !m_asks.empty();
        return m_has_data;
        
    } catch (...) {
        return false;
    }
}

void BybitOrderBook::run_loop() {
    while (m_running) {
        try {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tls_client};
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);
            
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve("stream.bybit.com", "443");
            
            beast::ssl_stream<beast::tcp_stream> stream{ioc, ctx};
            
            // Set SNI Hostname
            if (!SSL_set_tlsext_host_name(stream.native_handle(), "stream.bybit.com")) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                throw beast::system_error{ec};
            }
            
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);
            
            websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws{std::move(stream)};
            ws.handshake("stream.bybit.com", "/v5/public/spot");
            
            // Subscribe to order book
            std::ostringstream sub;
            sub << R"({"op":"subscribe","args":["orderbook.)" 
                << m_depth << "." << m_symbol << R"("]})";
            ws.write(net::buffer(sub.str()));
            
            std::cout << "✓ Bybit connected\n";
            
            while (m_running) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                std::string msg = beast::buffers_to_string(buffer.data());
                
                process_message(msg);
            }
            
            ws.close(websocket::close_code::normal);
            
        } catch (const std::exception& e) {
            if (m_running) {
                std::cerr << "Bybit error: " << e.what() << ", reconnecting...\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }
}

#ifndef NO_MAIN

static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

void display_orderbook(const BybitOrderBook& ob, int levels = 16) {
    if (!ob.has_data()) return;
    
    const auto& bids = ob.bids();
    const auto& asks = ob.asks();
    if (bids.empty() || asks.empty()) return;
    
    // Timestamps (GMT and GMT+8)
    auto now = std::chrono::system_clock::now();
    auto time_t_gmt = std::chrono::system_clock::to_time_t(now);
    auto time_t_gmt8 = time_t_gmt + 8 * 3600;  // GMT+8
    auto tm_gmt = *std::gmtime(&time_t_gmt);
    auto tm_gmt8 = *std::gmtime(&time_t_gmt8);
    
    // Calculate spread and mid
    double spread = asks[0].first - bids[0].first;
    double mid = (asks[0].first + bids[0].first) / 2.0;
    
    // Clear screen and header
    std::cout << "\033[2J\033[H\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::setw(35) << std::right << "Exchange: Bybit" << std::setw(35) << std::left << "" << "\n";
    
    char gmt_buf[64], gmt8_buf[64];
    std::strftime(gmt_buf, sizeof(gmt_buf), "GMT: %Y-%m-%d %H:%M:%S", &tm_gmt);
    std::strftime(gmt8_buf, sizeof(gmt8_buf), "GMT+8: %Y-%m-%d %H:%M:%S", &tm_gmt8);
    std::cout << std::setw(35) << std::right << gmt_buf << std::setw(35) << std::left << "" << "\n";
    std::cout << std::setw(35) << std::right << gmt8_buf << std::setw(35) << std::left << "" << "\n";
    
    std::stringstream spread_line;
    spread_line << "Spread: $" << std::fixed << std::setprecision(2) << spread 
                << "  |  Mid: $" << std::setprecision(2) << mid;
    std::cout << std::setw(35) << std::right << spread_line.str() << std::setw(35) << std::left << "" << "\n";
    std::cout << std::string(70, '=') << "\n\n";
    
    // ASKS section
    std::cout << std::setw(35) << std::right << "ASKS (Sell Orders)" << std::setw(35) << std::left << "" << "\n";
    std::cout << std::setw(11) << std::right << "Price" << "  "
              << std::setw(10) << std::right << "BTC" << "  "
              << std::setw(9) << std::right << "USDT" << "  "
              << std::setw(10) << std::right << "Cum BTC" << "  "
              << std::setw(9) << std::right << "Cum USDT" << "\n";
    std::cout << std::string(70, '-') << "\n";
    
    // Display asks in reverse (highest first) - always 16 levels
    double cum_btc_ask = 0.0, cum_usdt_ask = 0.0;
    for (int i = levels - 1; i >= 0; --i) {
        if (i < (int)asks.size()) {
            double price = asks[i].first;
            double vol = asks[i].second;
            double usdt = price * vol;
            cum_btc_ask += vol;
            cum_usdt_ask += usdt;
            
            // Red color for asks
            std::cout << "\033[91m" << std::setw(11) << std::right << std::fixed 
                      << std::setprecision(2) << price << "\033[0m  "
                      << std::setw(10) << std::right << std::setprecision(7) << vol << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << usdt << "  "
                      << std::setw(10) << std::right << std::setprecision(4) << cum_btc_ask << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << cum_usdt_ask << "\n";
        } else {
            // Padding with "---" when not enough data
            std::cout << std::setw(11) << std::right << "---" << "  "
                      << std::setw(10) << std::right << "---" << "  "
                      << std::setw(9) << std::right << "---" << "  "
                      << std::setw(10) << std::right << std::setprecision(4) << cum_btc_ask << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << cum_usdt_ask << "\n";
        }
    }
    
    // BIDS section
    std::cout << "\n" << std::setw(35) << std::right << "BIDS (Buy Orders)" << std::setw(35) << std::left << "" << "\n";
    std::cout << std::setw(11) << std::right << "Price" << "  "
              << std::setw(10) << std::right << "BTC" << "  "
              << std::setw(9) << std::right << "USDT" << "  "
              << std::setw(10) << std::right << "Cum BTC" << "  "
              << std::setw(9) << std::right << "Cum USDT" << "\n";
    std::cout << std::string(70, '-') << "\n";
    
    // Display bids (highest first - already sorted) - always 16 levels
    double cum_btc_bid = 0.0, cum_usdt_bid = 0.0;
    for (int i = 0; i < levels; ++i) {
        if (i < (int)bids.size()) {
            double price = bids[i].first;
            double vol = bids[i].second;
            double usdt = price * vol;
            cum_btc_bid += vol;
            cum_usdt_bid += usdt;
            
            // Green color for bids
            std::cout << "\033[92m" << std::setw(11) << std::right << std::fixed 
                      << std::setprecision(2) << price << "\033[0m  "
                      << std::setw(10) << std::right << std::setprecision(7) << vol << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << usdt << "  "
                      << std::setw(10) << std::right << std::setprecision(4) << cum_btc_bid << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << cum_usdt_bid << "\n";
        } else {
            // Padding with "---" when not enough data
            std::cout << std::setw(11) << std::right << "---" << "  "
                      << std::setw(10) << std::right << "---" << "  "
                      << std::setw(9) << std::right << "---" << "  "
                      << std::setw(10) << std::right << std::setprecision(4) << cum_btc_bid << "  "
                      << std::setw(9) << std::right << std::setprecision(0) << cum_usdt_bid << "\n";
        }
    }
    
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::flush;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "Starting Bybit OrderBook Feed (C++20 + simdjson)...\n";
    std::cout << "Connecting to Bybit WebSocket...\n\n";
    
    BybitOrderBook ob("BTCUSDT", 50);
    ob.start();
    
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);  // Wait for connection
    
    while (g_running) {
        display_orderbook(ob, 16);
        std::this_thread::sleep_for(500ms);  // 2 FPS like Python
    }
    
    ob.stop();
    std::cout << "\n\n✓ Stopped\n";
    return 0;
}

#endif // NO_MAIN
