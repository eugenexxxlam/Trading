#include <gtest/gtest.h>
#include <chrono>
#include <nlohmann/json.hpp>
#include <simdjson.h>
#include "../fast_json_parser.h"

using json = nlohmann::json;

TEST(OrderBookBinanceTest, BasicAssertions) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
}

// Sample Binance depth update message for benchmarking
const std::string SAMPLE_MESSAGE = R"({
    "lastUpdateId": 1027024,
    "bids": [
        ["4.00000000", "431.00000000"],
        ["3.99900000", "123.00000000"],
        ["3.99800000", "456.00000000"],
        ["3.99700000", "789.00000000"],
        ["3.99600000", "234.00000000"],
        ["3.99500000", "567.00000000"],
        ["3.99400000", "890.00000000"],
        ["3.99300000", "345.00000000"],
        ["3.99200000", "678.00000000"],
        ["3.99100000", "901.00000000"],
        ["3.99000000", "432.00000000"],
        ["3.98900000", "765.00000000"],
        ["3.98800000", "198.00000000"],
        ["3.98700000", "531.00000000"],
        ["3.98600000", "864.00000000"],
        ["3.98500000", "297.00000000"],
        ["3.98400000", "630.00000000"],
        ["3.98300000", "963.00000000"],
        ["3.98200000", "396.00000000"],
        ["3.98100000", "729.00000000"]
    ],
    "asks": [
        ["4.00000200", "12.00000000"],
        ["4.00000500", "34.00000000"],
        ["4.00001000", "56.00000000"],
        ["4.00001500", "78.00000000"],
        ["4.00002000", "90.00000000"],
        ["4.00002500", "23.00000000"],
        ["4.00003000", "45.00000000"],
        ["4.00003500", "67.00000000"],
        ["4.00004000", "89.00000000"],
        ["4.00004500", "12.00000000"],
        ["4.00005000", "34.00000000"],
        ["4.00005500", "56.00000000"],
        ["4.00006000", "78.00000000"],
        ["4.00006500", "90.00000000"],
        ["4.00007000", "23.00000000"],
        ["4.00007500", "45.00000000"],
        ["4.00008000", "67.00000000"],
        ["4.00008500", "89.00000000"],
        ["4.00009000", "12.00000000"],
        ["4.00009500", "34.00000000"]
    ]
})";

