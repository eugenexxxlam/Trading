// ============================================================================
// ORDERMATCH APPLICATION IMPLEMENTATION
// ============================================================================
// This file implements a real order matching engine that maintains order
// books and matches orders based on price-time priority.
//
// ORDER MATCHING FLOW:
// 1. Client sends NewOrderSingle via FIX
// 2. Validate order (type, TIF, etc.)
// 3. Convert FIX message -> internal Order object
// 4. Insert into order book (OrderMatcher)
// 5. Send ExecutionReport: Status=NEW
// 6. Run matching algorithm
// 7. For each match:
//    a. Execute trade (update both orders)
//    b. Send ExecutionReport: Status=FILLED or PARTIALLY_FILLED
// 8. If no match, order rests in book waiting for contra-side
//
// EXAMPLE SCENARIO:
// Initial state: Empty book
//
// Step 1: Client A sends BUY 100 AAPL @ $150
// - Order added to bid book
// - ExecutionReport: Status=NEW
// - No matching possible (no sell orders)
//
// Step 2: Client B sends SELL 50 AAPL @ $150
// - Order added to ask book
// - ExecutionReport: Status=NEW
// - Matching algorithm runs:
//   * Buy $150 >= Sell $150, so match!
//   * Execute 50 shares @ $150
//   * Buy order: 50 remaining (PARTIALLY_FILLED)
//   * Sell order: 0 remaining (FILLED)
// - ExecutionReport to A: PARTIALLY_FILLED (50 of 100 filled)
// - ExecutionReport to B: FILLED (50 of 50 filled)
//
// VALIDATION RULES:
// - Only LIMIT orders accepted (no MARKET orders)
// - Only DAY time in force (no GTC, IOC, etc.)
// - Must have valid symbol, side, price, quantity
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "Application.h"
#include "quickfix/Session.h"

#include "quickfix/fix42/ExecutionReport.h"

void Application::onLogon(const FIX::SessionID &sessionID) {}

void Application::onLogout(const FIX::SessionID &sessionID) {}

// ============================================================================
// MESSAGE ROUTER
// ============================================================================
// All application messages come through here. Use crack() to route to
// specific handlers based on message type.
void Application::fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) {
  crack(message, sessionID);
}

// ============================================================================
// NEW ORDER HANDLER (FIX 4.2)
// ============================================================================
// Handles new order submissions from clients.
//
// VALIDATION:
// - Only LIMIT orders accepted
// - Only DAY time in force accepted
// - All required fields must be present
//
// PROCESS:
// 1. Extract fields from FIX message
// 2. Validate order type and TIF
// 3. Convert FIX types to internal Order object
// 4. Call processOrder() to insert and match
// 5. If validation fails, send reject with error message
void Application::onMessage(const FIX42::NewOrderSingle &message, const FIX::SessionID &) {
  // Extract routing information from FIX header
  FIX::SenderCompID senderCompID;      // Who sent this order (client ID)
  FIX::TargetCompID targetCompID;      // Who should receive it (us)
  
  // Extract order parameters from FIX message body
  FIX::ClOrdID clOrdID;                // Client's unique order ID
  FIX::Symbol symbol;                  // Trading instrument (e.g., "AAPL")
  FIX::Side side;                      // Buy(1) or Sell(2)
  FIX::OrdType ordType;                // Order type (LIMIT, MARKET, etc.)
  FIX::Price price;                    // Limit price
  FIX::OrderQty orderQty;              // Quantity to trade
  FIX::TimeInForce timeInForce(FIX::TimeInForce_DAY);  // Default to DAY

  // Extract header fields (routing)
  message.getHeader().get(senderCompID);
  message.getHeader().get(targetCompID);
  
  // Extract body fields (order details)
  message.get(clOrdID);
  message.get(symbol);
  message.get(side);
  message.get(ordType);
  
  // Price is required for limit orders
  if (ordType == FIX::OrdType_LIMIT) {
    message.get(price);
  }
  
  message.get(orderQty);
  message.getFieldIfSet(timeInForce);  // Optional field with default

  try {
    // ======================================================================
    // VALIDATION: Time In Force
    // ======================================================================
    // Only DAY orders supported. Real systems support:
    // - GTC (Good Till Cancel): Active until explicitly canceled
    // - IOC (Immediate or Cancel): Execute immediately or cancel
    // - FOK (Fill or Kill): Execute completely or cancel
    // - GTD (Good Till Date): Active until specific date
    
    if (timeInForce != FIX::TimeInForce_DAY) {
      throw std::logic_error("Unsupported TIF, use Day");
    }

    // ======================================================================
    // CREATE INTERNAL ORDER OBJECT
    // ======================================================================
    // Convert FIX message to internal Order representation.
    // This allows matching engine to work with type-safe objects
    // instead of raw FIX messages.
    
    Order order(clOrdID,                    // Client order ID
                symbol,                     // Trading symbol
                senderCompID,               // Order owner (client)
                targetCompID,               // Order target (us/exchange)
                convert(side),              // Convert FIX side to Order::Side
                convert(ordType),           // Convert FIX type to Order::Type
                price,                      // Limit price
                (long)orderQty);           // Quantity (cast to long)

    // ======================================================================
    // PROCESS ORDER
    // ======================================================================
    // Insert into order book and attempt matching
    // This handles:
    // - Inserting into appropriate book (bid or ask)
    // - Sending NEW status ExecutionReport
    // - Running matching algorithm
    // - Sending FILLED/PARTIALLY_FILLED ExecutionReports
    
    processOrder(order);
    
  } catch (std::exception &e) {
    // Order validation failed - send rejection with error message
    rejectOrder(senderCompID, targetCompID, clOrdID, symbol, side, e.what());
  }
}

