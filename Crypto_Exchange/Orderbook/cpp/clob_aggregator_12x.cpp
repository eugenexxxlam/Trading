#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <sstream>
#include <ctime>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fstream>
#include <simdjson.h>
#include "0_CLOB_MM.h"

const char* CONFIG_FILE = "config.json";

// Include all 14 exchange headers
#include "1_orderbook_binance.h"
#include "2_orderbook_okx.h"
#include "3_orderbook_bybit.h"
#include "4_orderbook_gate.h"
#include "5_orderbook_bitget.h"
#include "6_orderbook_bingx.h"
#include "7_orderbook_bitfinex.h"
#include "8_orderbook_HTX.h"
#include "9_orderbook_hyperliquid.h"
#include "10_orderbook_dydx.h"
#include "11_orderbook_coinbase.h"
#include "12_orderbook_kraken.h"
#include "13_orderbook_cryptocom.h"
#include "14_orderbook_kucoin.h"

using namespace clob;

// ============================================================================
// ORDERBOOK CONTROLS - Tune these parameters to control spread & liquidity
// ============================================================================
struct DisplayConfig {
    // Liquidity Filter
    double min_notional = 10000.0;     // Min USDT per level (lower = more levels)
                                       // 1000 = very tight, includes tiny orders
                                       // 5000 = balanced filtering
                                       // 10000+ = only large liquidity
    
    // Output Depth
    int depth = 32;                    // Number of price levels per side
                                       // 8 = compact view
                                       // 16 = standard view
                                       // 32+ = deep book view
    
    // Display Performance
    int display_fps = 10;              // Display refresh rate (frames per second)
                                       // 10 = low CPU (100ms updates)
                                       // 30 = balanced (33ms updates)
                                       // 60 = high refresh (16ms updates)
    
    int aggregation_hz = 20;           // Aggregation update rate (Hz)
                                       // 20 = low CPU (50ms aggregation)
                                       // 100 = balanced (10ms aggregation)
                                       // 200+ = maximum (5ms aggregation)
    
    // Spread Tightening Controls (basis points, 1 bp = 0.01%)
    double bid_markup_bps = 0.8;       // Push BID prices UP to tighten spread
    double ask_markup_bps = 0.1;       // Push ASK prices DOWN to tighten spread
    double spread_floor_bps = 0.0;     // Minimum spread enforcement (safety net)
    
    // Exchange Weighting System
    std::unordered_map<std::string, double> weights = {
        // TIER 1: Price Discovery Leaders (tight spreads, high volume)
        {"binance", 1.5},      // Global leader, $0.01 spread
        {"coinbase", 1.5},     // US market leader, $0.01 spread
        {"kraken", 1.2},       // EU leader, $0.70 spread
        {"bingx", 1.2},        // Consistently $0.01 spread
        
        // TIER 2: Reliable Contributors (tight spreads)
        {"gate", 1.0},         // Asian market, $0.10 spread
        {"kucoin", 1.0},       // Popular CEX, $0.10 spread
        {"bitget", 1.0},       // Growing exchange, $0.01 spread
        {"bitfinex", 1.0},     // Established exchange, $1-2 spread
        {"cryptocom", 0.8},    // Growing CEX, $2-3 spread
        
        // TIER 3: Supplementary (moderate spreads)
        {"hyperliquid", 0.6},  // DEX but tight $1.00 spread
        {"htx", 0.5},          // $3-5 spread typical
        {"bybit", 0.5},        // $0.10 spread typical
        
        // TIER 4: Wide Spreads (reduced influence)
        {"okx", 0.3},          // Often $10-20 spreads
        {"dydx", 0.2}          // DEX with $10-20 spreads
    };
};

// Global controls
DisplayConfig CONTROLS;
std::unique_ptr<CLOBAggregator>* g_aggregator_ptr = nullptr;

std::atomic<bool> g_running{true};

// ANSI color codes
namespace Color {
    const char* RESET = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* RED = "\033[31m";
    const char* CYAN = "\033[36m";
}

