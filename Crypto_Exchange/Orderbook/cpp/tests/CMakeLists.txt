add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/..)
enable_testing()

include_directories(${GTEST_INCLUDE_DIRS})

set(TEST_SOURCES test_orderbook_binance.cpp)

add_executable(orderbook_tests ${TEST_SOURCES})
target_link_libraries(orderbook_tests gtest gtest_main)

add_test(NAME orderbook_tests COMMAND orderbook_tests)