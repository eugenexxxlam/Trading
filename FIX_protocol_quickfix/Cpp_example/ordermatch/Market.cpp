// ============================================================================
// MARKET IMPLEMENTATION - Order Book Matching Logic
// ============================================================================
// This file implements the core order matching algorithm for a single
// trading instrument. This is the heart of any trading system.
//
// MATCHING ALGORITHM SUMMARY:
// 1. Maintain separate books for bids (buys) and asks (sells)
// 2. Sort bids by price descending (best bid = highest price first)
// 3. Sort asks by price ascending (best ask = lowest price first)
// 4. When bid price >= ask price, execute trade at ask price
// 5. Fill min(bid quantity, ask quantity)
// 6. Remove completely filled orders, keep partial fills in book
// 7. Repeat until bid price < ask price (no more matches)
//
// EXAMPLE MATCHING SEQUENCE:
// Initial state:
//   BID: $100.50 (1000 shares)
//   ASK: $100.25 (500 shares)
//
// Match occurs (bid >= ask):
//   Execute: 500 shares @ $100.25
//   Result:  BID becomes $100.50 (500 shares remaining)
//            ASK removed (completely filled)
//
// PERFORMANCE CHARACTERISTICS:
// - insert():  O(log n) - std::multimap insertion
// - erase():   O(n) - linear search by ID, then O(log n) erase
// - match():   O(k * log n) where k = number of matches
// - find():    O(n) - linear search by ID
//
// For high-frequency trading, use:
// - Hash maps for O(1) order lookup by ID
// - Custom sorted containers with order pointers
// - Lock-free data structures
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4786)
#endif

#include "Market.h"
#include <iostream>

// ============================================================================
// INSERT - Add Order to Book
// ============================================================================
// Inserts a new order into the appropriate book based on side.
// Orders are automatically sorted by price due to std::multimap.
//
// BID ORDERS: Inserted into m_bidOrders, sorted descending by price
// ASK ORDERS: Inserted into m_askOrders, sorted ascending by price
//
// RETURNS: Always true in this implementation
// In production, might return false if:
// - Order fails validation (invalid price, quantity)
// - Risk limits exceeded (max position, max order size)
// - Duplicate order ID
// - Market halted or not open
bool Market::insert(const Order &order) {
  if (order.getSide() == Order::buy) {
    // Insert buy order into bid book
    // std::greater<double> comparator ensures highest bids first
    m_bidOrders.insert(BidOrders::value_type(order.getPrice(), order));
  } else {
    // Insert sell order into ask book
    // std::less<double> comparator ensures lowest asks first
    m_askOrders.insert(AskOrders::value_type(order.getPrice(), order));
  }
  return true;
}

// ============================================================================
// ERASE - Remove Order from Book
// ============================================================================
// Removes an order from the book (used for cancellations).
// Must search for order by client ID since we don't have iterator reference.
//
// PERFORMANCE NOTE:
// This is O(n) linear search - not efficient for large order books!
// Production systems maintain hash map: order ID -> iterator for O(1) lookup.
//
// PRODUCTION OPTIMIZATION:
//   std::unordered_map<std::string, BidOrders::iterator> m_bidOrdersByID;
//   std::unordered_map<std::string, AskOrders::iterator> m_askOrdersByID;
//
//   // In insert():
//   auto it = m_bidOrders.insert({price, order});
//   m_bidOrdersByID[order.getClientID()] = it;
//
//   // In erase():
//   auto it = m_bidOrdersByID.find(order.getClientID());
//   m_bidOrders.erase(it->second);
void Market::erase(const Order &order) {
  std::string id = order.getClientID();
  
  if (order.getSide() == Order::buy) {
    // Search bid book for matching client ID
    BidOrders::iterator i;
    for (i = m_bidOrders.begin(); i != m_bidOrders.end(); ++i) {
      if (i->second.getClientID() == id) {
        m_bidOrders.erase(i);
        return;
      }
    }
  } else if (order.getSide() == Order::sell) {
    // Search ask book for matching client ID
    AskOrders::iterator i;
    for (i = m_askOrders.begin(); i != m_askOrders.end(); ++i) {
      if (i->second.getClientID() == id) {
        m_askOrders.erase(i);
        return;
      }
    }
  }
}

