cmake_minimum_required(VERSION 3.20)
project(OrderBookFeeds CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")

# Dependencies
find_package(Boost REQUIRED COMPONENTS system iostreams)
find_package(OpenSSL REQUIRED)
find_package(simdjson REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Binance Executable
add_executable(binance_orderbook 1_orderbook_binance.cpp)
target_link_libraries(binance_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    Threads::Threads
)
target_compile_options(binance_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET binance_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# OKX Executable
add_executable(okx_orderbook 2_orderbook_okx.cpp)
target_link_libraries(okx_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    Threads::Threads
)
target_compile_options(okx_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET okx_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Bybit Executable
add_executable(bybit_orderbook 3_orderbook_bybit.cpp)
target_link_libraries(bybit_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    Threads::Threads
)
target_compile_options(bybit_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET bybit_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Gate.io Executable
add_executable(gate_orderbook 4_orderbook_gate.cpp)
target_link_libraries(gate_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(gate_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET gate_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Bitget Executable
add_executable(bitget_orderbook 5_orderbook_bitget.cpp)
target_link_libraries(bitget_orderbook PRIVATE
    Boost::system
    Boost::iostreams
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    z
)
target_compile_options(bitget_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET bitget_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# BingX Executable
add_executable(bingx_orderbook 6_orderbook_bingx.cpp)
target_link_libraries(bingx_orderbook PRIVATE
    Boost::system
    Boost::iostreams
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    z
)
target_compile_options(bingx_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET bingx_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Bitfinex Executable
add_executable(bitfinex_orderbook 7_orderbook_bitfinex.cpp)
target_link_libraries(bitfinex_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(bitfinex_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET bitfinex_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# HTX Executable
add_executable(htx_orderbook 8_orderbook_HTX.cpp)
target_link_libraries(htx_orderbook PRIVATE
    Boost::system
    Boost::iostreams
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    z
)
target_compile_options(htx_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET htx_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Hyperliquid Executable
add_executable(hyperliquid_orderbook 9_orderbook_hyperliquid.cpp)
target_link_libraries(hyperliquid_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(hyperliquid_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET hyperliquid_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# dYdX Executable
add_executable(dydx_orderbook 10_orderbook_dydx.cpp)
target_link_libraries(dydx_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(dydx_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET dydx_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Coinbase Executable
add_executable(coinbase_orderbook 11_orderbook_coinbase.cpp)
target_link_libraries(coinbase_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(coinbase_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET coinbase_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Kraken Executable
add_executable(kraken_orderbook 12_orderbook_kraken.cpp)
target_link_libraries(kraken_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(kraken_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET kraken_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Crypto.com Executable
add_executable(cryptocom_orderbook 13_orderbook_cryptocom.cpp)
target_link_libraries(cryptocom_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(cryptocom_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET cryptocom_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# KuCoin Executable
add_executable(kucoin_orderbook 14_orderbook_kucoin.cpp)
target_link_libraries(kucoin_orderbook PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
target_compile_options(kucoin_orderbook PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET kucoin_orderbook PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Library for orderbook (no main)
add_library(orderbook_lib OBJECT 1_orderbook_binance.cpp)
target_link_libraries(orderbook_lib PUBLIC
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    Threads::Threads
)
target_compile_options(orderbook_lib PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
target_compile_definitions(orderbook_lib PRIVATE NO_MAIN)

# Benchmark executable (shares implementation with main)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark_latency.cpp")
    add_executable(benchmark_latency benchmark_latency.cpp)
    target_link_libraries(benchmark_latency PRIVATE 
        orderbook_lib
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads
    )
    target_compile_options(benchmark_latency PRIVATE
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
    )
    set_property(TARGET benchmark_latency PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "Skipping benchmark_latency: benchmark_latency.cpp not found")
endif()

# CLOB Aggregator - 3 Exchanges (Binance, Coinbase, OKX)
# Define NO_MAIN to exclude standalone main() from exchange implementations
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/clob_aggregator_main.cpp")
    add_executable(clob_aggregator
        clob_aggregator_main.cpp
        0_CLOB_MM.cpp
        1_orderbook_binance.cpp
        2_orderbook_okx.cpp
        11_orderbook_coinbase.cpp
    )
    target_compile_definitions(clob_aggregator PRIVATE NO_MAIN)
    target_link_libraries(clob_aggregator PRIVATE
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads
    )
    target_compile_options(clob_aggregator PRIVATE
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
    )
    set_property(TARGET clob_aggregator PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "Skipping clob_aggregator: clob_aggregator_main.cpp not found")
endif()

# CLOB Aggregator - 12 Exchanges (Production version matching Python)
add_executable(clob_aggregator_12x
    clob_aggregator_12x.cpp
    0_CLOB_MM.cpp
    1_orderbook_binance.cpp
    2_orderbook_okx.cpp
    3_orderbook_bybit.cpp
    4_orderbook_gate.cpp
    5_orderbook_bitget.cpp
    6_orderbook_bingx.cpp
    7_orderbook_bitfinex.cpp
    8_orderbook_HTX.cpp
    9_orderbook_hyperliquid.cpp
    10_orderbook_dydx.cpp
    11_orderbook_coinbase.cpp
    12_orderbook_kraken.cpp
    13_orderbook_cryptocom.cpp
    14_orderbook_kucoin.cpp
)
target_compile_definitions(clob_aggregator_12x PRIVATE NO_MAIN)
target_link_libraries(clob_aggregator_12x PRIVATE
    Boost::system
    Boost::iostreams
    OpenSSL::SSL
    OpenSSL::Crypto
    simdjson::simdjson
    Threads::Threads
    ZLIB::ZLIB
)
target_compile_options(clob_aggregator_12x PRIVATE
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Release>:-funroll-loops>
)
set_property(TARGET clob_aggregator_12x PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

# Lock-Free CLOB Aggregator (Short version)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LFQ_CLOB_short.cpp")
    add_executable(lfq_clob
        LFQ_CLOB_short.cpp
        0_CLOB_MM.cpp
        1_orderbook_binance.cpp
        2_orderbook_okx.cpp
        3_orderbook_bybit.cpp
        4_orderbook_gate.cpp
        5_orderbook_bitget.cpp
        6_orderbook_bingx.cpp
        7_orderbook_bitfinex.cpp
        8_orderbook_HTX.cpp
        9_orderbook_hyperliquid.cpp
        10_orderbook_dydx.cpp
        11_orderbook_coinbase.cpp
        12_orderbook_kraken.cpp
        13_orderbook_cryptocom.cpp
        14_orderbook_kucoin.cpp
    )
    target_compile_definitions(lfq_clob PRIVATE NO_MAIN)
    target_link_libraries(lfq_clob PRIVATE
        Boost::system
        Boost::iostreams
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads
        ZLIB::ZLIB
    )
    target_compile_options(lfq_clob PRIVATE
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
    )
    set_property(TARGET lfq_clob PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "Skipping lfq_clob: LFQ_CLOB_short.cpp not found")
endif()

# CLOB Aggregator Test (Mock data - for testing aggregation logic only)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_clob_aggregator.cpp")
    add_executable(test_clob_aggregator 
        test_clob_aggregator.cpp
        0_CLOB_MM.cpp
    )
    target_link_libraries(test_clob_aggregator PRIVATE
        Boost::system
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads
    )
    target_compile_options(test_clob_aggregator PRIVATE
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-funroll-loops>
    )
    set_property(TARGET test_clob_aggregator PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "Skipping test_clob_aggregator: test_clob_aggregator.cpp not found")
endif()

install(TARGETS binance_orderbook DESTINATION bin)