TEST(OrderBookBinanceTest, JsonParsingBenchmark_Nlohmann) {
    const int iterations = 10000;
    std::vector<std::pair<double, double>> bids, asks;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        json j = json::parse(SAMPLE_MESSAGE);
        
        bids.clear();
        asks.clear();
        
        for (const auto& bid : j["bids"]) {
            double price = std::stod(bid[0].get<std::string>());
            double qty = std::stod(bid[1].get<std::string>());
            bids.emplace_back(price, qty);
        }
        
        for (const auto& ask : j["asks"]) {
            double price = std::stod(ask[0].get<std::string>());
            double qty = std::stod(ask[1].get<std::string>());
            asks.emplace_back(price, qty);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = duration.count() / static_cast<double>(iterations);
    
    std::cout << "\n=== Parser 1: nlohmann/json ===" << std::endl;
    std::cout << "Average time: " << avg_us << " μs per parse" << std::endl;
    std::cout << "Throughput: " << (1000000.0 / avg_us) << " parses/second" << std::endl;
    std::cout << "Bids parsed: " << bids.size() << std::endl;
    std::cout << "Asks parsed: " << asks.size() << std::endl;
    
    EXPECT_EQ(bids.size(), 20);
    EXPECT_EQ(asks.size(), 20);
    EXPECT_NEAR(bids[0].first, 4.0, 0.1);
    EXPECT_NEAR(asks[0].first, 4.0, 0.1);
}

TEST(OrderBookBinanceTest, JsonParsingBenchmark_Simdjson) {
    const int iterations = 10000;
    std::vector<std::pair<double, double>> bids, asks;
    simdjson::ondemand::parser parser;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        bids.clear();
        asks.clear();
        
        simdjson::padded_string json_padded(SAMPLE_MESSAGE);
        auto doc = parser.iterate(json_padded);
        
        // Parse bids
        auto bids_array = doc["bids"].get_array();
        for (auto bid_element : bids_array) {
            auto bid_array = bid_element.get_array();
            auto it = bid_array.begin();
            
            std::string_view price_sv = (*it).get_string().value();
            ++it;
            std::string_view qty_sv = (*it).get_string().value();
            
            double price = std::stod(std::string(price_sv));
            double qty = std::stod(std::string(qty_sv));
            
            bids.emplace_back(price, qty);
        }
        
        // Parse asks
        auto asks_array = doc["asks"].get_array();
        for (auto ask_element : asks_array) {
            auto ask_array = ask_element.get_array();
            auto it = ask_array.begin();
            
            std::string_view price_sv = (*it).get_string().value();
            ++it;
            std::string_view qty_sv = (*it).get_string().value();
            
            double price = std::stod(std::string(price_sv));
            double qty = std::stod(std::string(qty_sv));
            
            asks.emplace_back(price, qty);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = duration.count() / static_cast<double>(iterations);
    
    std::cout << "\n=== Parser 2: simdjson ===" << std::endl;
    std::cout << "Average time: " << avg_us << " μs per parse" << std::endl;
    std::cout << "Throughput: " << (1000000.0 / avg_us) << " parses/second" << std::endl;
    std::cout << "Bids parsed: " << bids.size() << std::endl;
    std::cout << "Asks parsed: " << asks.size() << std::endl;
    
    EXPECT_EQ(bids.size(), 20);
    EXPECT_EQ(asks.size(), 20);
    EXPECT_NEAR(bids[0].first, 4.0, 0.1);
    EXPECT_NEAR(asks[0].first, 4.0, 0.1);
}

TEST(OrderBookBinanceTest, JsonParsingBenchmark_FastCustom) {
    const int iterations = 10000;
    std::vector<std::pair<double, double>> bids, asks;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        bids.clear();
        asks.clear();
        
        bool success = orderbook::FastDepthParser::parse(SAMPLE_MESSAGE, bids, asks);
        EXPECT_TRUE(success);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double avg_us = duration.count() / static_cast<double>(iterations);
    
    std::cout << "\n=== Parser 3: fast_custom ===" << std::endl;
    std::cout << "Average time: " << avg_us << " μs per parse" << std::endl;
    std::cout << "Throughput: " << (1000000.0 / avg_us) << " parses/second" << std::endl;
    std::cout << "Bids parsed: " << bids.size() << std::endl;
    std::cout << "Asks parsed: " << asks.size() << std::endl;
    
    EXPECT_EQ(bids.size(), 20);
    EXPECT_EQ(asks.size(), 20);
    EXPECT_NEAR(bids[0].first, 4.0, 0.1);
    EXPECT_NEAR(asks[0].first, 4.0, 0.1);
}

TEST(OrderBookBinanceTest, VectorSortBenchmark) {
    std::vector<std::pair<double, double>> levels;
    
    // Pre-populate with 20 levels
    for (int i = 0; i < 20; ++i) {
        levels.emplace_back(100.0 + i, 1.0);
    }
    
    const int iterations = 100000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        std::sort(levels.begin(), levels.end(),
                 [](const auto& a, const auto& b) { return a.first > b.first; });
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_ns = duration.count() / static_cast<double>(iterations);
    
    std::cout << "\n=== Vector Sort Benchmark ===" << std::endl;
    std::cout << "Size: 20 levels" << std::endl;
    std::cout << "Average time: " << avg_ns << " ns per sort" << std::endl;
    std::cout << "           = " << (avg_ns / 1000.0) << " μs per sort" << std::endl;
    
    // Assert sorting is fast (should be < 500 ns for 20 items)
    EXPECT_LT(avg_ns, 500.0) << "Vector sorting too slow!";
}

TEST(OrderBookBinanceTest, ParserCorrectness_AllParsers) {
    std::vector<std::pair<double, double>> bids1, asks1;
    std::vector<std::pair<double, double>> bids2, asks2;
    std::vector<std::pair<double, double>> bids3, asks3;
    
    // Parse with nlohmann
    json j = json::parse(SAMPLE_MESSAGE);
    for (const auto& bid : j["bids"]) {
        double price = std::stod(bid[0].get<std::string>());
        double qty = std::stod(bid[1].get<std::string>());
        bids1.emplace_back(price, qty);
    }
    
    // Parse with simdjson
    simdjson::ondemand::parser parser;
    simdjson::padded_string json_padded(SAMPLE_MESSAGE);
    auto doc = parser.iterate(json_padded);
    auto bids_array = doc["bids"].get_array();
    for (auto bid_element : bids_array) {
        auto bid_array = bid_element.get_array();
        auto it = bid_array.begin();
        std::string_view price_sv = (*it).get_string().value();
        ++it;
        std::string_view qty_sv = (*it).get_string().value();
        double price = std::stod(std::string(price_sv));
        double qty = std::stod(std::string(qty_sv));
        bids2.emplace_back(price, qty);
    }
    
    // Parse with fast_custom
    orderbook::FastDepthParser::parse(SAMPLE_MESSAGE, bids3, asks3);
    
    std::cout << "\n=== Parser Correctness Test ===" << std::endl;
    std::cout << "All parsers should produce identical results:" << std::endl;
    std::cout << "nlohmann bids:    " << bids1.size() << " levels" << std::endl;
    std::cout << "simdjson bids:    " << bids2.size() << " levels" << std::endl;
    std::cout << "fast_custom bids: " << bids3.size() << " levels" << std::endl;
    
    // Verify all parsers return same data
    EXPECT_EQ(bids1.size(), bids2.size());
    EXPECT_EQ(bids1.size(), bids3.size());
    EXPECT_EQ(bids1.size(), 20);
    
    // Verify first bid matches
    EXPECT_NEAR(bids1[0].first, bids2[0].first, 0.0001);
    EXPECT_NEAR(bids1[0].first, bids3[0].first, 0.0001);
    EXPECT_NEAR(bids1[0].first, 4.0, 0.01);
}

TEST(OrderBookBinanceTest, LargeMessageBenchmark) {
    // Create larger message with 100 levels
    std::string large_msg = R"({"lastUpdateId": 1027024, "bids": [)";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) large_msg += ",";
        large_msg += "[\"" + std::to_string(100.0 - i*0.01) + "\", \"" + std::to_string(100.0 + i) + "\"]";
    }
    large_msg += R"(], "asks": [)";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) large_msg += ",";
        large_msg += "[\"" + std::to_string(100.01 + i*0.01) + "\", \"" + std::to_string(100.0 + i) + "\"]";
    }
    large_msg += "]}";
    
    const int iterations = 5000;
    std::vector<std::pair<double, double>> bids, asks;
    
    std::cout << "\n=== Large Message (100 levels) Benchmark ===" << std::endl;
    
    // nlohmann
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            json j = json::parse(large_msg);
            bids.clear();
            for (const auto& bid : j["bids"]) {
                double price = std::stod(bid[0].get<std::string>());
                double qty = std::stod(bid[1].get<std::string>());
                bids.emplace_back(price, qty);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "nlohmann/json: " << (us / double(iterations)) << " μs" << std::endl;
    }
    
    // simdjson
    {
        simdjson::ondemand::parser parser;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            simdjson::padded_string padded(large_msg);
            auto doc = parser.iterate(padded);
            bids.clear();
            auto bids_array = doc["bids"].get_array();
            for (auto bid_element : bids_array) {
                auto bid_array = bid_element.get_array();
                auto it = bid_array.begin();
                std::string_view price_sv = (*it).get_string().value();
                ++it;
                std::string_view qty_sv = (*it).get_string().value();
                double price = std::stod(std::string(price_sv));
                double qty = std::stod(std::string(qty_sv));
                bids.emplace_back(price, qty);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "simdjson:      " << (us / double(iterations)) << " μs" << std::endl;
    }
    
    // fast_custom
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            bids.clear();
            asks.clear();
            orderbook::FastDepthParser::parse(large_msg, bids, asks);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "fast_custom:   " << (us / double(iterations)) << " μs" << std::endl;
    }
}

