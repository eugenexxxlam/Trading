// ============================================================================
// ORDERMATCH APPLICATION HEADER
// ============================================================================
// This file defines the Application class for a FIX Protocol Order Matching
// Engine - a real (simplified) trading exchange simulator.
//
// PURPOSE:
// Unlike the Executor (which immediately fills all orders), this application
// maintains actual order books and matches buy orders with sell orders based
// on price-time priority, just like a real stock exchange.
//
// KEY DIFFERENCES FROM EXECUTOR:
// - EXECUTOR: Immediately fills all orders (straight-through processing)
// - ORDERMATCH: Maintains order books and matches contra-side orders
//
// WORKFLOW:
// 1. Receive NewOrderSingle from client
// 2. Validate order (only limit orders, only DAY time in force)
// 3. Convert FIX message to internal Order object
// 4. Insert order into matching engine (OrderMatcher)
// 5. Send ExecutionReport with NEW status
// 6. Attempt to match orders
// 7. For each fill, send ExecutionReport with FILLED/PARTIALLY_FILLED status
//
// ORDER BOOK EXAMPLE:
// AAPL Bids (Buy):          AAPL Asks (Sell):
// $100.50 - 1000 shares     $100.75 - 500 shares
// $100.00 - 500 shares      $101.00 - 1000 shares
//
// When new sell order at $100.25 arrives:
// - Matches with $100.50 bid
// - Executes at $100.50 (resting order's price)
// - Both sides get ExecutionReports
//
// SUPPORTED FEATURES:
// - NewOrderSingle (FIX 4.2)
// - OrderCancelRequest (FIX 4.2)
// - MarketDataRequest (FIX 4.2 and 4.3)
// - Price-time priority matching
// - Partial fills
// - Order cancellation
//
// PRODUCTION ENHANCEMENTS NEEDED:
// - Support all order types (market, stop, stop-limit)
// - Support all time in force (GTC, IOC, FOK)
// - Support order modification
// - Risk management and circuit breakers
// - Market data dissemination
// - Post-trade clearing and settlement
// ============================================================================

#ifndef ORDERMATCH_APPLICATION_H
#define ORDERMATCH_APPLICATION_H

#include "IDGenerator.h"
#include "Order.h"
#include "OrderMatcher.h"
#include <iostream>
#include <queue>

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Mutex.h"
#include "quickfix/Utility.h"
#include "quickfix/Values.h"

#include "quickfix/fix42/MarketDataRequest.h"
#include "quickfix/fix42/NewOrderSingle.h"
#include "quickfix/fix42/OrderCancelRequest.h"
#include "quickfix/fix43/MarketDataRequest.h"

// ============================================================================
// ORDERMATCH APPLICATION CLASS
// ============================================================================
class Application : public FIX::Application, public FIX::MessageCracker {
  // ========================================================================
  // LIFECYCLE CALLBACKS - Empty implementations
  // ========================================================================
  void onCreate(const FIX::SessionID &) {}        // Session created
  void onLogon(const FIX::SessionID &sessionID);  // Client logged on
  void onLogout(const FIX::SessionID &sessionID); // Client logged out
  
  // ========================================================================
  // MESSAGE CALLBACKS - Empty implementations
  // ========================================================================
  void toAdmin(FIX::Message &, const FIX::SessionID &) {}
  void toApp(FIX::Message &, const FIX::SessionID &) EXCEPT(FIX::DoNotSend) {}
  void fromAdmin(const FIX::Message &, const FIX::SessionID &)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) {}
  
  // Main application message router - calls crack() to dispatch to handlers
  void fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType);

  // ========================================================================
  // MESSAGE HANDLERS
  // ========================================================================
  // This implementation only supports FIX 4.2 for orders.
  // Could be extended to support other versions like Executor example.
  
  // Handle new order submissions
  void onMessage(const FIX42::NewOrderSingle &, const FIX::SessionID &);
  
  // Handle order cancellation requests
  void onMessage(const FIX42::OrderCancelRequest &, const FIX::SessionID &);
  
  // Handle market data requests (4.2 version)
  void onMessage(const FIX42::MarketDataRequest &, const FIX::SessionID &);
  
  // Handle market data requests (4.3 version)
  void onMessage(const FIX43::MarketDataRequest &, const FIX::SessionID &);

  // ========================================================================
  // ORDER PROCESSING
  // ========================================================================
  
  // Process a new order: insert into book and attempt matching
  // @param order - Internal order object to process
  void processOrder(const Order &);
  
  // Process an order cancellation
  // @param id - Client Order ID to cancel
  // @param symbol - Trading instrument
  // @param side - Buy or Sell (for disambiguation)
  void processCancel(const std::string &id, const std::string &symbol, Order::Side);

  // ========================================================================
  // EXECUTION REPORT GENERATION
  // ========================================================================
  // These methods generate ExecutionReports for different order states.
  // ExecutionReports notify clients about order status changes.
  
  // Generic update: send ExecutionReport with specified status
  // @param order - Order to report on
  // @param status - FIX order status (NEW, FILLED, PARTIALLY_FILLED, etc.)
  void updateOrder(const Order &, char status);
  
  // Convenience methods for common status updates
  void rejectOrder(const Order &order) { updateOrder(order, FIX::OrdStatus_REJECTED); }
  void acceptOrder(const Order &order) { updateOrder(order, FIX::OrdStatus_NEW); }
  
  // For fills, check if completely filled or partial
  void fillOrder(const Order &order) {
    updateOrder(order, order.isFilled() ? FIX::OrdStatus_FILLED : FIX::OrdStatus_PARTIALLY_FILLED);
  }
  void cancelOrder(const Order &order) { updateOrder(order, FIX::OrdStatus_CANCELED); }

  // Reject order with custom error message
  // Used when order fails validation
  void rejectOrder(
      const FIX::SenderCompID &,
      const FIX::TargetCompID &,
      const FIX::ClOrdID &clOrdID,
      const FIX::Symbol &symbol,
      const FIX::Side &side,
      const std::string &message);

  // ========================================================================
  // TYPE CONVERSION UTILITIES
  // ========================================================================
  // Convert between FIX protocol types and internal Order types.
  // FIX uses different enums than our internal representation.
  
  // FIX Side -> Order Side
  Order::Side convert(const FIX::Side &);
  
  // FIX OrdType -> Order Type
  Order::Type convert(const FIX::OrdType &);
  
  // Order Side -> FIX Side
  FIX::Side convert(Order::Side);
  
  // Order Type -> FIX OrdType
  FIX::OrdType convert(Order::Type);

  // ========================================================================
  // MEMBER VARIABLES
  // ========================================================================
  
  // Core matching engine - maintains order books for all symbols
  OrderMatcher m_orderMatcher;
  
  // ID generator for creating unique order and execution IDs
  IDGenerator m_generator;

public:
  // Provide read-only access to order matcher (for display)
  const OrderMatcher &orderMatcher() { return m_orderMatcher; }
};

#endif