bool load_config(const char* filename, DisplayConfig& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot open config file: " << filename << "\n";
        return false;
    }
    
    std::string json_str((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    auto error = parser.parse(json_str).get(doc);
    if (error) {
        std::cerr << "[ERROR] JSON parse error: " << error << "\n";
        return false;
    }
    
    // Load config values
    config.min_notional = double(doc["min_notional"]);
    config.depth = int64_t(doc["depth"]);
    config.display_fps = int64_t(doc["display_fps"]);
    config.aggregation_hz = int64_t(doc["aggregation_hz"]);
    config.bid_markup_bps = double(doc["bid_markup_bps"]);
    config.ask_markup_bps = double(doc["ask_markup_bps"]);
    config.spread_floor_bps = double(doc["spread_floor_bps"]);
    
    // Load weights
    config.weights.clear();
    for (auto [key, value] : simdjson::dom::object(doc["weights"])) {
        config.weights[std::string(key)] = double(value);
    }
    
    return true;
}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n\nReceived interrupt signal. Shutting down gracefully...\n";
        g_running.store(false);
    } else if (signal == SIGUSR1) {
        std::cout << "\n[INFO] Received reload signal (SIGUSR1)...\n";
        DisplayConfig new_config;
        if (load_config(CONFIG_FILE, new_config)) {
            CONTROLS = new_config;
            
            // Update aggregator config if running
            if (g_aggregator_ptr && *g_aggregator_ptr) {
                CLOBConfig config;
                config.min_notional = CONTROLS.min_notional;
                config.depth = CONTROLS.depth;
                config.aggregation_hz = CONTROLS.aggregation_hz;
                config.bid_markup_bps = CONTROLS.bid_markup_bps;
                config.ask_markup_bps = CONTROLS.ask_markup_bps;
                config.spread_floor_bps = CONTROLS.spread_floor_bps;
                config.weights = CONTROLS.weights;
                (*g_aggregator_ptr)->update_config(config);
                
                std::cout << "[INFO] ✓ Configuration reloaded successfully!\n";
                std::cout << "[INFO]   Bid Markup: " << CONTROLS.bid_markup_bps << " bps\n";
                std::cout << "[INFO]   Ask Markup: " << CONTROLS.ask_markup_bps << " bps\n";
                std::cout << "[INFO]   Min Notional: $" << CONTROLS.min_notional << "\n";
                std::cout << "[INFO]   Aggregation: " << CONTROLS.aggregation_hz << " Hz\n";
            }
        } else {
            std::cerr << "[ERROR] Failed to reload configuration\n";
        }
    }
}

// Helper to format numbers with thousand separators
std::string format_number(double num, int decimals) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(decimals) << num;
    std::string str = ss.str();
    
    // Add thousand separators
    auto pos = str.find('.');
    if (pos == std::string::npos) pos = str.length();
    
    int insertPos = pos - 3;
    while (insertPos > 0) {
        str.insert(insertPos, ",");
        insertPos -= 3;
    }
    return str;
}

// Clear screen and reset cursor (like curses)
void clear_screen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

// Exchange status tracking
struct ExchangeStatus {
    bool connected = false;
    double last_update = 0.0;
    double age = 0.0;
    double confidence = 0.0;
    double best_bid = 0.0;
    double best_ask = 0.0;
    double spread = 0.0;
};

std::unordered_map<std::string, ExchangeStatus> g_exchange_status;

// Calculate confidence based on age (matching Python)
double calculate_confidence(double age_seconds) {
    if (age_seconds < 3.0) {
        return 1.0;  // 100% confidence
    } else if (age_seconds < 8.0) {
        return 1.0 - (age_seconds - 3.0) * 0.12;  // Linear decay 100% → 40%
    } else if (age_seconds < 15.0) {
        return 0.4 - (age_seconds - 8.0) * 0.0429;  // Aggressive decay 40% → 10%
    } else {
        return 0.05;  // 5% confidence for stale data
    }
}

