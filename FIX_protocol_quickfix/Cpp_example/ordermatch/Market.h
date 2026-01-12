// ============================================================================
// MARKET - Order Book for a Single Trading Instrument
// ============================================================================
// This file defines the Market class, which maintains buy and sell order
// books for a single trading symbol and performs order matching.
//
// PURPOSE:
// A "Market" represents the order book for one trading instrument (e.g., AAPL).
// It maintains two separate books:
// - BID book: Buy orders (sorted by price descending - highest first)
// - ASK book: Sell orders (sorted by price ascending - lowest first)
//
// MATCHING ALGORITHM:
// Uses price-time priority matching:
// 1. Best price has priority (highest bid, lowest ask)
// 2. Among same price, earlier orders fill first (FIFO)
// 3. When bid price >= ask price, orders can match
// 4. Match at ask price (benefit to passive/resting order)
//
// ORDER BOOK STRUCTURE:
// BID BOOK (Buy orders, descending):
//   $100.50 - 1000 shares  <- Best bid (top of book)
//   $100.00 - 500 shares
//   $99.50  - 2000 shares
//
// ASK BOOK (Sell orders, ascending):
//   $100.75 - 500 shares   <- Best ask (top of book)
//   $101.00 - 1000 shares
//   $101.25 - 800 shares
//
// SPREAD: $100.75 - $100.50 = $0.25 (no match possible yet)
//
// PRODUCTION CONSIDERATIONS:
// Real order books need:
// - Hidden orders (iceberg/reserve orders)
// - Order priorities (market makers, institutional)
// - Multiple matching algorithms (pro-rata, time-priority, size-priority)
// - Order book snapshots and incremental updates
// - Market data publishing
// ============================================================================

#ifndef ORDERMATCH_MARKET_H
#define ORDERMATCH_MARKET_H

#include "Order.h"
#include <functional>
#include <map>
#include <queue>
#include <string>

class Market {
public:
  // ========================================================================
  // ORDER LIFECYCLE OPERATIONS
  // ========================================================================
  
  // Insert a new order into the appropriate book (bid or ask)
  // Returns true if successful
  // In production, might return false if:
  // - Duplicate order ID
  // - Invalid price/quantity
  // - Risk limits exceeded
  bool insert(const Order &order);
  
  // Remove an order from the book (for cancellations)
  // Searches for order by client ID and removes it
  void erase(const Order &order);
  
  // Find an order in the book by side and client ID
  // Throws exception if not found
  // Used for cancel and replace operations
  Order &find(Order::Side side, std::string id);
  
  // ========================================================================
  // ORDER MATCHING
  // ========================================================================
  // Attempt to match bid and ask orders.
  // - Checks if best bid >= best ask
  // - If so, executes trade at ask price
  // - Partially or fully fills both orders
  // - Removes completely filled orders
  // - Continues until no more matches possible
  //
  // @param orders - Queue to receive orders that were updated
  // @return true if any matches occurred
  //
  // MATCHING PRICE LOGIC:
  // Aggressor (new order) pays the price of the resting order.
  // If bid $100.50 hits ask $100.25, trade executes at $100.25.
  // This rewards passive liquidity providers.
  bool match(std::queue<Order> &);
  
  // ========================================================================
  // DISPLAY/DEBUGGING
  // ========================================================================
  // Print current state of bid and ask books to console
  void display() const;

private:
  // ========================================================================
  // ORDER BOOK TYPE DEFINITIONS
  // ========================================================================
  
  // BidOrders: Multimap sorted by price DESCENDING (highest bid first)
  // Key: price (double)
  // Value: Order
  // std::greater<double> sorts in descending order
  // Multimap allows multiple orders at same price (FIFO within price level)
  typedef std::multimap<double, Order, std::greater<double>> BidOrders;
  
  // AskOrders: Multimap sorted by price ASCENDING (lowest ask first)
  // Key: price (double)
  // Value: Order
  // std::less<double> sorts in ascending order (default)
  typedef std::multimap<double, Order, std::less<double>> AskOrders;

  // ========================================================================
  // INTERNAL MATCHING LOGIC
  // ========================================================================
  // Execute a match between a bid and ask order.
  // - Determines execution price (ask order's price)
  // - Determines execution quantity (min of both open quantities)
  // - Calls execute() on both orders
  //
  // EXECUTION PRICE LOGIC:
  // Always use ask (resting) order's price.
  // This is standard in most markets to reward passive orders.
  //
  // EXECUTION QUANTITY LOGIC:
  // Match the smaller of:
  // - Bid order's open quantity
  // - Ask order's open quantity
  // One order will be completely filled, the other may be partial.
  void match(Order &bid, Order &ask);

  // ========================================================================
  // MEMBER VARIABLES
  // ========================================================================
  
  // Queue of orders that were updated (filled/partially filled)
  // Used to generate ExecutionReports back to clients
  std::queue<Order> m_orderUpdates;
  
  // Bid book: All buy orders sorted by price (highest first)
  // Example: {100.50: Order1, 100.50: Order2, 100.00: Order3}
  BidOrders m_bidOrders;
  
  // Ask book: All sell orders sorted by price (lowest first)
  // Example: {100.75: Order1, 101.00: Order2, 101.25: Order3}
  AskOrders m_askOrders;
};

#endif
