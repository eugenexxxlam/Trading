#include "1_orderbook_binance.h"
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

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace orderbook {

BinanceOrderBook::BinanceOrderBook(const std::string& symbol, int depth)
    : symbol_(symbol), depth_(depth) {
    bids_.reserve(depth);
    asks_.reserve(depth);
}

BinanceOrderBook::~BinanceOrderBook() {
    stop();
}

void BinanceOrderBook::start() {
    if (running_.exchange(true)) return;
    worker_ = std::make_unique<std::thread>([this] { run_loop(); });
}

void BinanceOrderBook::stop() {
    running_ = false;
    if (worker_ && worker_->joinable()) worker_->join();
}

void BinanceOrderBook::run_loop() {
    using namespace std::chrono_literals;
    
    while (running_) {
        try {
            net::io_context ioc;
            ssl::context ctx{ssl::context::tlsv12_client};
            tcp::resolver resolver{ioc};
            websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

            auto results = resolver.resolve("stream.binance.com", "9443");
            net::connect(beast::get_lowest_layer(ws), results);
            beast::get_lowest_layer(ws).set_option(tcp::no_delay(true));
            
            SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "stream.binance.com");
            ws.next_layer().handshake(ssl::stream_base::client);

            std::string symbol_lower = symbol_;
            std::transform(symbol_lower.begin(), symbol_lower.end(), symbol_lower.begin(), ::tolower);
            std::string target = "/ws/" + symbol_lower + "@depth20@100ms";
            
            ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, "BinanceOrderBook/1.0");
            }));
            ws.handshake("stream.binance.com", target);

            while (running_) {
                beast::flat_buffer buffer;
                ws.read(buffer);
                process_message(beast::buffers_to_string(buffer.data()));
            }
            
            ws.close(websocket::close_code::normal);
        } catch (const std::exception& e) {
            if (running_) std::this_thread::sleep_for(2s);
        }
    }
}

void BinanceOrderBook::process_message(const std::string& message) {
    try {
        bids_.clear();
        asks_.clear();
        
        simdjson::padded_string json(message);
        auto doc = parser_.iterate(json);
        
        // Parse bids
        for (auto bid : doc["bids"].get_array()) {
            auto arr = bid.get_array();
            auto it = arr.begin();
            double price = std::stod(std::string((*it).get_string().value()));
            ++it;
            double qty = std::stod(std::string((*it).get_string().value()));
            bids_.emplace_back(price, qty);
        }
        
        // Parse asks
        for (auto ask : doc["asks"].get_array()) {
            auto arr = ask.get_array();
            auto it = arr.begin();
            double price = std::stod(std::string((*it).get_string().value()));
            ++it;
            double qty = std::stod(std::string((*it).get_string().value()));
            asks_.emplace_back(price, qty);
        }
        
        // Sort: bids descending, asks ascending
        std::sort(bids_.begin(), bids_.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::sort(asks_.begin(), asks_.end(), [](auto& a, auto& b) { return a.first < b.first; });
        
        has_data_ = true;
    } catch (...) {}
}

} // namespace orderbook

#ifndef NO_MAIN

using namespace orderbook;

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

void display_orderbook(const BinanceOrderBook& ob, int levels = 16) {
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
    std::cout << std::setw(35) << std::right << "Exchange: Binance" << std::setw(35) << std::left << "" << "\n";
    
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
    
    std::cout << "Starting Binance OrderBook Feed (C++20 + simdjson)...\n";
    std::cout << "Connecting to Binance WebSocket...\n\n";
    
    BinanceOrderBook ob("BTCUSDT", 20);
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