void print_orderbook_snapshot(const CLOBAggregator& aggregator, double fps) {
    clear_screen();
    
    auto [bids, asks] = aggregator.get_orderbook();
    
    // Find best bid and ask
    double best_bid = 0.0, best_ask = 0.0;
    
    for (const auto& bid : bids) {
        if (bid.price > 0) {
            if (best_bid == 0.0) best_bid = bid.price;
            break;
        }
    }
    
    for (const auto& ask : asks) {
        if (ask.price > 0) {
            if (best_ask == 0.0) best_ask = ask.price;
            break;
        }
    }
    
    if (best_bid == 0 || best_ask == 0) {
        std::cout << "[INFO] Waiting for orderbook data...\n";
        return;
    }
    
    double spread = best_ask - best_bid;
    double mid = (best_bid + best_ask) / 2.0;
    
    // GMT timestamps
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm gmt_tm;
    gmtime_r(&now_time, &gmt_tm);
    
    // Calculate GMT+8
    time_t gmt8_time = now_time + 8 * 3600;
    std::tm gmt8_tm;
    gmtime_r(&gmt8_time, &gmt8_tm);
    
    char gmt_buf[64], gmt8_buf[64];
    strftime(gmt_buf, sizeof(gmt_buf), "%Y-%m-%d %H:%M:%S", &gmt_tm);
    strftime(gmt8_buf, sizeof(gmt8_buf), "%Y-%m-%d %H:%M:%S", &gmt8_tm);
    
    // Print header
    std::cout << "======================================================================\n";
    std::cout << "                   AGGREGATED CLOB - Multi-Exchange\n";
    std::cout << "                       GMT: " << gmt_buf << "\n";
    std::cout << "                      GMT+8: " << gmt8_buf << "\n";
    std::cout << "           Spread: $" << std::fixed << std::setprecision(2) << spread
              << "  |  Mid: $" << format_number(mid, 2) << "  |  FPS: " 
              << std::setprecision(1) << fps << "\n";
    std::cout << "======================================================================\n\n";
    
    // Count high quality exchanges
    int high_quality = 0;
    for (const auto& [name, status] : g_exchange_status) {
        if (status.confidence >= 0.7) high_quality++;
    }
    
    // Exchange status table
    std::cout << "     EXCHANGE STATUS - Confidence Weighting (" << high_quality << "/14 high quality)      \n";
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "Exchange      Confidence  Weight     Spread     Age      Quality\n";
    std::cout << "----------------------------------------------------------------------\n";
    
    const char* exchanges[] = {"binance", "okx", "bybit", "gate", "kucoin", "bitget", 
                               "bingx", "bitfinex", "htx", "hyperliquid", "dydx", 
                               "coinbase", "kraken", "cryptocom"};
    
    for (const auto& ex_name : exchanges) {
        if (g_exchange_status.count(ex_name)) {
            const auto& status = g_exchange_status[ex_name];
            double base_weight = CONTROLS.weights[ex_name];
            double effective_weight = base_weight * status.confidence;
            
            const char* color;
            const char* quality;
            
            if (status.confidence >= 0.8) {
                color = Color::GREEN;
                quality = "EXCELLENT";
            } else if (status.confidence >= 0.4) {
                color = Color::YELLOW;
                quality = "GOOD";
            } else if (status.confidence >= 0.15) {
                color = Color::YELLOW;
                quality = "AGING";
            } else {
                color = Color::RED;
                quality = "STALE";
            }
            
            std::cout << color << std::left << std::setw(12) << ex_name
                      << std::right << std::setw(9) << std::fixed << std::setprecision(0) 
                      << (status.confidence * 100) << "%  "
                      << std::setw(6) << std::setprecision(2) << effective_weight << "x"
                      << " $" << std::setw(8) << std::setprecision(2) << status.spread
                      << " " << std::setw(6) << std::setprecision(1) << status.age << "s"
                      << std::setw(13) << quality << Color::RESET << "\n";
        } else {
            std::cout << Color::RED << std::left << std::setw(12) << ex_name
                      << "       0%    0.00x      ---     ---  DISCONNECTED" 
                      << Color::RESET << "\n";
        }
    }
    std::cout << "----------------------------------------------------------------------\n\n";
    
    // Display ASKS
    std::cout << "                          ASKS (Sell Orders)\n";
    std::cout << "      Price         BTC       USDT     Cum BTC   Cum USDT  LP\n";
    std::cout << "----------------------------------------------------------------------\n";
    
    double cum_btc = 0.0, cum_usdt = 0.0;
    int ask_count = 0;
    
    for (auto it = asks.rbegin(); it != asks.rend() && ask_count < 17; ++it) {
        if (it->price > 0) {
            double notional = it->price * it->volume;
            cum_btc += it->volume;
            cum_usdt += notional;
            
            std::cout << Color::RED << "  " << std::setw(10) << format_number(it->price, 2)
                      << "   " << std::setw(10) << std::fixed << std::setprecision(7) << it->volume
                      << "     " << std::setw(6) << std::fixed << std::setprecision(0) << notional
                      << "      " << std::setw(6) << std::fixed << std::setprecision(4) << cum_btc
                      << "  " << std::setw(9) << std::fixed << std::setprecision(0) << cum_usdt
                      << "  " << std::left << std::setw(8) << it->exchange << Color::RESET << "\n";
            ask_count++;
        }
    }
    
    std::cout << "\n";
    
    // Display BIDS
    std::cout << "                          BIDS (Buy Orders)\n";
    std::cout << "      Price         BTC       USDT     Cum BTC   Cum USDT  LP\n";
    std::cout << "----------------------------------------------------------------------\n";
    
    cum_btc = 0.0;
    cum_usdt = 0.0;
    int bid_count = 0;
    
    for (const auto& bid : bids) {
        if (bid.price > 0 && bid_count < 17) {
            double notional = bid.price * bid.volume;
            cum_btc += bid.volume;
            cum_usdt += notional;
            
            std::cout << Color::GREEN << "  " << std::setw(10) << format_number(bid.price, 2)
                      << "   " << std::setw(10) << std::fixed << std::setprecision(7) << bid.volume
                      << "     " << std::setw(6) << std::fixed << std::setprecision(0) << notional
                      << "      " << std::setw(6) << std::fixed << std::setprecision(4) << cum_btc
                      << "  " << std::setw(9) << std::fixed << std::setprecision(0) << cum_usdt
                      << "  " << std::left << std::setw(8) << bid.exchange << Color::RESET << "\n";
            bid_count++;
        }
    }
    
    std::cout << "======================================================================\n";
}

