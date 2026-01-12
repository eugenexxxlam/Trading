// ============================================================================
// EXECUTOR APPLICATION IMPLEMENTATION
// ============================================================================
// This file implements the order execution logic for a FIX Protocol Executor.
//
// EXECUTION MODEL:
// This is a "straight-through processing" (STP) executor that immediately
// fills all valid limit orders at their specified price. Real-world executors
// would interact with markets, check liquidity, and manage order books.
//
// MESSAGE FLOW:
// 1. Receive NewOrderSingle from client
// 2. Validate order type (only LIMIT orders accepted)
// 3. Extract order parameters (symbol, side, quantity, price, etc.)
// 4. Generate OrderID and ExecID
// 5. Create ExecutionReport showing order is FILLED
// 6. Send ExecutionReport back to client
//
// ERROR HANDLING:
// - Invalid order types throw IncorrectTagValue exception
// - Missing required fields throw FieldNotFound exception
// - Session errors are caught and ignored (client may have disconnected)
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "Application.h"
#include "quickfix/Session.h"

// Include ExecutionReport definitions for all supported FIX versions
// ExecutionReport is the primary response message sent by executors
#include "quickfix/fix40/ExecutionReport.h"
#include "quickfix/fix41/ExecutionReport.h"
#include "quickfix/fix42/ExecutionReport.h"
#include "quickfix/fix43/ExecutionReport.h"
#include "quickfix/fix44/ExecutionReport.h"
#include "quickfix/fix50/ExecutionReport.h"

// ============================================================================
// LIFECYCLE CALLBACKS - Empty implementations
// ============================================================================
// These callbacks are intentionally empty for this simple example.
// In production systems, you would use these for:
// - onCreate: Initialize data structures, load reference data
// - onLogon: Start accepting orders, log the event, notify monitoring
// - onLogout: Stop accepting orders, handle open orders, cleanup
// ============================================================================

void Application::onCreate(const FIX::SessionID &sessionID) {}
void Application::onLogon(const FIX::SessionID &sessionID) {}
void Application::onLogout(const FIX::SessionID &sessionID) {}

// ============================================================================
// ADMIN MESSAGE CALLBACKS - Empty implementations
// ============================================================================
// For this simple executor, we don't need custom admin message handling.
// In production, you might:
// - toAdmin: Add custom authentication fields to Logon messages
// - fromAdmin: Validate credentials, handle test requests
// ============================================================================

void Application::toAdmin(FIX::Message &message, const FIX::SessionID &sessionID) {}

// Called for outgoing application messages
// This could be used to validate or modify messages before sending
void Application::toApp(FIX::Message &message, const FIX::SessionID &sessionID) EXCEPT(FIX::DoNotSend) {}

// Called for incoming administrative messages
// Could validate sequence numbers, handle resend requests, etc.
void Application::fromAdmin(const FIX::Message &message, const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) {}

// ============================================================================
// APPLICATION MESSAGE ROUTING
// ============================================================================
// This method is called for all incoming application messages (not admin).
// The crack() method examines the message type and calls the appropriate
// onMessage() overload based on the FIX version and message type.
//
// DESIGN PATTERN:
// This is the Visitor pattern - the MessageCracker "visits" the message
// and dispatches to the correct handler without needing explicit if/switch.
// ============================================================================
void Application::fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
    EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType) {
  // crack() inspects message type and calls appropriate onMessage() handler
  crack(message, sessionID);
}

