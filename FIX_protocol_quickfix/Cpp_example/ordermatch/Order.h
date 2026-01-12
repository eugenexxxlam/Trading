// ============================================================================
// ORDER - Trading Order Data Structure
// ============================================================================
// This file defines the Order class, which represents a single trading order
// in the order matching engine.
//
// PURPOSE:
// Encapsulates all information about a trading order including:
// - Identity (client ID, symbol)
// - Routing (owner, target)
// - Characteristics (side, type, price, quantity)
// - Execution state (open, filled, partially filled)
//
// ORDER LIFECYCLE:
// 1. NEW: Created with initial quantity
// 2. PARTIALLY_FILLED: Some quantity executed
// 3. FILLED: Completely executed (m_executedQuantity == m_quantity)
// 4. CANCELED: Canceled by user (m_openQuantity = 0, not filled)
// 5. CLOSED: Either FILLED or CANCELED (m_openQuantity == 0)
//
// KEY CONCEPTS:
// - OpenQuantity: Quantity still available for execution
// - ExecutedQuantity: Quantity already filled
// - AvgExecutedPrice: Weighted average price of all fills
// - LastExecutedPrice/Qty: Most recent fill details
//
// PRODUCTION ENHANCEMENTS:
// Real order objects would include:
// - Timestamps (creation, last update, expiration)
// - Time in force (Day, GTC, IOC, FOK)
// - Advanced order types (Iceberg, TWAP, VWAP)
// - Risk limits (max position, max order size)
// - Routing instructions
// - Clearing and settlement details
// ============================================================================

#ifndef ORDERMATCH_ORDER_H
#define ORDERMATCH_ORDER_H

#include <iomanip>
#include <ostream>
#include <string>

// ============================================================================
// ORDER CLASS
// ============================================================================
class Order {
  // Allow std::ostream to access private members for printing
  friend std::ostream &operator<<(std::ostream &, const Order &);

public:
  // ========================================================================
  // ORDER ENUMERATIONS
  // ========================================================================
  
  // Side: Buy or Sell
  // In real systems, might also include:
  // - SELL_SHORT: Borrow shares to sell
  // - SELL_SHORT_EXEMPT: Short sale exempt from uptick rule
  // - CROSS: Match buyer and seller from same firm
  enum Side {
    buy,
    sell
  };
  
  // Type: Order type determines pricing
  // This simple engine only supports LIMIT orders.
  // Production systems support:
  // - MARKET: Execute at best available price immediately
  // - STOP: Trigger at stop price, then execute as market
  // - STOP_LIMIT: Trigger at stop price, then execute as limit
  // - TRAILING_STOP: Stop price follows market by fixed amount
  // - PEG: Price pegged to reference (bid, ask, mid, etc.)
  enum Type {
    market,
    limit
  };

  // ========================================================================
  // CONSTRUCTOR
  // ========================================================================
  // Creates a new order with all required fields.
  //
  // PARAMETERS:
  // @param clientId - Unique ID from client's perspective (ClOrdID)
  // @param symbol - Trading instrument (e.g., "AAPL", "MSFT")
  // @param owner - Client who owns this order (SenderCompID from FIX)
  // @param target - Destination/broker (TargetCompID from FIX)
  // @param side - Buy or Sell
  // @param type - Order type (market, limit)
  // @param price - Limit price (ignored for market orders)
  // @param quantity - Total quantity to trade
  
  Order(
      const std::string &clientId,
      const std::string &symbol,
      const std::string &owner,
      const std::string &target,
      Side side,
      Type type,
      double price,
      long quantity)
      : m_clientId(clientId),
        m_symbol(symbol),
        m_owner(owner),
        m_target(target),
        m_side(side),
        m_type(type),
        m_price(price),
        m_quantity(quantity) {
    // Initialize execution tracking fields
    m_openQuantity = m_quantity;          // Initially all quantity is open
    m_executedQuantity = 0;                // No executions yet
    m_avgExecutedPrice = 0;                // No fills yet
    m_lastExecutedPrice = 0;               // No fills yet
    m_lastExecutedQuantity = 0;            // No fills yet
  }

  // ========================================================================
  // ACCESSOR METHODS - Order Properties
  // ========================================================================
  // These methods provide read-only access to order properties.
  // In C++, const methods guarantee they won't modify the object.
  
  const std::string &getClientID() const { return m_clientId; }
  const std::string &getSymbol() const { return m_symbol; }
  const std::string &getOwner() const { return m_owner; }
  const std::string &getTarget() const { return m_target; }
  Side getSide() const { return m_side; }
  Type getType() const { return m_type; }
  double getPrice() const { return m_price; }
  long getQuantity() const { return m_quantity; }