int main() {
    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGUSR1, signal_handler);
    
    // Load configuration from file
    std::cout << "[INFO] Loading configuration from " << CONFIG_FILE << "...\n";
    if (!load_config(CONFIG_FILE, CONTROLS)) {
        std::cerr << "[ERROR] Using default configuration instead\n";
    }
    
    // Create aggregator with controls
    CLOBConfig config;
    config.min_notional = CONTROLS.min_notional;
    config.depth = CONTROLS.depth;
    config.aggregation_hz = CONTROLS.aggregation_hz;
    config.bid_markup_bps = CONTROLS.bid_markup_bps;
    config.ask_markup_bps = CONTROLS.ask_markup_bps;
    config.spread_floor_bps = CONTROLS.spread_floor_bps;
    config.weights = CONTROLS.weights;
    
    std::cout << "======================================================================\n";
    std::cout << "      CLOB Aggregator - 14 Exchange Multi-Orderbook\n";
    std::cout << "======================================================================\n";
    std::cout << "Exchanges: 14 (Binance, Coinbase, Kraken, BingX, Gate, KuCoin,\n";
    std::cout << "               Bitget, Bitfinex, Crypto.com, Hyperliquid,\n";
    std::cout << "               HTX, OKX, Bybit, dYdX)\n";
    std::cout << "Aggregation: 100 Hz | Min Notional: $10,000\n";
    std::cout << "Bid Markup: +" << CONTROLS.bid_markup_bps << " bps | Ask Markup: +" << CONTROLS.ask_markup_bps << " bps\n";
    std::cout << "Config: " << CONFIG_FILE << " | Hot Reload: kill -SIGUSR1 $PID\n";
    std::cout << "======================================================================\n\n";
    
    auto aggregator = std::make_unique<CLOBAggregator>(config);
    g_aggregator_ptr = &aggregator;
    
    // Initialize all 14 exchange orderbooks
    std::cout << "→ Initializing exchange orderbooks...\n";
    
    orderbook::BinanceOrderBook binance("BTCUSDT", 20);
    OKXOrderBook okx("BTC-USDT", 20);
    BybitOrderBook bybit("BTCUSDT", 50);
    GateOrderBook gate("BTC_USDT", 20);
    BitgetOrderBook bitget("BTCUSDT", 20);
    bingx::OrderBook bingx_ob;
    bitfinex::OrderBook bitfinex_ob;
    htx::OrderBook htx_ob;
    hyperliquid::OrderBook hyperliquid_ob;
    dydx::OrderBook dydx_ob;
    coinbase::OrderBook coinbase_ob;
    kraken::OrderBook kraken_ob;
    cryptocom::OrderBook cryptocom_ob;
    kucoin::OrderBook kucoin_ob;
    
    std::cout << "\n✓ CLOB Aggregator started (100 Hz)\n";
    aggregator->start();
    
    // Start all exchange WebSocket feeds
    std::cout << "→ Starting WebSocket feeds...\n";
    binance.start();
    okx.start();
    bybit.start();
    gate.start();
    bitget.start();
    bingx_ob.start();
    bitfinex_ob.start();
    htx_ob.start();
    hyperliquid_ob.start();
    dydx_ob.start();
    coinbase_ob.start();
    kraken_ob.start();
    cryptocom_ob.start();
    kucoin_ob.start();
    
    // Create feeder threads for all 12 exchanges with status tracking
    std::thread binance_feeder([&]() {
        while (g_running.load()) {
            if (binance.has_data()) {
                const auto& bids = binance.bids();
                const auto& asks = binance.asks();
                aggregator->update_exchange("binance", bids, asks);
                
                // Update status
                auto& status = g_exchange_status["binance"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                status.age = 0.0;
                status.confidence = 1.0;
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread okx_feeder([&]() {
        while (g_running.load()) {
            if (okx.has_data()) {
                const auto& bids = okx.bids();
                const auto& asks = okx.asks();
                aggregator->update_exchange("okx", bids, asks);
                auto& status = g_exchange_status["okx"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread bybit_feeder([&]() {
        while (g_running.load()) {
            if (bybit.has_data()) {
                const auto& bids = bybit.bids();
                const auto& asks = bybit.asks();
                aggregator->update_exchange("bybit", bids, asks);
                auto& status = g_exchange_status["bybit"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread gate_feeder([&]() {
        while (g_running.load()) {
            if (gate.has_data()) {
                const auto& bids = gate.bids();
                const auto& asks = gate.asks();
                aggregator->update_exchange("gate", bids, asks);
                auto& status = g_exchange_status["gate"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread bitget_feeder([&]() {
        while (g_running.load()) {
            if (bitget.has_data()) {
                const auto& bids = bitget.bids();
                const auto& asks = bitget.asks();
                aggregator->update_exchange("bitget", bids, asks);
                auto& status = g_exchange_status["bitget"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread bingx_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = bingx_ob.get_bids();
            auto asks_vec = bingx_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("bingx", bids, asks);
                auto& status = g_exchange_status["bingx"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread bitfinex_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = bitfinex_ob.get_bids();
            auto asks_vec = bitfinex_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("bitfinex", bids, asks);
                auto& status = g_exchange_status["bitfinex"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread htx_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = htx_ob.get_bids();
            auto asks_vec = htx_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("htx", bids, asks);
                auto& status = g_exchange_status["htx"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread hyperliquid_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = hyperliquid_ob.get_bids();
            auto asks_vec = hyperliquid_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("hyperliquid", bids, asks);
                auto& status = g_exchange_status["hyperliquid"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread dydx_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = dydx_ob.get_bids();
            auto asks_vec = dydx_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("dydx", bids, asks);
                auto& status = g_exchange_status["dydx"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread coinbase_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = coinbase_ob.get_bids();
            auto asks_vec = coinbase_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("coinbase", bids, asks);
                auto& status = g_exchange_status["coinbase"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread kraken_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = kraken_ob.get_bids();
            auto asks_vec = kraken_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("kraken", bids, asks);
                auto& status = g_exchange_status["kraken"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread cryptocom_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = cryptocom_ob.get_bids();
            auto asks_vec = cryptocom_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("cryptocom", bids, asks);
                auto& status = g_exchange_status["cryptocom"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::thread kucoin_feeder([&]() {
        while (g_running.load()) {
            auto bids_vec = kucoin_ob.get_bids();
            auto asks_vec = kucoin_ob.get_asks();
            if (!bids_vec.empty() && !asks_vec.empty()) {
                Levels bids, asks;
                for (const auto& level : bids_vec) {
                    bids.push_back({level.price, level.volume});
                }
                for (const auto& level : asks_vec) {
                    asks.push_back({level.price, level.volume});
                }
                aggregator->update_exchange("kucoin", bids, asks);
                auto& status = g_exchange_status["kucoin"];
                status.connected = true;
                status.last_update = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (!bids.empty() && !asks.empty()) {
                    status.best_bid = bids[0].first;
                    status.best_ask = asks[0].first;
                    status.spread = status.best_ask - status.best_bid;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    
    std::cout << "\n[INFO] Waiting for orderbook data from exchanges...\n";
    
    std::cout << "\n✓ Aggregator running. Press Ctrl+C to exit.\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Display loop with FPS tracking
    auto last_frame = std::chrono::steady_clock::now();
    std::vector<double> frame_times;
    double fps = 0.0;
    
    while (g_running.load()) {
        auto frame_start = std::chrono::steady_clock::now();
        
        // Update exchange ages and confidence
        double now = std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        for (auto& [name, status] : g_exchange_status) {
            if (status.connected) {
                status.age = now - status.last_update;
                status.confidence = calculate_confidence(status.age);
            }
        }
        
        print_orderbook_snapshot(*aggregator, fps);
        
        // Calculate FPS
        auto frame_end = std::chrono::steady_clock::now();
        double frame_time = std::chrono::duration<double>(frame_end - frame_start).count();
        frame_times.push_back(frame_time);
        if (frame_times.size() > 30) frame_times.erase(frame_times.begin());
        
        if (!frame_times.empty()) {
            double avg_time = 0;
            for (auto t : frame_times) avg_time += t;
            avg_time /= frame_times.size();
            fps = avg_time > 0 ? 1.0 / avg_time : 0.0;
        }
        
        int sleep_ms = 1000 / CONTROLS.display_fps;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    
    // Cleanup
    std::cout << "\n\n[INFO] Stopping aggregator...\n";
    aggregator->stop();
    
    std::cout << "[INFO] Stopping exchange feeds...\n";
    binance.stop();
    okx.stop();
    bybit.stop();
    gate.stop();
    bitget.stop();
    bingx_ob.stop();
    bitfinex_ob.stop();
    htx_ob.stop();
    hyperliquid_ob.stop();
    dydx_ob.stop();
    coinbase_ob.stop();
    kraken_ob.stop();
    cryptocom_ob.stop();
    kucoin_ob.stop();
    
    // Join feeder threads
    if (binance_feeder.joinable()) binance_feeder.join();
    if (okx_feeder.joinable()) okx_feeder.join();
    if (bybit_feeder.joinable()) bybit_feeder.join();
    if (gate_feeder.joinable()) gate_feeder.join();
    if (bitget_feeder.joinable()) bitget_feeder.join();
    if (bingx_feeder.joinable()) bingx_feeder.join();
    if (bitfinex_feeder.joinable()) bitfinex_feeder.join();
    if (htx_feeder.joinable()) htx_feeder.join();
    if (hyperliquid_feeder.joinable()) hyperliquid_feeder.join();
    if (dydx_feeder.joinable()) dydx_feeder.join();
    if (coinbase_feeder.joinable()) coinbase_feeder.join();
    if (kraken_feeder.joinable()) kraken_feeder.join();
    if (cryptocom_feeder.joinable()) cryptocom_feeder.join();
    if (kucoin_feeder.joinable()) kucoin_feeder.join();
    
    std::cout << "\n✓ Shutdown complete.\n";
    
    return 0;
}