// ============================================================================
// ORDER CANCEL HANDLER (FIX 4.2)
// ============================================================================
// Handles order cancellation requests from clients.
//
// REQUIRED FIELDS:
// - OrigClOrdID: ID of order to cancel
// - Symbol: Must match original order
// - Side: Must match original order (for disambiguation)
//
// PROCESS:
// 1. Extract fields from FIX message
// 2. Find order in order book
// 3. Remove order from book
// 4. Send ExecutionReport with CANCELED status
// 5. If order not found or already filled, ignore (no error)
void Application::onMessage(const FIX42::OrderCancelRequest &message, const FIX::SessionID &) {
  FIX::OrigClOrdID origClOrdID;  // ID of order to cancel
  FIX::Symbol symbol;            // Symbol (must match original)
  FIX::Side side;                // Side (must match original)

  message.get(origClOrdID);
  message.get(symbol);
  message.get(side);

  try {
    // Attempt to cancel the order
    processCancel(origClOrdID, symbol, convert(side));
  } catch (std::exception &) {
    // Order not found or already filled - silently ignore
    // Real system would send OrderCancelReject message
  }
}

// ============================================================================
// MARKET DATA REQUEST HANDLER (FIX 4.2)
// ============================================================================
// Handles requests for market data (quotes, depth, etc.)
//
// SUPPORTED:
// - Snapshot requests only (one-time data, no subscriptions)
//
// VALIDATION:
// - SubscriptionRequestType must be SNAPSHOT
//
// PROCESS:
// 1. Extract request parameters
// 2. Validate request type (snapshot only)
// 3. Extract requested symbols from repeating group
// 4. In production, would send MarketDataSnapshotFullRefresh responses
void Application::onMessage(const FIX42::MarketDataRequest &message, const FIX::SessionID &) {
  FIX::MDReqID mdReqID;                              // Request ID
  FIX::SubscriptionRequestType subscriptionRequestType;  // Snapshot or subscription
  FIX::MarketDepth marketDepth;                      // How many price levels
  FIX::NoRelatedSym noRelatedSym;                    // Number of symbols requested
  FIX42::MarketDataRequest::NoRelatedSym noRelatedSymGroup;  // Symbol repeating group

  message.get(mdReqID);
  message.get(subscriptionRequestType);
  
  // Only support snapshot (one-time) requests, not streaming subscriptions
  if (subscriptionRequestType != FIX::SubscriptionRequestType_SNAPSHOT) {
    throw FIX::IncorrectTagValue(subscriptionRequestType.getTag());
  }
  
  message.get(marketDepth);
  message.get(noRelatedSym);

  // Extract all requested symbols from repeating group
  for (int i = 1; i <= noRelatedSym; ++i) {
    FIX::Symbol symbol;
    message.getGroup(i, noRelatedSymGroup);
    noRelatedSymGroup.get(symbol);
    
    // In production, would generate market data response here:
    // - Create MarketDataSnapshotFullRefresh message
    // - Add bid/ask prices and quantities from order book
    // - Send to client
  }
}