  // ========================================================================
  // ACCESSOR METHODS - Execution State
  // ========================================================================
  // These track how much has been executed and at what prices.
  
  // Quantity still available for execution
  // Formula: original quantity - executed quantity - canceled quantity
  long getOpenQuantity() const { return m_openQuantity; }
  
  // Total quantity that has been filled so far
  long getExecutedQuantity() const { return m_executedQuantity; }
  
  // Weighted average price of all fills
  // Example: Fill 100 @ $10, then 50 @ $11 = AvgPx of $10.33
  double getAvgExecutedPrice() const { return m_avgExecutedPrice; }
  
  // Price of the most recent fill (useful for market data)
  double getLastExecutedPrice() const { return m_lastExecutedPrice; }
  
  // Quantity of the most recent fill
  long getLastExecutedQuantity() const { return m_lastExecutedQuantity; }

  // ========================================================================
  // STATE QUERY METHODS
  // ========================================================================
  
  // Returns true if order is completely filled
  // Used to determine if order should be removed from book
  bool isFilled() const { return m_quantity == m_executedQuantity; }
  
  // Returns true if order is closed (either filled or canceled)
  // Closed orders should be removed from active order book
  bool isClosed() const { return m_openQuantity == 0; }

  // ========================================================================
  // EXECUTION RECORDING
  // ========================================================================
  // Called when order executes (matches with contra-side order).
  //
  // PARAMETERS:
  // @param price - Execution price for this fill
  // @param quantity - Quantity filled in this execution
  //
  // SIDE EFFECTS:
  // - Decreases m_openQuantity
  // - Increases m_executedQuantity
  // - Updates m_avgExecutedPrice (weighted average)
  // - Records m_lastExecutedPrice and m_lastExecutedQuantity
  //
  // AVERAGE PRICE CALCULATION:
  // Uses weighted average formula:
  // NewAvg = (NewQty * NewPx + OldAvg * OldQty) / (NewQty + OldQty)
  //
  // Example:
  //   First fill:  100 @ $10.00  => AvgPx = $10.00
  //   Second fill:  50 @ $10.50  => AvgPx = $10.16
  //   Calculation: ((50 * 10.50) + (10.00 * 100)) / (50 + 100) = $10.16
  
  void execute(double price, long quantity) {
    // Calculate new weighted average price
    m_avgExecutedPrice
        = ((quantity * price) + (m_avgExecutedPrice * m_executedQuantity)) / (quantity + m_executedQuantity);

    // Update quantities
    m_openQuantity -= quantity;          // Less quantity available
    m_executedQuantity += quantity;      // More quantity filled

    // Record this execution for reporting
    m_lastExecutedPrice = price;
    m_lastExecutedQuantity = quantity;
  }
  
  // ========================================================================
  // CANCELLATION
  // ========================================================================
  // Cancels any remaining open quantity.
  // Order status becomes CANCELED if no quantity was executed,
  // or PARTIALLY_FILLED if some quantity was executed.
  //
  // NOTE: Does not decrease m_quantity (original order size)
  // Just sets m_openQuantity to 0 so it won't match anymore.
  
  void cancel() { m_openQuantity = 0; }

private:
  // ========================================================================
  // MEMBER VARIABLES - Order Identity
  // ========================================================================
  std::string m_clientId;    // Client's order ID (ClOrdID)
  std::string m_symbol;      // Trading instrument
  std::string m_owner;       // Client/firm who owns order
  std::string m_target;      // Destination/broker

  // ========================================================================
  // MEMBER VARIABLES - Order Characteristics
  // ========================================================================
  Side m_side;              // Buy or Sell
  Type m_type;              // Market or Limit
  double m_price;           // Limit price (0 for market orders)
  long m_quantity;          // Original order size

  // ========================================================================
  // MEMBER VARIABLES - Execution Tracking
  // ========================================================================
  long m_openQuantity;              // Quantity still available for execution
  long m_executedQuantity;          // Total quantity filled so far
  double m_avgExecutedPrice;        // Weighted average fill price
  double m_lastExecutedPrice;       // Price of most recent fill
  long m_lastExecutedQuantity;      // Quantity of most recent fill
};

// ============================================================================
// OUTPUT OPERATOR - For Debugging/Display
// ============================================================================
// Allows printing order details using std::cout << order;
// Displays key fields in readable format for order book visualization.
inline std::ostream &operator<<(std::ostream &ostream, const Order &order) {
  return ostream << "ID: " << std::setw(10) << "," << order.getClientID() << " OWNER: " << std::setw(10) << ","
                 << order.getOwner() << " PRICE: " << std::setw(10) << "," << order.getPrice()
                 << " QUANTITY: " << std::setw(10) << "," << order.getQuantity();
}

#endif
