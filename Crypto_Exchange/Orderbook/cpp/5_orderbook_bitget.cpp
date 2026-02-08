#include "5_orderbook_bitget.h"
#include <iostream>
#include <iomanip>
#include <sstream>
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
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/copy.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
namespace bio = boost::iostreams;

BitgetOrderBook::BitgetOrderBook(const std::string& symbol, int depth)
    : m_symbol(symbol), m_depth(depth) {
    m_bids.reserve(depth);
    m_asks.reserve(depth);
}

BitgetOrderBook::~BitgetOrderBook() {
    stop();
}

void BitgetOrderBook::start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&BitgetOrderBook::run_loop, this);
}

void BitgetOrderBook::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

bool BitgetOrderBook::process_message(const std::string& msg) {
    // Bitget sends: {"data":[{"bids":[["p","v"]],"asks":[["p","v"]]}]}
    try {
        // Check for ping/pong (handled in run_loop)
        if (msg.find("\"op\":\"ping\"") != std::string::npos) return false;
        
        if (msg.find("\"data\"") == std::string::npos) return false;
        
        // Lambda: parse [["price","vol"]] arrays
        auto parse = [](const std::string& json, const char* key) {
            std::vector<Level> result;
            auto key_pos = json.find(std::string("\"") + key + "\"");
            if (key_pos == std::string::npos) return result;
            
            auto start = json.find("[[", key_pos);
            if (start == std::string::npos) return result;
            start += 2;
            
            while (true) {
                auto p1 = json.find("\"", start);
                if (p1 == std::string::npos || p1 >= json.find("]]", start)) break;
                auto p2 = json.find("\"", p1 + 1);
                auto v1 = json.find("\"", p2 + 1);
                auto v2 = json.find("\"", v1 + 1);
                if (v2 == std::string::npos) break;
                
                double price = std::stod(json.substr(p1 + 1, p2 - p1 - 1));
                double volume = std::stod(json.substr(v1 + 1, v2 - v1 - 1));
                result.emplace_back(price, volume);
                
                start = v2 + 1;
                auto comma = json.find(",", start);
                if (comma == std::string::npos || comma >= json.find("]]", start)) break;
                start = comma + 1;
            }
            return result;
        };
        
        m_bids = parse(msg, "bids");
        m_asks = parse(msg, "asks");
        
        if (m_bids.empty() || m_asks.empty()) return false;
        
        // Sort: bids descending, asks ascending
        std::sort(m_bids.begin(), m_bids.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::sort(m_asks.begin(), m_asks.end(), [](auto& a, auto& b) { return a.first < b.first; });
        
        // Sanitize crossed levels
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

void BitgetOrderBook::run_loop() {
    while (m_running) {
        try {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            tcp::resolver resolver{ioc};
            websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

            auto results = resolver.resolve("ws.bitget.com", "443");
            net::connect(beast::get_lowest_layer(ws), results);
            beast::get_lowest_layer(ws).set_option(tcp::no_delay(true));
            
            SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "ws.bitget.com");
            ws.next_layer().handshake(ssl::stream_base::client);

            ws.handshake("ws.bitget.com", "/v2/ws/public");
            
            // Subscribe: {"op":"subscribe","args":[{"instType":"SPOT","channel":"books","instId":"BTCUSDT"}]}
            std::string sub = "{\"op\":\"subscribe\",\"args\":[{\"instType\":\"SPOT\",\"channel\":\"books\",\"instId\":\"" + 
                            m_symbol + "\"}]}";
            ws.write(net::buffer(sub));

            beast::flat_buffer buffer;
            while (m_running) {
                ws.read(buffer);
                auto data = buffer.data();
                std::string msg;
                
                // Try to decompress gzip
                try {
                    std::string compressed(static_cast<const char*>(data.data()), data.size());
                    std::stringstream compressed_stream(compressed);
                    std::stringstream decompressed_stream;
                    
                    bio::filtering_streambuf<bio::input> in;
                    in.push(bio::gzip_decompressor());
                    in.push(compressed_stream);
                    bio::copy(in, decompressed_stream);
                    
                    msg = decompressed_stream.str();
                } catch (...) {
                    // Not gzip, use as-is
                    msg = beast::buffers_to_string(data);
                }
                
                buffer.consume(buffer.size());
                
                // Handle ping/pong
                if (msg.find("\"op\":\"ping\"") != std::string::npos) {
                    ws.write(net::buffer("{\"op\":\"pong\"}"));
                    continue;
                }
                
                process_message(msg);
            }
            
        } catch (const std::exception& e) {
            if (m_running) {
                std::cerr << "Bitget error: " << e.what() << ", reconnecting...\n";
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

void display_orderbook(const BitgetOrderBook& ob, int levels = 16) {
    if (!ob.has_data()) return;
    
    const auto& bids = ob.bids();
    const auto& asks = ob.asks();
    if (bids.empty() || asks.empty()) return;
    
    // Timestamps (GMT and GMT+8)
    auto now = std::chrono::system_clock::now();
    auto time_t_gmt = std::chrono::system_clock::to_time_t(now);
    auto time_t_gmt8 = time_t_gmt + 8 * 3600;
    auto tm_gmt = *std::gmtime(&time_t_gmt);
    auto tm_gmt8 = *std::gmtime(&time_t_gmt8);
    
    // Calculate spread and mid
    double spread = asks[0].first - bids[0].first;
    double mid = (asks[0].first + bids[0].first) / 2.0;
    
    // Clear screen and header
    std::cout << "\033[2J\033[H\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::setw(35) << std::right << "Exchange: Bitget" << std::setw(35) << std::left << "" << "\n";
    
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
            double volume = asks[i].second;
            double usdt = price * volume;
            cum_btc_ask += volume;
            cum_usdt_ask += usdt;
            
            std::cout << "\033[91m" << std::setw(11) << std::fixed << std::setprecision(2) << price << "\033[0m" << "  "
                      << std::setw(10) << std::setprecision(7) << volume << "  "
                      << std::setw(9) << std::setprecision(0) << usdt << "  "
                      << std::setw(10) << std::setprecision(4) << cum_btc_ask << "  "
                      << std::setw(9) << std::setprecision(0) << cum_usdt_ask << "\n";
        } else {
            std::cout << std::setw(11) << std::right << "---" << "  "
                      << std::setw(10) << std::right << "---" << "  "
                      << std::setw(9) << std::right << "---" << "  "
                      << std::setw(10) << std::fixed << std::setprecision(4) << cum_btc_ask << "  "
                      << std::setw(9) << std::setprecision(0) << cum_usdt_ask << "\n";
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
            double volume = bids[i].second;
            double usdt = price * volume;
            cum_btc_bid += volume;
            cum_usdt_bid += usdt;
            
            std::cout << "\033[92m" << std::setw(11) << std::fixed << std::setprecision(2) << price << "\033[0m" << "  "
                      << std::setw(10) << std::setprecision(7) << volume << "  "
                      << std::setw(9) << std::setprecision(0) << usdt << "  "
                      << std::setw(10) << std::setprecision(4) << cum_btc_bid << "  "
                      << std::setw(9) << std::setprecision(0) << cum_usdt_bid << "\n";
        } else {
            std::cout << std::setw(11) << std::right << "---" << "  "
                      << std::setw(10) << std::right << "---" << "  "
                      << std::setw(9) << std::right << "---" << "  "
                      << std::setw(10) << std::fixed << std::setprecision(4) << cum_btc_bid << "  "
                      << std::setw(9) << std::setprecision(0) << cum_usdt_bid << "\n";
        }
    }
    
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::flush;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "Starting Bitget OrderBook Feed (C++20)...\n";
    std::cout << "Connecting to Bitget WebSocket...\n\n";
    
    BitgetOrderBook ob("BTCUSDT", 20);
    ob.start();
    
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);
    
    while (g_running) {
        display_orderbook(ob, 16);
        std::this_thread::sleep_for(500ms);  // 2 FPS
    }
    
    ob.stop();
    std::cout << "\n\n✓ Stopped\n";
    return 0;
}

#endif // NO_MAIN