// ============================================================================
// FIX 4.0 NEW ORDER HANDLER
// ============================================================================
// Handles new order requests using FIX 4.0 protocol.
//
// FIX 4.0 SPECIFICS:
// - Requires ExecTransType field (removed in later versions)
// - Uses LastShares instead of LastQty
// - Different constructor signature than later versions
//
// VALIDATION:
// Only LIMIT orders are accepted. Market orders would require real-time
// market data and more complex price determination logic.
// ============================================================================
void Application::onMessage(const FIX40::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  // Declare FIX field objects to extract data from the message
  FIX::Symbol symbol;          // Trading instrument (e.g., "AAPL", "MSFT")
  FIX::Side side;             // Buy (1) or Sell (2)
  FIX::OrdType ordType;       // Order type (LIMIT=2, MARKET=1, etc.)
  FIX::OrderQty orderQty;     // Number of shares/contracts
  FIX::Price price;           // Limit price for the order
  FIX::ClOrdID clOrdID;       // Client Order ID (for tracking)
  FIX::Account account;       // Optional account identifier

  // Extract order type first to validate we support it
  message.get(ordType);

  // VALIDATION: Only LIMIT orders supported
  // Real systems would also support MARKET, STOP, STOP_LIMIT, etc.
  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  // Extract all required fields from the incoming order
  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // ========================================================================
  // CREATE EXECUTION REPORT
  // ========================================================================
  // ExecutionReport tells the client what happened to their order.
  // In this case, we're immediately filling the entire order at the
  // requested limit price (instant execution).
  //
  // KEY FIELDS:
  // - OrderID: Exchange-assigned order ID
  // - ExecID: Unique execution ID for this fill
  // - ExecTransType: NEW (FIX 4.0 only, deprecated later)
  // - OrdStatus: FILLED (order completely executed)
  // - Symbol, Side: Echo from original order
  // - OrderQty: Total order size
  // - LastShares: Quantity filled in THIS execution (all of it)
  // - LastPx: Price of THIS execution
  // - CumQty: Cumulative quantity filled (equals OrderQty since fully filled)
  // - AvgPx: Average fill price (equals price since one fill)
  
  FIX40::ExecutionReport executionReport = FIX40::ExecutionReport(
      FIX::OrderID(genOrderID()),                          // Exchange order ID
      FIX::ExecID(genExecID()),                           // Execution ID
      FIX::ExecTransType(FIX::ExecTransType_NEW),         // FIX 4.0 only
      FIX::OrdStatus(FIX::OrdStatus_FILLED),              // Status: FILLED
      symbol,                                              // Instrument
      side,                                                // Buy or Sell
      orderQty,                                            // Order size
      FIX::LastShares(orderQty),                          // Filled quantity (4.0 uses LastShares)
      FIX::LastPx(price),                                 // Execution price
      FIX::CumQty(orderQty),                              // Cumulative filled
      FIX::AvgPx(price));                                 // Average price

  // Echo back the client's order ID so they can match the response
  executionReport.set(clOrdID);

  // If client provided an account number, include it in the response
  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  // Send the execution report back to the client
  // Catch SessionNotFound in case client disconnected
  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// FIX 4.1 NEW ORDER HANDLER
// ============================================================================
// FIX 4.1 CHANGES FROM 4.0:
// - Still requires ExecTransType
// - Added ExecType field to distinguish fill types
// - Added LeavesQty field (quantity remaining unexecuted)
// - Still uses LastShares (not LastQty)
// ============================================================================
void Application::onMessage(const FIX41::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // FIX 4.1 constructor includes ExecType (FILL) and LeavesQty (0 since filled)
  FIX41::ExecutionReport executionReport = FIX41::ExecutionReport(
      FIX::OrderID(genOrderID()),
      FIX::ExecID(genExecID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),
      FIX::ExecType(FIX::ExecType_FILL),                  // FIX 4.1 added ExecType
      FIX::OrdStatus(FIX::OrdStatus_FILLED),
      symbol,
      side,
      orderQty,
      FIX::LastShares(orderQty),
      FIX::LastPx(price),
      FIX::LeavesQty(0),                                   // FIX 4.1 added LeavesQty
      FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  executionReport.set(clOrdID);

  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// FIX 4.2 NEW ORDER HANDLER
// ============================================================================
// FIX 4.2 CHANGES FROM 4.1:
// - ExecTransType still present but deprecated
// - Constructor signature changed: removed OrderQty, LastShares, LastPx
// - These fields now set via .set() method after construction
// - More flexible field ordering
// ============================================================================
void Application::onMessage(const FIX42::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // FIX 4.2 constructor has fewer required fields in constructor
  FIX42::ExecutionReport executionReport = FIX42::ExecutionReport(
      FIX::OrderID(genOrderID()),
      FIX::ExecID(genExecID()),
      FIX::ExecTransType(FIX::ExecTransType_NEW),         // Deprecated but still present
      FIX::ExecType(FIX::ExecType_FILL),
      FIX::OrdStatus(FIX::OrdStatus_FILLED),
      symbol,
      side,
      FIX::LeavesQty(0),
      FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  // Set additional fields after construction (FIX 4.2 style)
  executionReport.set(clOrdID);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastShares(orderQty));         // Still LastShares in 4.2
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// FIX 4.3 NEW ORDER HANDLER
// ============================================================================
// FIX 4.3 CHANGES FROM 4.2:
// - ExecTransType removed from constructor (finally!)
// - Changed from LastShares to LastQty
// - Symbol moved from constructor to .set() method
// - Cleaner, more consistent API
// ============================================================================
void Application::onMessage(const FIX43::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // FIX 4.3 removed ExecTransType, simplified constructor
  FIX43::ExecutionReport executionReport = FIX43::ExecutionReport(
      FIX::OrderID(genOrderID()),
      FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_FILL),
      FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side,
      FIX::LeavesQty(0),
      FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  executionReport.set(clOrdID);
  executionReport.set(symbol);                            // Symbol now set separately
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));           // Changed to LastQty in 4.3
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// FIX 4.4 NEW ORDER HANDLER
// ============================================================================
// FIX 4.4 CHANGES FROM 4.3:
// - ExecType changed from FILL to TRADE (semantic change)
// - FILL meant "completely filled"
// - TRADE means "execution occurred" (more accurate)
// - Otherwise very similar to 4.3
// ============================================================================
void Application::onMessage(const FIX44::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // FIX 4.4 uses TRADE instead of FILL for ExecType
  FIX44::ExecutionReport executionReport = FIX44::ExecutionReport(
      FIX::OrderID(genOrderID()),
      FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_TRADE),                 // Changed to TRADE in 4.4
      FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side,
      FIX::LeavesQty(0),
      FIX::CumQty(orderQty),
      FIX::AvgPx(price));

  executionReport.set(clOrdID);
  executionReport.set(symbol);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));
  executionReport.set(FIX::LastPx(price));

  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}

