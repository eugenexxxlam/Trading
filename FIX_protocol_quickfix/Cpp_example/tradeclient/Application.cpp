// ============================================================================
// TRADE CLIENT APPLICATION IMPLEMENTATION
// ============================================================================
// This file implements an interactive FIX trading client with a command-line
// interface for sending orders, canceling orders, and requesting market data.
//
// ARCHITECTURE:
// 1. User selects action from menu
// 2. Application prompts for order details
// 3. Version-specific message is constructed
// 4. Message is sent via FIX session
// 5. ExecutionReports are received and displayed
//
// USER INTERFACE:
// Simple text-based REPL (Read-Eval-Print Loop) where users type responses
// to prompts. In production, this would be a GUI or API layer.
//
// MESSAGE FLOW:
// CLIENT → NewOrderSingle → EXECUTOR
// CLIENT ← ExecutionReport ← EXECUTOR (NEW status)
// CLIENT ← ExecutionReport ← EXECUTOR (FILLED status)
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "Application.h"
#include "quickfix/Session.h"
#include <iostream>

// ============================================================================
// SESSION LIFECYCLE CALLBACKS
// ============================================================================

// Called when session successfully logs on to the executor
// This is the signal that we can start sending orders
void Application::onLogon(const FIX::SessionID &sessionID) {
  std::cout << std::endl << "Logon - " << sessionID << std::endl;
}

// Called when session logs out (graceful disconnect or error)
// Any pending orders should be handled appropriately
void Application::onLogout(const FIX::SessionID &sessionID) {
  std::cout << std::endl << "Logout - " << sessionID << std::endl;
}

// ============================================================================
// INBOUND APPLICATION MESSAGE HANDLER
// ============================================================================
// All application messages (ExecutionReport, OrderCancelReject, etc.) come
// through this callback. We use crack() to route to specific handlers.
void Application::fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) {
  // Route message to appropriate onMessage() handler
  crack(message, sessionID);
  
  // Display the received message for debugging/auditing
  std::cout << std::endl << "IN: " << message << std::endl;
}

// ============================================================================
// OUTBOUND MESSAGE FILTER
// ============================================================================
// Called before any application message is sent to the executor.
// This provides an opportunity to validate, modify, or reject messages.
void Application::toApp(FIX::Message &message, const FIX::SessionID &sessionID) EXCEPT(FIX::DoNotSend) {
  try {
    // Check if this is a possible duplicate (resend situation)
    FIX::PossDupFlag possDupFlag;
    message.getHeader().getField(possDupFlag);
    
    // If PossDupFlag is set to 'Y', don't send (already sent before)
    // This prevents duplicate order submissions during resync
    if (possDupFlag) {
      throw FIX::DoNotSend();
    }
  } catch (FIX::FieldNotFound &) {
    // PossDupFlag not set, which is normal for new messages
  }

  // Display outgoing message for debugging
  std::cout << std::endl << "OUT: " << message << std::endl;
}

// ============================================================================
// EXECUTION REPORT HANDLERS
// ============================================================================
// ExecutionReports provide status updates on our orders.
// For this simple client, we just display them. In production, you would:
// - Update internal order book
// - Update position tracking
// - Trigger alerts/notifications
// - Calculate P&L
// - Record for audit trail

// Empty implementations - messages are displayed in fromApp()
// If you need version-specific handling, implement logic here
void Application::onMessage(const FIX40::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX40::OrderCancelReject &, const FIX::SessionID &) {}
void Application::onMessage(const FIX41::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX41::OrderCancelReject &, const FIX::SessionID &) {}
void Application::onMessage(const FIX42::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX42::OrderCancelReject &, const FIX::SessionID &) {}
void Application::onMessage(const FIX43::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX43::OrderCancelReject &, const FIX::SessionID &) {}
void Application::onMessage(const FIX44::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX44::OrderCancelReject &, const FIX::SessionID &) {}
void Application::onMessage(const FIX50::ExecutionReport &, const FIX::SessionID &) {}
void Application::onMessage(const FIX50::OrderCancelReject &, const FIX::SessionID &) {}