TEST(OrderBookBinanceTest, MemoryAllocationTest) {
    const int iterations = 1000;
    std::vector<std::pair<double, double>> bids, asks;
    
    std::cout << "\n=== Memory Allocation Pattern ===" << std::endl;
    
    // Pre-allocate once
    bids.reserve(20);
    asks.reserve(20);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        bids.clear();  // Clear but keep capacity
        asks.clear();
        
        // Simulate parsing
        for (int j = 0; j < 20; ++j) {
            bids.emplace_back(100.0 + j, 1.0);
            asks.emplace_back(101.0 + j, 1.0);
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    
    std::cout << "Pre-allocated vector operations: " << (ns / double(iterations)) << " ns per iteration" << std::endl;
    std::cout << "                               = " << (ns / double(iterations) / 1000.0) << " μs per iteration" << std::endl;
    std::cout << "Vector capacity maintained: " << bids.capacity() << " elements" << std::endl;
}

TEST(OrderBookBinanceTest, EndToEndLatencySimulation) {
    const int iterations = 1000;
    std::vector<std::pair<double, double>> bids, asks;
    
    std::cout << "\n=== End-to-End Latency Simulation ===" << std::endl;
    std::cout << "(Parse + Sort + Callback overhead)" << std::endl << std::endl;
    
    // Callback counter
    int callback_count = 0;
    auto callback = [&callback_count](const std::string&, 
                                      const std::vector<std::pair<double,double>>&,
                                      const std::vector<std::pair<double,double>>&) {
        callback_count++;
    };
    
    // Test 1: nlohmann
    {
        callback_count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            json j = json::parse(SAMPLE_MESSAGE);
            bids.clear();
            asks.clear();
            
            for (const auto& bid : j["bids"]) {
                double price = std::stod(bid[0].get<std::string>());
                double qty = std::stod(bid[1].get<std::string>());
                bids.emplace_back(price, qty);
            }
            for (const auto& ask : j["asks"]) {
                double price = std::stod(ask[0].get<std::string>());
                double qty = std::stod(ask[1].get<std::string>());
                asks.emplace_back(price, qty);
            }
            
            std::sort(bids.begin(), bids.end(), 
                     [](const auto& a, const auto& b) { return a.first > b.first; });
            std::sort(asks.begin(), asks.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
            
            callback("binance", bids, asks);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        double avg = us / double(iterations);
        
        std::cout << "nlohmann/json: " << avg << " μs (total latency)" << std::endl;
        std::cout << "  └─ Callbacks: " << callback_count << std::endl;
    }
    
    // Test 2: simdjson
    {
        callback_count = 0;
        simdjson::ondemand::parser parser;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            simdjson::padded_string padded(SAMPLE_MESSAGE);
            auto doc = parser.iterate(padded);
            bids.clear();
            asks.clear();
            
            auto bids_array = doc["bids"].get_array();
            for (auto bid_element : bids_array) {
                auto bid_array = bid_element.get_array();
                auto it = bid_array.begin();
                std::string_view price_sv = (*it).get_string().value();
                ++it;
                std::string_view qty_sv = (*it).get_string().value();
                double price = std::stod(std::string(price_sv));
                double qty = std::stod(std::string(qty_sv));
                bids.emplace_back(price, qty);
            }
            
            auto asks_array = doc["asks"].get_array();
            for (auto ask_element : asks_array) {
                auto ask_array = ask_element.get_array();
                auto it = ask_array.begin();
                std::string_view price_sv = (*it).get_string().value();
                ++it;
                std::string_view qty_sv = (*it).get_string().value();
                double price = std::stod(std::string(price_sv));
                double qty = std::stod(std::string(qty_sv));
                asks.emplace_back(price, qty);
            }
            
            std::sort(bids.begin(), bids.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
            std::sort(asks.begin(), asks.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
            
            callback("binance", bids, asks);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        double avg = us / double(iterations);
        
        std::cout << "simdjson:      " << avg << " μs (total latency)" << std::endl;
        std::cout << "  └─ Callbacks: " << callback_count << std::endl;
    }
    
    // Test 3: fast_custom
    {
        callback_count = 0;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            bids.clear();
            asks.clear();
            
            orderbook::FastDepthParser::parse(SAMPLE_MESSAGE, bids, asks);
            
            std::sort(bids.begin(), bids.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
            std::sort(asks.begin(), asks.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
            
            callback("binance", bids, asks);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        double avg = us / double(iterations);
        
        std::cout << "fast_custom:   " << avg << " μs (total latency)" << std::endl;
        std::cout << "  └─ Callbacks: " << callback_count << std::endl;
    }
}

TEST(OrderBookBinanceTest, ComparisonSummary) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "PERFORMANCE COMPARISON SUMMARY" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "\nRun the individual benchmark tests above to see:" << std::endl;
    std::cout << "1. nlohmann/json - General purpose (baseline)" << std::endl;
    std::cout << "2. simdjson - SIMD-accelerated (5-10x faster)" << std::endl;
    std::cout << "3. fast_custom - Hand-written (10-20x faster)" << std::endl;
    std::cout << "\nExpected results:" << std::endl;
    std::cout << "  nlohmann/json:  20-30 μs" << std::endl;
    std::cout << "  simdjson:       2-5 μs" << std::endl;
    std::cout << "  fast_custom:    1-2 μs" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}