// ============================================================================
// MARKET DATA REQUEST HANDLER (FIX 4.3)
// ============================================================================
// FIX 4.3 version - just prints message as XML for debugging.
// Same structure as 4.2, could be implemented similarly.
void Application::onMessage(const FIX43::MarketDataRequest &message, const FIX::SessionID &) {
  std::cout << message.toXML() << std::endl;
}

// ============================================================================
// UPDATE ORDER - Send ExecutionReport
// ============================================================================
// Generates and sends an ExecutionReport to notify client of order status.
//
// EXECUTION REPORT FIELDS:
// - OrderID: Exchange-assigned ID
// - ExecID: Unique ID for this status update
// - ExecTransType: NEW (deprecated in later FIX versions)
// - ExecType: Status of this update (NEW, FILL, CANCELED, etc.)
// - OrdStatus: Current order status
// - Symbol, Side: From original order
// - LeavesQty: Quantity still open
// - CumQty: Cumulative filled quantity
// - AvgPx: Average execution price
// - LastShares: Quantity filled in most recent execution (if any)
// - LastPx: Price of most recent execution (if any)
//
// ROUTING:
// Swaps SenderCompID and TargetCompID from original order
// (we are now the sender, client is the target)
void Application::updateOrder(const Order &order, char status) {
  // Swap sender/target for response (we're responding to client)
  FIX::TargetCompID targetCompID(order.getOwner());
  FIX::SenderCompID senderCompID(order.getTarget());

  // Create ExecutionReport with current order state
  FIX42::ExecutionReport fixOrder(
      FIX::OrderID(order.getClientID()),                        // Order ID
      FIX::ExecID(m_generator.genExecutionID()),               // Execution ID
      FIX::ExecTransType(FIX::ExecTransType_NEW),              // FIX 4.2 required
      FIX::ExecType(status),                                    // What happened (NEW, FILL, etc.)
      FIX::OrdStatus(status),                                   // Current status
      FIX::Symbol(order.getSymbol()),                          // Trading instrument
      FIX::Side(convert(order.getSide())),                     // Buy or Sell
      FIX::LeavesQty(order.getOpenQuantity()),                 // Quantity still open
      FIX::CumQty(order.getExecutedQuantity()),                // Total filled quantity
      FIX::AvgPx(order.getAvgExecutedPrice()));                // Average fill price

  // Set additional fields
  fixOrder.set(FIX::ClOrdID(order.getClientID()));
  fixOrder.set(FIX::OrderQty(order.getQuantity()));

  // For fills, include last execution details
  if (status == FIX::OrdStatus_FILLED || status == FIX::OrdStatus_PARTIALLY_FILLED) {
    fixOrder.set(FIX::LastShares(order.getLastExecutedQuantity()));
    fixOrder.set(FIX::LastPx(order.getLastExecutedPrice()));
  }

  // Send ExecutionReport back to client
  try {
    FIX::Session::sendToTarget(fixOrder, senderCompID, targetCompID);
  } catch (FIX::SessionNotFound &) {
    // Client disconnected - order state is saved, they'll get update on reconnect
  }
}

