// ============================================================================
// ORDER MATCHER - Multi-Symbol Order Book Manager
// ============================================================================
// This file defines the OrderMatcher class, which manages order books
// (Market objects) for multiple trading symbols.
//
// PURPOSE:
// While Market handles a single symbol's order book, OrderMatcher manages
// all symbols traded in the system. It routes orders to the correct Market
// based on the symbol and coordinates matching across all markets.
//
// ARCHITECTURE:
// OrderMatcher is a container/router:
// - Maintains map of symbol -> Market
// - Routes orders to appropriate Market
// - Creates new Markets on-demand for new symbols
// - Provides aggregate operations across all markets
//
// EXAMPLE USAGE:
//   OrderMatcher matcher;
//   
//   // Insert orders for different symbols
//   Order aaplBuy("ID1", "AAPL", ...);
//   Order msftBuy("ID2", "MSFT", ...);
//   
//   matcher.insert(aaplBuy);  // Routes to AAPL market (creates if needed)
//   matcher.insert(msftBuy);  // Routes to MSFT market (creates if needed)
//   
//   // Match all markets
//   std::queue<Order> fills;
//   matcher.match(fills);     // Matches across all symbols
//
// PRODUCTION CONSIDERATIONS:
// Real order matchers need:
// - Symbol validation (valid instruments only)
// - Trading sessions (pre-market, regular, after-hours)
// - Circuit breakers (halt trading on volatility)
// - Cross-symbol constraints (spread orders, pairs trading)
// - Market data publishing
// - Audit logging
// ============================================================================

#ifndef ORDERMATCH_ORDERMATCHER_H
#define ORDERMATCH_ORDERMATCHER_H

#include "Market.h"
#include <iostream>
#include <map>

class OrderMatcher {
  // Map: symbol (string) -> Market (order book)
  // Each trading instrument has its own Market
  typedef std::map<std::string, Market> Markets;

public:
  // ========================================================================
  // INSERT - Add Order to Appropriate Market
  // ========================================================================
  // Routes an order to the correct Market based on symbol.
  // If Market doesn't exist for this symbol, creates it automatically.
  //
  // FLOW:
  // 1. Extract symbol from order
  // 2. Look up Market for that symbol
  // 3. If Market doesn't exist, create new Market
  // 4. Insert order into Market
  //
  // AUTO-CREATION:
  // New Markets are created on-demand when first order arrives.
  // In production, you might:
  // - Pre-create Markets for known symbols at startup
  // - Validate symbol against reference data
  // - Reject orders for unknown/delisted symbols
  // - Load historical orders from database
  //
  // @param order - Order to insert
  // @return true if successful
  bool insert(const Order &order) {
    // Find Market for this symbol
    Markets::iterator i = m_markets.find(order.getSymbol());
    
    // If Market doesn't exist, create it
    if (i == m_markets.end()) {
      i = m_markets.insert(std::make_pair(order.getSymbol(), Market())).first;
    }
    
    // Insert order into the Market
    return i->second.insert(order);
  }

  // ========================================================================
  // ERASE - Remove Order from Market
  // ========================================================================
  // Removes an order from its Market (for cancellations).
  //
  // FLOW:
  // 1. Find Market for order's symbol
  // 2. If Market exists, remove order from it
  // 3. If Market doesn't exist, silently ignore (order not in book)
  //
  // NOTE: Does not remove empty Markets
  // In production, might want to clean up empty Markets to save memory
  //
  // @param order - Order to remove
  void erase(const Order &order) {
    Markets::iterator i = m_markets.find(order.getSymbol());
    if (i == m_markets.end()) {
      return;  // Market doesn't exist, order not in book
    }
    i->second.erase(order);
  }

  // ========================================================================
  // FIND - Locate Order in Market
  // ========================================================================
  // Finds an order by symbol, side, and client ID.
  // Used for cancel and replace operations.
  //
  // @param symbol - Trading instrument
  // @param side - Buy or Sell
  // @param id - Client Order ID
  // @return Reference to the order
  // @throws std::exception if Market or order not found
  Order &find(std::string symbol, Order::Side side, std::string id) {
    Markets::iterator i = m_markets.find(symbol);
    if (i == m_markets.end()) {
      throw std::exception();  // No market for this symbol
    }
    return i->second.find(side, id);
  }

  // ========================================================================
  // MATCH - Match Orders in Specific Market
  // ========================================================================
  // Attempts to match orders for a single symbol.
  //
  // @param symbol - Symbol to match
  // @param orders - Output queue of filled orders
  // @return true if any matches occurred
  bool match(std::string symbol, std::queue<Order> &orders) {
    Markets::iterator i = m_markets.find(symbol);
    if (i == m_markets.end()) {
      return false;  // No market for this symbol
    }
    return i->second.match(orders);
  }

  // ========================================================================
  // MATCH - Match Orders Across All Markets
  // ========================================================================
  // Iterates through all Markets and attempts matching.
  // This is typically called periodically or after new orders arrive.
  //
  // MATCHING STRATEGY:
  // Sequential matching - each market matched independently.
  // No cross-symbol logic (e.g., no spread orders, pairs trading).
  //
  // PRODUCTION ENHANCEMENTS:
  // - Prioritize markets by volume/activity
  // - Parallel matching using thread pool
  // - Fair queueing to prevent starvation
  // - Rate limiting per market
  //
  // @param orders - Output queue of all filled orders across all markets
  // @return true if any matches occurred in any market
  bool match(std::queue<Order> &orders) {
    Markets::iterator i;
    // Match each market independently
    for (i = m_markets.begin(); i != m_markets.end(); ++i) {
      i->second.match(orders);
    }
    return orders.size() != 0;
  }

  // ========================================================================
  // DISPLAY - Show Order Book for Specific Market
  // ========================================================================
  // Displays the order book (bids and asks) for a single symbol.
  //
  // @param symbol - Symbol to display
  void display(std::string symbol) const {
    Markets::const_iterator i = m_markets.find(symbol);
    if (i == m_markets.end()) {
      return;  // No market for this symbol
    }
    i->second.display();
  }

  // ========================================================================
  // DISPLAY - List All Trading Symbols
  // ========================================================================
  // Displays all symbols that have active markets (order books).
  // Useful for seeing what's currently trading.
  //
  // FORMAT:
  // SYMBOLS:
  // --------
  // AAPL
  // GOOGL
  // MSFT
  void display() const {
    std::cout << "SYMBOLS:" << std::endl;
    std::cout << "--------" << std::endl;

    Markets::const_iterator i;
    for (i = m_markets.begin(); i != m_markets.end(); ++i) {
      std::cout << i->first << std::endl;
    }
  }

private:
  // Map of all markets: symbol -> Market
  // Example:
  // {
  //   "AAPL":  Market(bid book, ask book),
  //   "GOOGL": Market(bid book, ask book),
  //   "MSFT":  Market(bid book, ask book)
  // }
  Markets m_markets;
};

#endif