// ============================================================================
// FIX 5.0 (FIXT 1.1) NEW ORDER HANDLER
// ============================================================================
// FIX 5.0 CHANGES FROM 4.4:
// - Major protocol revision (uses FIXT for transport)
// - AvgPx moved from constructor to .set() method
// - More modular message structure
// - Better support for complex instruments
// ============================================================================
void Application::onMessage(const FIX50::NewOrderSingle &message, const FIX::SessionID &sessionID) {
  FIX::Symbol symbol;
  FIX::Side side;
  FIX::OrdType ordType;
  FIX::OrderQty orderQty;
  FIX::Price price;
  FIX::ClOrdID clOrdID;
  FIX::Account account;

  message.get(ordType);

  if (ordType != FIX::OrdType_LIMIT) {
    throw FIX::IncorrectTagValue(ordType.getTag());
  }

  message.get(symbol);
  message.get(side);
  message.get(orderQty);
  message.get(price);
  message.get(clOrdID);

  // FIX 5.0 removed AvgPx from constructor
  FIX50::ExecutionReport executionReport = FIX50::ExecutionReport(
      FIX::OrderID(genOrderID()),
      FIX::ExecID(genExecID()),
      FIX::ExecType(FIX::ExecType_TRADE),
      FIX::OrdStatus(FIX::OrdStatus_FILLED),
      side,
      FIX::LeavesQty(0),
      FIX::CumQty(orderQty));

  executionReport.set(clOrdID);
  executionReport.set(symbol);
  executionReport.set(orderQty);
  executionReport.set(FIX::LastQty(orderQty));
  executionReport.set(FIX::LastPx(price));
  executionReport.set(FIX::AvgPx(price));                // AvgPx now set separately

  if (message.isSet(account)) {
    executionReport.setField(message.get(account));
  }

  try {
    FIX::Session::sendToTarget(executionReport, sessionID);
  } catch (FIX::SessionNotFound &) {}
}