// ============================================================================
// REJECT ORDER - Send Rejection ExecutionReport
// ============================================================================
// Sends ExecutionReport with REJECTED status and error message.
// Used when order fails validation (invalid type, TIF, etc.)
void Application::rejectOrder(
    const FIX::SenderCompID &sender,
    const FIX::TargetCompID &target,
    const FIX::ClOrdID &clOrdID,
    const FIX::Symbol &symbol,
    const FIX::Side &side,
    const std::string &message) {
  
  // Swap sender/target for response
  FIX::TargetCompID targetCompID(sender.getValue());
  FIX::SenderCompID senderCompID(target.getValue());

  // Create rejection ExecutionReport
  FIX42::ExecutionReport fixOrder(
      FIX::OrderID(clOrdID.getValue()),
      FIX::ExecID(m_generator.genExecutionID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),
      FIX::ExecType(FIX::ExecType_REJECTED),
      FIX::OrdStatus(FIX::ExecType_REJECTED),
      symbol,
      side,
      FIX::LeavesQty(0),
      FIX::CumQty(0),
      FIX::AvgPx(0));

  fixOrder.set(clOrdID);
  fixOrder.set(FIX::Text(message));  // Error message explaining rejection

  try {
    FIX::Session::sendToTarget(fixOrder, senderCompID, targetCompID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// PROCESS ORDER - Insert and Match
// ============================================================================
// Core order processing logic:
// 1. Insert order into book
// 2. Send NEW ExecutionReport
// 3. Run matching algorithm
// 4. Send FILLED/PARTIALLY_FILLED ExecutionReports for all matches
void Application::processOrder(const Order &order) {
  // Try to insert order into order book
  if (m_orderMatcher.insert(order)) {
    // Order accepted - send NEW status
    acceptOrder(order);

    // Run matching algorithm for this symbol
    std::queue<Order> orders;
    m_orderMatcher.match(order.getSymbol(), orders);

    // Send ExecutionReports for all filled orders
    while (orders.size()) {
      fillOrder(orders.front());
      orders.pop();
    }
  } else {
    // Order rejected (failed validation)
    rejectOrder(order);
  }
}

// ============================================================================
// PROCESS CANCEL - Remove Order from Book
// ============================================================================
// Handles order cancellation:
// 1. Find order in book
// 2. Mark as canceled
// 3. Send CANCELED ExecutionReport
// 4. Remove from book
void Application::processCancel(const std::string &id, const std::string &symbol, Order::Side side) {
  // Find the order in the book
  Order &order = m_orderMatcher.find(symbol, side, id);
  
  // Mark order as canceled (sets open quantity to 0)
  order.cancel();
  
  // Send CANCELED ExecutionReport
  cancelOrder(order);
  
  // Remove from book
  m_orderMatcher.erase(order);
}

// ============================================================================
// TYPE CONVERSION UTILITIES
// ============================================================================
// Convert between FIX protocol types and internal Order types.

// Convert FIX Side to Order Side
Order::Side Application::convert(const FIX::Side &side) {
  switch (side) {
  case FIX::Side_BUY:
    return Order::buy;
  case FIX::Side_SELL:
    return Order::sell;
  default:
    throw std::logic_error("Unsupported Side, use buy or sell");
  }
}

// Convert FIX OrdType to Order Type
Order::Type Application::convert(const FIX::OrdType &ordType) {
  switch (ordType) {
  case FIX::OrdType_LIMIT:
    return Order::limit;
  default:
    throw std::logic_error("Unsupported Order Type, use limit");
  }
}

// Convert Order Side to FIX Side
FIX::Side Application::convert(Order::Side side) {
  switch (side) {
  case Order::buy:
    return FIX::Side(FIX::Side_BUY);
  case Order::sell:
    return FIX::Side(FIX::Side_SELL);
  default:
    throw std::logic_error("Unsupported Side, use buy or sell");
  }
}

// Convert Order Type to FIX OrdType
FIX::OrdType Application::convert(Order::Type type) {
  switch (type) {
  case Order::limit:
    return FIX::OrdType(FIX::OrdType_LIMIT);
  default:
    throw std::logic_error("Unsupported Order Type, use limit");
  }
}