// ============================================================================
// MAIN INTERACTIVE LOOP
// ============================================================================
// Displays menu and processes user choices until user quits.
// This is the heart of the interactive trading client.
void Application::run() {
  while (true) {
    try {
      // Display menu and get user's choice
      char action = queryAction();

      if (action == '1') {
        queryEnterOrder();          // Submit new order
      } else if (action == '2') {
        queryCancelOrder();         // Cancel existing order
      } else if (action == '3') {
        queryReplaceOrder();        // Modify existing order
      } else if (action == '4') {
        queryMarketDataRequest();   // Request market data (FIX 4.3+)
      } else if (action == '5') {
        break;                      // Quit application
      }
    } catch (std::exception &e) {
      // Display error but don't crash - let user retry
      std::cout << "Message Not Sent: " << e.what();
    }
  }
}

// ============================================================================
// ORDER ENTRY WORKFLOW
// ============================================================================
// Prompts user for order details, constructs version-specific message,
// confirms with user, and sends to executor.
void Application::queryEnterOrder() {
  // Get FIX version to use (affects message structure)
  int version = queryVersion();
  std::cout << "\nNewOrderSingle\n";
  FIX::Message order;

  // Construct version-specific NewOrderSingle message
  // Each version has different required fields and constructor signatures
  switch (version) {
  case 40:
    order = queryNewOrderSingle40();
    break;
  case 41:
    order = queryNewOrderSingle41();
    break;
  case 42:
    order = queryNewOrderSingle42();
    break;
  case 43:
    order = queryNewOrderSingle43();
    break;
  case 44:
    order = queryNewOrderSingle44();
    break;
  case 50:
    order = queryNewOrderSingle50();
    break;
  default:
    std::cerr << "No test for version " << version << std::endl;
    break;
  }

  // Confirm before sending (prevent accidental orders)
  if (queryConfirm("Send order")) {
    // Send to target using active session
    // QuickFIX routes to correct session based on SenderCompID/TargetCompID
    FIX::Session::sendToTarget(order);
  }
}

// ============================================================================
// ORDER CANCELLATION WORKFLOW
// ============================================================================
// Prompts for order to cancel, constructs cancel request, and sends.
void Application::queryCancelOrder() {
  int version = queryVersion();
  std::cout << "\nOrderCancelRequest\n";
  FIX::Message cancel;

  // Construct version-specific OrderCancelRequest
  // Requires OrigClOrdID to identify which order to cancel
  switch (version) {
  case 40:
    cancel = queryOrderCancelRequest40();
    break;
  case 41:
    cancel = queryOrderCancelRequest41();
    break;
  case 42:
    cancel = queryOrderCancelRequest42();
    break;
  case 43:
    cancel = queryOrderCancelRequest43();
    break;
  case 44:
    cancel = queryOrderCancelRequest44();
    break;
  case 50:
    cancel = queryOrderCancelRequest50();
    break;
  default:
    std::cerr << "No test for version " << version << std::endl;
    break;
  }

  if (queryConfirm("Send cancel")) {
    FIX::Session::sendToTarget(cancel);
  }
}

// ============================================================================
// ORDER REPLACE WORKFLOW
// ============================================================================
// Replaces an existing order with new parameters (price, quantity, etc.)
// Also known as "amend" or "modify" in some trading systems.
void Application::queryReplaceOrder() {
  int version = queryVersion();
  std::cout << "\nCancelReplaceRequest\n";
  FIX::Message replace;

  // Construct version-specific OrderCancelReplaceRequest
  // Can modify price, quantity, or both
  switch (version) {
  case 40:
    replace = queryCancelReplaceRequest40();
    break;
  case 41:
    replace = queryCancelReplaceRequest41();
    break;
  case 42:
    replace = queryCancelReplaceRequest42();
    break;
  case 43:
    replace = queryCancelReplaceRequest43();
    break;
  case 44:
    replace = queryCancelReplaceRequest44();
    break;
  case 50:
    replace = queryCancelReplaceRequest50();
    break;
  default:
    std::cerr << "No test for version " << version << std::endl;
    break;
  }

  if (queryConfirm("Send replace")) {
    FIX::Session::sendToTarget(replace);
  }
}