// ============================================================================
// MATCH - Execute Matching Orders
// ============================================================================
// Core matching algorithm. Repeatedly matches best bid with best ask
// until no more matches are possible.
//
// MATCHING CONDITION:
// A match occurs when: best_bid_price >= best_ask_price
//
// MATCHING PRICE:
// Always execute at the ask (resting) order's price.
// This rewards passive liquidity providers.
//
// MATCHING QUANTITY:
// Execute min(bid_open_qty, ask_open_qty)
// One or both orders may be completely filled.
//
// ALGORITHM:
// 1. Check if both books have orders
// 2. Get best bid (highest price) and best ask (lowest price)
// 3. If bid price >= ask price:
//    a. Execute trade (call match())
//    b. Add both orders to update queue (for ExecutionReports)
//    c. Remove completely filled orders from books
// 4. Repeat until bid price < ask price
//
// @param orders - Output queue of orders that were updated
// @return true if any matches occurred, false if no matches
bool Market::match(std::queue<Order> &orders) {
  while (true) {
    // Check if we have orders to match
    if (!m_bidOrders.size() || !m_askOrders.size()) {
      return orders.size() != 0;  // Return if any matches occurred
    }

    // Get best bid and best ask (top of each book)
    BidOrders::iterator iBid = m_bidOrders.begin();  // Highest bid
    AskOrders::iterator iAsk = m_askOrders.begin();  // Lowest ask

    // Check if orders can match (bid price >= ask price)
    if (iBid->second.getPrice() >= iAsk->second.getPrice()) {
      // MATCH FOUND! Execute the trade
      
      // Get references to the orders (must match by reference to update state)
      Order &bid = iBid->second;
      Order &ask = iAsk->second;

      // Execute the match (fills both orders, updates quantities/prices)
      match(bid, ask);
      
      // Add both orders to update queue
      // These will be sent as ExecutionReports to clients
      orders.push(bid);
      orders.push(ask);

      // Remove completely filled orders from books
      if (bid.isClosed()) {
        m_bidOrders.erase(iBid);
      }
      if (ask.isClosed()) {
        m_askOrders.erase(iAsk);
      }
    } else {
      // Spread exists (bid < ask), no more matches possible
      // Example: Best bid $100.00, Best ask $100.25 = $0.25 spread
      return orders.size() != 0;
    }
  }
}

// ============================================================================
// FIND - Locate Order by ID
// ============================================================================
// Searches for an order by side and client ID.
// Used for cancel and replace operations.
//
// THROWS: std::exception() if order not found
// In production, use custom exception with error details.
//
// PERFORMANCE: O(n) linear search
// Production optimization: use hash map for O(1) lookup
Order &Market::find(Order::Side side, std::string id) {
  if (side == Order::buy) {
    // Search bid book
    BidOrders::iterator i;
    for (i = m_bidOrders.begin(); i != m_bidOrders.end(); ++i) {
      if (i->second.getClientID() == id) {
        return i->second;
      }
    }
  } else if (side == Order::sell) {
    // Search ask book
    AskOrders::iterator i;
    for (i = m_askOrders.begin(); i != m_askOrders.end(); ++i) {
      if (i->second.getClientID() == id) {
        return i->second;
      }
    }
  }
  // Order not found
  throw std::exception();
}

// ============================================================================
// MATCH (INTERNAL) - Execute Trade Between Two Orders
// ============================================================================
// Executes a trade between a bid and ask order.
//
// EXECUTION PRICE LOGIC:
// Always use ask order's price (rewards passive order).
// Example:
//   BID: $100.50, ASK: $100.25 => Execute at $100.25
//   BID: $100.00, ASK: $100.50 => Execute at $100.50
//
// EXECUTION QUANTITY LOGIC:
// Match the minimum of both open quantities:
//   BID: 1000 shares, ASK: 600 shares => Execute 600
//   BID: 500 shares,  ASK: 800 shares => Execute 500
//
// SIDE EFFECTS:
// Both orders have their execute() method called, which:
// - Decreases open quantity
// - Increases executed quantity
// - Updates average executed price
// - Records last executed price/quantity
void Market::match(Order &bid, Order &ask) {
  // Execution price = ask (resting) order's price
  double price = ask.getPrice();
  
  // Execution quantity = min of both open quantities
  long quantity = 0;
  if (bid.getOpenQuantity() > ask.getOpenQuantity()) {
    quantity = ask.getOpenQuantity();  // Ask fills completely, bid partial
  } else {
    quantity = bid.getOpenQuantity();  // Bid fills completely, ask partial (or both full)
  }

  // Execute both sides of the trade
  bid.execute(price, quantity);
  ask.execute(price, quantity);
}

// ============================================================================
// DISPLAY - Print Order Book State
// ============================================================================
// Displays current state of bid and ask books to console.
// Useful for debugging and visualization during development.
//
// FORMAT:
// BIDS:
// -----
// ID: ..., OWNER: ..., PRICE: $100.50, QUANTITY: 1000
// ID: ..., OWNER: ..., PRICE: $100.00, QUANTITY: 500
//
// ASKS:
// -----
// ID: ..., OWNER: ..., PRICE: $100.75, QUANTITY: 800
// ID: ..., OWNER: ..., PRICE: $101.00, QUANTITY: 1000
//
// In production:
// - Publish order book snapshots via market data feed
// - Support incremental updates (add/modify/delete)
// - Provide aggregated depth (sum quantities at each price level)
// - Offer different depth levels (top 5, top 10, full book)
void Market::display() const {
  BidOrders::const_iterator iBid;
  AskOrders::const_iterator iAsk;

  std::cout << "BIDS:" << std::endl;
  std::cout << "-----" << std::endl << std::endl;
  for (iBid = m_bidOrders.begin(); iBid != m_bidOrders.end(); ++iBid) {
    std::cout << iBid->second << std::endl;
  }

  std::cout << std::endl << std::endl;

  std::cout << "ASKS:" << std::endl;
  std::cout << "-----" << std::endl << std::endl;
  for (iAsk = m_askOrders.begin(); iAsk != m_askOrders.end(); ++iAsk) {
    std::cout << iAsk->second << std::endl;
  }
}