// ============================================================================
// MARKET DATA REQUEST WORKFLOW
// ============================================================================
// Requests market data (quotes) for specified symbols.
// Only supported in FIX 4.3 and later versions.
void Application::queryMarketDataRequest() {
  int version = queryVersion();
  std::cout << "\nMarketDataRequest\n";
  FIX::Message md;

  // Market data requests only available in FIX 4.3+
  switch (version) {
  case 43:
    md = queryMarketDataRequest43();
    break;
  case 44:
    md = queryMarketDataRequest44();
    break;
  case 50:
    md = queryMarketDataRequest50();
    break;
  default:
    std::cerr << "No test for version " << version << std::endl;
    break;
  }

  // Market data requests are typically always sent (no confirm needed)
  FIX::Session::sendToTarget(md);
}

// ============================================================================
// FIX 4.0 NEW ORDER CONSTRUCTION
// ============================================================================
// Constructs a FIX 4.0 NewOrderSingle message by prompting user for fields.
//
// FIX 4.0 SPECIFICS:
// - Requires HandlInst field (order handling instructions)
// - OrderQty in constructor
// - TimeInForce is separate from constructor
// - Price/StopPx only for certain order types
FIX40::NewOrderSingle Application::queryNewOrderSingle40() {
  FIX::OrdType ordType;

  // Construct message with required fields
  FIX40::NewOrderSingle newOrderSingle(
      queryClOrdID(),           // Client Order ID (tracking)
      FIX::HandlInst('1'),      // '1' = automated execution, private
      querySymbol(),            // Instrument (e.g., "AAPL")
      querySide(),              // Buy or Sell
      queryOrderQty(),          // Quantity
      ordType = queryOrdType()); // Order type (sets ordType variable)

  // Set time in force (Day, GTC, IOC, etc.)
  newOrderSingle.set(queryTimeInForce());
  
  // For limit and stop-limit orders, need a price
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  
  // For stop and stop-limit orders, need a stop price
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  // Set routing fields in message header
  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// FIX 4.1 NEW ORDER CONSTRUCTION
// ============================================================================
// FIX 4.1 DIFFERENCES FROM 4.0:
// - OrderQty set via .set() instead of constructor
// - Otherwise very similar structure
FIX41::NewOrderSingle Application::queryNewOrderSingle41() {
  FIX::OrdType ordType;

  FIX41::NewOrderSingle
      newOrderSingle(queryClOrdID(), FIX::HandlInst('1'), querySymbol(), querySide(), ordType = queryOrdType());

  // OrderQty now set separately (not in constructor)
  newOrderSingle.set(queryOrderQty());
  newOrderSingle.set(queryTimeInForce());
  
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// FIX 4.2 NEW ORDER CONSTRUCTION
// ============================================================================
// FIX 4.2 DIFFERENCES FROM 4.1:
// - Added TransactTime field (timestamp when order was created)
// - Automatically set to current time
FIX42::NewOrderSingle Application::queryNewOrderSingle42() {
  FIX::OrdType ordType;

  FIX42::NewOrderSingle newOrderSingle(
      queryClOrdID(),
      FIX::HandlInst('1'),
      querySymbol(),
      querySide(),
      FIX::TransactTime(),      // Timestamp - set to current time
      ordType = queryOrdType());

  newOrderSingle.set(queryOrderQty());
  newOrderSingle.set(queryTimeInForce());
  
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// FIX 4.3 NEW ORDER CONSTRUCTION
// ============================================================================
// FIX 4.3 DIFFERENCES FROM 4.2:
// - Symbol removed from constructor, set via .set()
// - Simplified constructor with fewer required fields
FIX43::NewOrderSingle Application::queryNewOrderSingle43() {
  FIX::OrdType ordType;

  FIX43::NewOrderSingle
      newOrderSingle(queryClOrdID(), FIX::HandlInst('1'), querySide(), FIX::TransactTime(), ordType = queryOrdType());

  // Symbol now set separately
  newOrderSingle.set(querySymbol());
  newOrderSingle.set(queryOrderQty());
  newOrderSingle.set(queryTimeInForce());
  
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// FIX 4.4 NEW ORDER CONSTRUCTION
// ============================================================================
// FIX 4.4 DIFFERENCES FROM 4.3:
// - HandlInst removed from constructor, set via .set()
// - Even more simplified constructor
FIX44::NewOrderSingle Application::queryNewOrderSingle44() {
  FIX::OrdType ordType;

  FIX44::NewOrderSingle newOrderSingle(queryClOrdID(), querySide(), FIX::TransactTime(), ordType = queryOrdType());

  // HandlInst now set separately
  newOrderSingle.set(FIX::HandlInst('1'));
  newOrderSingle.set(querySymbol());
  newOrderSingle.set(queryOrderQty());
  newOrderSingle.set(queryTimeInForce());
  
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// FIX 5.0 NEW ORDER CONSTRUCTION
// ============================================================================
// FIX 5.0 uses same structure as 4.4 for NewOrderSingle
// Major changes are in other message types and infrastructure (FIXT)
FIX50::NewOrderSingle Application::queryNewOrderSingle50() {
  FIX::OrdType ordType;

  FIX50::NewOrderSingle newOrderSingle(queryClOrdID(), querySide(), FIX::TransactTime(), ordType = queryOrdType());

  newOrderSingle.set(FIX::HandlInst('1'));
  newOrderSingle.set(querySymbol());
  newOrderSingle.set(queryOrderQty());
  newOrderSingle.set(queryTimeInForce());
  
  if (ordType == FIX::OrdType_LIMIT || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryPrice());
  }
  if (ordType == FIX::OrdType_STOP || ordType == FIX::OrdType_STOP_LIMIT) {
    newOrderSingle.set(queryStopPx());
  }

  queryHeader(newOrderSingle.getHeader());
  return newOrderSingle;
}

// ============================================================================
// ORDER CANCEL REQUEST CONSTRUCTORS (All Versions)
// ============================================================================
// These construct OrderCancelRequest messages for each FIX version.
// Required fields:
// - OrigClOrdID: ID of order to cancel
// - ClOrdID: New ID for this cancel request
// - Symbol, Side, OrderQty: Match original order (for validation)

FIX40::OrderCancelRequest Application::queryOrderCancelRequest40() {
  // FIX 4.0 requires CxlType field ('F' = full cancel)
  FIX40::OrderCancelRequest orderCancelRequest(
      queryOrigClOrdID(),       // Order to cancel
      queryClOrdID(),           // ID for this cancel request
      FIX::CxlType('F'),        // 'F' = full cancel (vs partial)
      querySymbol(),
      querySide(),
      queryOrderQty());

  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

FIX41::OrderCancelRequest Application::queryOrderCancelRequest41() {
  // FIX 4.1 removed CxlType, added OrderQty via .set()
  FIX41::OrderCancelRequest orderCancelRequest(queryOrigClOrdID(), queryClOrdID(), querySymbol(), querySide());

  orderCancelRequest.set(queryOrderQty());
  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

FIX42::OrderCancelRequest Application::queryOrderCancelRequest42() {
  // FIX 4.2 added TransactTime
  FIX42::OrderCancelRequest
      orderCancelRequest(queryOrigClOrdID(), queryClOrdID(), querySymbol(), querySide(), FIX::TransactTime());

  orderCancelRequest.set(queryOrderQty());
  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

FIX43::OrderCancelRequest Application::queryOrderCancelRequest43() {
  // FIX 4.3 removed Symbol from constructor
  FIX43::OrderCancelRequest orderCancelRequest(queryOrigClOrdID(), queryClOrdID(), querySide(), FIX::TransactTime());

  orderCancelRequest.set(querySymbol());
  orderCancelRequest.set(queryOrderQty());
  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

FIX44::OrderCancelRequest Application::queryOrderCancelRequest44() {
  // FIX 4.4 same as 4.3
  FIX44::OrderCancelRequest orderCancelRequest(queryOrigClOrdID(), queryClOrdID(), querySide(), FIX::TransactTime());

  orderCancelRequest.set(querySymbol());
  orderCancelRequest.set(queryOrderQty());
  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

FIX50::OrderCancelRequest Application::queryOrderCancelRequest50() {
  // FIX 5.0 same as 4.4
  FIX50::OrderCancelRequest orderCancelRequest(queryOrigClOrdID(), queryClOrdID(), querySide(), FIX::TransactTime());

  orderCancelRequest.set(querySymbol());
  orderCancelRequest.set(queryOrderQty());
  queryHeader(orderCancelRequest.getHeader());
  return orderCancelRequest;
}

// ============================================================================
// ORDER CANCEL/REPLACE REQUEST CONSTRUCTORS (All Versions)
// ============================================================================
// These allow modifying an existing order (price, quantity, or both).
// User is prompted whether they want to change price and/or quantity.

FIX40::OrderCancelReplaceRequest Application::queryCancelReplaceRequest40() {
  FIX40::OrderCancelReplaceRequest cancelReplaceRequest(
      queryOrigClOrdID(),       // Order to modify
      queryClOrdID(),           // ID for this replace request
      FIX::HandlInst('1'),
      querySymbol(),
      querySide(),
      queryOrderQty(),          // New quantity (or same as before)
      queryOrdType());

  // Optionally change price
  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  
  // Optionally change quantity
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

FIX41::OrderCancelReplaceRequest Application::queryCancelReplaceRequest41() {
  FIX41::OrderCancelReplaceRequest cancelReplaceRequest(
      queryOrigClOrdID(),
      queryClOrdID(),
      FIX::HandlInst('1'),
      querySymbol(),
      querySide(),
      queryOrdType());

  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

FIX42::OrderCancelReplaceRequest Application::queryCancelReplaceRequest42() {
  FIX42::OrderCancelReplaceRequest cancelReplaceRequest(
      queryOrigClOrdID(),
      queryClOrdID(),
      FIX::HandlInst('1'),
      querySymbol(),
      querySide(),
      FIX::TransactTime(),
      queryOrdType());

  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

FIX43::OrderCancelReplaceRequest Application::queryCancelReplaceRequest43() {
  FIX43::OrderCancelReplaceRequest cancelReplaceRequest(
      queryOrigClOrdID(),
      queryClOrdID(),
      FIX::HandlInst('1'),
      querySide(),
      FIX::TransactTime(),
      queryOrdType());

  cancelReplaceRequest.set(querySymbol());
  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

FIX44::OrderCancelReplaceRequest Application::queryCancelReplaceRequest44() {
  FIX44::OrderCancelReplaceRequest
      cancelReplaceRequest(queryOrigClOrdID(), queryClOrdID(), querySide(), FIX::TransactTime(), queryOrdType());

  cancelReplaceRequest.set(FIX::HandlInst('1'));
  cancelReplaceRequest.set(querySymbol());
  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

FIX50::OrderCancelReplaceRequest Application::queryCancelReplaceRequest50() {
  FIX50::OrderCancelReplaceRequest
      cancelReplaceRequest(queryOrigClOrdID(), queryClOrdID(), querySide(), FIX::TransactTime(), queryOrdType());

  cancelReplaceRequest.set(FIX::HandlInst('1'));
  cancelReplaceRequest.set(querySymbol());
  if (queryConfirm("New price")) {
    cancelReplaceRequest.set(queryPrice());
  }
  if (queryConfirm("New quantity")) {
    cancelReplaceRequest.set(queryOrderQty());
  }

  queryHeader(cancelReplaceRequest.getHeader());
  return cancelReplaceRequest;
}

// ============================================================================
// MARKET DATA REQUEST CONSTRUCTORS (FIX 4.3+)
// ============================================================================
// These request market data (bid/ask quotes) for specified symbols.
// Uses repeating groups to specify multiple symbols and entry types.

FIX43::MarketDataRequest Application::queryMarketDataRequest43() {
  FIX::MDReqID mdReqID("MARKETDATAID");                                   // Request ID
  FIX::SubscriptionRequestType subType(FIX::SubscriptionRequestType_SNAPSHOT);  // One-time snapshot
  FIX::MarketDepth marketDepth(0);                                        // 0 = top of book only

  // Repeating group: What data types do we want?
  FIX43::MarketDataRequest::NoMDEntryTypes marketDataEntryGroup;
  FIX::MDEntryType mdEntryType(FIX::MDEntryType_BID);                    // BID prices
  marketDataEntryGroup.set(mdEntryType);

  // Repeating group: Which symbols do we want data for?
  FIX43::MarketDataRequest::NoRelatedSym symbolGroup;
  FIX::Symbol symbol("LNUX");                                            // Example: LNUX stock
  symbolGroup.set(symbol);

  // Construct message and add groups
  FIX43::MarketDataRequest message(mdReqID, subType, marketDepth);
  message.addGroup(marketDataEntryGroup);
  message.addGroup(symbolGroup);

  queryHeader(message.getHeader());

  // Display message in XML and FIX formats for debugging
  std::cout << message.toXML() << std::endl;
  std::cout << message.toString() << std::endl;

  return message;
}

FIX44::MarketDataRequest Application::queryMarketDataRequest44() {
  // Same structure as 4.3
  FIX::MDReqID mdReqID("MARKETDATAID");
  FIX::SubscriptionRequestType subType(FIX::SubscriptionRequestType_SNAPSHOT);
  FIX::MarketDepth marketDepth(0);

  FIX44::MarketDataRequest::NoMDEntryTypes marketDataEntryGroup;
  FIX::MDEntryType mdEntryType(FIX::MDEntryType_BID);
  marketDataEntryGroup.set(mdEntryType);

  FIX44::MarketDataRequest::NoRelatedSym symbolGroup;
  FIX::Symbol symbol("LNUX");
  symbolGroup.set(symbol);

  FIX44::MarketDataRequest message(mdReqID, subType, marketDepth);
  message.addGroup(marketDataEntryGroup);
  message.addGroup(symbolGroup);

  queryHeader(message.getHeader());

  std::cout << message.toXML() << std::endl;
  std::cout << message.toString() << std::endl;

  return message;
}

FIX50::MarketDataRequest Application::queryMarketDataRequest50() {
  // Same structure as 4.4
  FIX::MDReqID mdReqID("MARKETDATAID");
  FIX::SubscriptionRequestType subType(FIX::SubscriptionRequestType_SNAPSHOT);
  FIX::MarketDepth marketDepth(0);

  FIX50::MarketDataRequest::NoMDEntryTypes marketDataEntryGroup;
  FIX::MDEntryType mdEntryType(FIX::MDEntryType_BID);
  marketDataEntryGroup.set(mdEntryType);

  FIX50::MarketDataRequest::NoRelatedSym symbolGroup;
  FIX::Symbol symbol("LNUX");
  symbolGroup.set(symbol);

  FIX50::MarketDataRequest message(mdReqID, subType, marketDepth);
  message.addGroup(marketDataEntryGroup);
  message.addGroup(symbolGroup);

  queryHeader(message.getHeader());

  std::cout << message.toXML() << std::endl;
  std::cout << message.toString() << std::endl;

  return message;
}

// ============================================================================
// MESSAGE HEADER CONFIGURATION
// ============================================================================
// Sets routing fields in the message header (who is sending to whom).
// These fields determine which FIX session the message is sent on.
void Application::queryHeader(FIX::Header &header) {
  header.setField(querySenderCompID());   // Who is sending (us)
  header.setField(queryTargetCompID());   // Who to send to (executor)

  // Optional sub-ID for routing within target organization
  if (queryConfirm("Use a TargetSubID")) {
    header.setField(queryTargetSubID());
  }
}

// ============================================================================
// MAIN MENU
// ============================================================================
// Displays action menu and returns user's choice.
char Application::queryAction() {
  char value;
  std::cout << std::endl
            << "1) Enter Order" << std::endl
            << "2) Cancel Order" << std::endl
            << "3) Replace Order" << std::endl
            << "4) Market data test" << std::endl
            << "5) Quit" << std::endl
            << "Action: ";
  std::cin >> value;
  
  // Validate input
  switch (value) {
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
    break;
  default:
    throw std::exception();  // Invalid choice
  }
  return value;
}

// ============================================================================
// VERSION SELECTION
// ============================================================================
// Prompts user to select which FIX version to use.
// Returns numeric version (40, 41, 42, 43, 44, 50).
int Application::queryVersion() {
  char value;
  std::cout << std::endl
            << "1) FIX.4.0" << std::endl
            << "2) FIX.4.1" << std::endl
            << "3) FIX.4.2" << std::endl
            << "4) FIX.4.3" << std::endl
            << "5) FIX.4.4" << std::endl
            << "6) FIXT.1.1 (FIX.5.0)" << std::endl
            << "BeginString: ";
  std::cin >> value;
  
  switch (value) {
  case '1':
    return 40;
  case '2':
    return 41;
  case '3':
    return 42;
  case '4':
    return 43;
  case '5':
    return 44;
  case '6':
    return 50;
  default:
    throw std::exception();
  }
}

// ============================================================================
// YES/NO CONFIRMATION
// ============================================================================
// General purpose confirmation prompt. Returns true if user types 'Y' or 'y'.
bool Application::queryConfirm(const std::string &query) {
  std::string value;
  std::cout << std::endl << query << "?: ";
  std::cin >> value;
  return toupper(*value.c_str()) == 'Y';
}

// ============================================================================
// FIELD INPUT METHODS
// ============================================================================
// Each method prompts for a specific FIX field and returns the field object.
// These are used to build messages in the query*Order*() methods above.

FIX::SenderCompID Application::querySenderCompID() {
  std::string value;
  std::cout << std::endl << "SenderCompID: ";
  std::cin >> value;
  return FIX::SenderCompID(value);
}

FIX::TargetCompID Application::queryTargetCompID() {
  std::string value;
  std::cout << std::endl << "TargetCompID: ";
  std::cin >> value;
  return FIX::TargetCompID(value);
}

FIX::TargetSubID Application::queryTargetSubID() {
  std::string value;
  std::cout << std::endl << "TargetSubID: ";
  std::cin >> value;
  return FIX::TargetSubID(value);
}

FIX::ClOrdID Application::queryClOrdID() {
  std::string value;
  std::cout << std::endl << "ClOrdID: ";
  std::cin >> value;
  return FIX::ClOrdID(value);
}

FIX::OrigClOrdID Application::queryOrigClOrdID() {
  std::string value;
  std::cout << std::endl << "OrigClOrdID: ";
  std::cin >> value;
  return FIX::OrigClOrdID(value);
}

FIX::Symbol Application::querySymbol() {
  std::string value;
  std::cout << std::endl << "Symbol: ";
  std::cin >> value;
  return FIX::Symbol(value);
}

// ============================================================================
// SIDE SELECTION (Buy/Sell)
// ============================================================================
// Prompts user to select order side with menu.
FIX::Side Application::querySide() {
  char value;
  std::cout << std::endl
            << "1) Buy" << std::endl
            << "2) Sell" << std::endl
            << "3) Sell Short" << std::endl
            << "4) Sell Short Exempt" << std::endl
            << "5) Cross" << std::endl
            << "6) Cross Short" << std::endl
            << "7) Cross Short Exempt" << std::endl
            << "Side: ";

  std::cin >> value;
  switch (value) {
  case '1':
    return FIX::Side(FIX::Side_BUY);
  case '2':
    return FIX::Side(FIX::Side_SELL);
  case '3':
    return FIX::Side(FIX::Side_SELL_SHORT);
  case '4':
    return FIX::Side(FIX::Side_SELL_SHORT_EXEMPT);
  case '5':
    return FIX::Side(FIX::Side_CROSS);
  case '6':
    return FIX::Side(FIX::Side_CROSS_SHORT);
  case '7':
    return FIX::Side('A');  // Cross short exempt
  default:
    throw std::exception();
  }
}

FIX::OrderQty Application::queryOrderQty() {
  long value;
  std::cout << std::endl << "OrderQty: ";
  std::cin >> value;
  return FIX::OrderQty(value);
}

// ============================================================================
// ORDER TYPE SELECTION
// ============================================================================
// Prompts for order type: Market, Limit, Stop, or Stop-Limit.
FIX::OrdType Application::queryOrdType() {
  char value;
  std::cout << std::endl
            << "1) Market" << std::endl
            << "2) Limit" << std::endl
            << "3) Stop" << std::endl
            << "4) Stop Limit" << std::endl
            << "OrdType: ";

  std::cin >> value;
  switch (value) {
  case '1':
    return FIX::OrdType(FIX::OrdType_MARKET);
  case '2':
    return FIX::OrdType(FIX::OrdType_LIMIT);
  case '3':
    return FIX::OrdType(FIX::OrdType_STOP);
  case '4':
    return FIX::OrdType(FIX::OrdType_STOP_LIMIT);
  default:
    throw std::exception();
  }
}

FIX::Price Application::queryPrice() {
  double value;
  std::cout << std::endl << "Price: ";
  std::cin >> value;
  return FIX::Price(value);
}

FIX::StopPx Application::queryStopPx() {
  double value;
  std::cout << std::endl << "StopPx: ";
  std::cin >> value;
  return FIX::StopPx(value);
}

// ============================================================================
// TIME IN FORCE SELECTION
// ============================================================================
// Determines how long order remains active:
// - Day: Valid until end of trading day
// - IOC (Immediate or Cancel): Execute immediately or cancel
// - OPG (At the Opening): Execute at market open
// - GTC (Good Till Cancel): Valid until explicitly canceled
// - GTX (Good Till Crossing): Valid until crossing session
FIX::TimeInForce Application::queryTimeInForce() {
  char value;
  std::cout << std::endl
            << "1) Day" << std::endl
            << "2) IOC" << std::endl
            << "3) OPG" << std::endl
            << "4) GTC" << std::endl
            << "5) GTX" << std::endl
            << "TimeInForce: ";

  std::cin >> value;
  switch (value) {
  case '1':
    return FIX::TimeInForce(FIX::TimeInForce_DAY);
  case '2':
    return FIX::TimeInForce(FIX::TimeInForce_IMMEDIATE_OR_CANCEL);
  case '3':
    return FIX::TimeInForce(FIX::TimeInForce_AT_THE_OPENING);
  case '4':
    return FIX::TimeInForce(FIX::TimeInForce_GOOD_TILL_CANCEL);
  case '5':
    return FIX::TimeInForce(FIX::TimeInForce_GOOD_TILL_CROSSING);
  default:
    throw std::exception();
  }
}
