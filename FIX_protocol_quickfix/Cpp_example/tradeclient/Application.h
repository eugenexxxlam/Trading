// ============================================================================
// TRADE CLIENT APPLICATION HEADER
// ============================================================================
// This file defines the Application class for a FIX Protocol Trading Client.
//
// PURPOSE:
// A trading client (initiator) that connects to a FIX server and allows users
// to interactively send orders, cancel orders, replace orders, and request
// market data through a command-line interface.
//
// KEY DIFFERENCES FROM EXECUTOR:
// - Client (Initiator) vs Server (Acceptor)
// - Interactive user interface (REPL - Read-Eval-Print Loop)
// - Sends orders instead of receiving them
// - Handles ExecutionReports instead of generating them
//
// SUPPORTED OPERATIONS:
// 1. Enter new orders (NewOrderSingle)
// 2. Cancel orders (OrderCancelRequest)
// 3. Replace orders (OrderCancelReplaceRequest)
// 4. Request market data (MarketDataRequest)
//
// USER WORKFLOW:
// 1. Application connects to executor
// 2. User selects action from menu
// 3. User enters order details interactively
// 4. Application sends FIX message
// 5. Application receives and displays ExecutionReport
//
// PRODUCTION USE CASES:
// - Trading desk application
// - Algorithmic trading client
// - Order management system (OMS) frontend
// - Market data subscriber
// ============================================================================

#ifndef TRADECLIENT_APPLICATION_H
#define TRADECLIENT_APPLICATION_H

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Mutex.h"
#include "quickfix/Values.h"

// ============================================================================
// MESSAGE TYPE IMPORTS - All supported FIX versions
// ============================================================================
// The client supports FIX 4.0 through 5.0, allowing it to connect to
// executors running any of these protocol versions.
//
// For each version, we import:
// - ExecutionReport: Response from executor about order status
// - NewOrderSingle: Message to submit new orders
// - OrderCancelRequest: Message to cancel existing orders
// - OrderCancelReplaceRequest: Message to modify existing orders
// - OrderCancelReject: Rejection of cancel/replace request
// - MarketDataRequest: Request for market data (FIX 4.3+)
// ============================================================================

// FIX 4.0 message types
#include "quickfix/fix40/ExecutionReport.h"
#include "quickfix/fix40/NewOrderSingle.h"
#include "quickfix/fix40/OrderCancelReject.h"
#include "quickfix/fix40/OrderCancelReplaceRequest.h"
#include "quickfix/fix40/OrderCancelRequest.h"

// FIX 4.1 message types
#include "quickfix/fix41/ExecutionReport.h"
#include "quickfix/fix41/NewOrderSingle.h"
#include "quickfix/fix41/OrderCancelReject.h"
#include "quickfix/fix41/OrderCancelReplaceRequest.h"
#include "quickfix/fix41/OrderCancelRequest.h"

// FIX 4.2 message types
#include "quickfix/fix42/ExecutionReport.h"
#include "quickfix/fix42/NewOrderSingle.h"
#include "quickfix/fix42/OrderCancelReject.h"
#include "quickfix/fix42/OrderCancelReplaceRequest.h"
#include "quickfix/fix42/OrderCancelRequest.h"

// FIX 4.3 message types (added MarketDataRequest)
#include "quickfix/fix43/ExecutionReport.h"
#include "quickfix/fix43/MarketDataRequest.h"
#include "quickfix/fix43/NewOrderSingle.h"
#include "quickfix/fix43/OrderCancelReject.h"
#include "quickfix/fix43/OrderCancelReplaceRequest.h"
#include "quickfix/fix43/OrderCancelRequest.h"

// FIX 4.4 message types
#include "quickfix/fix44/ExecutionReport.h"
#include "quickfix/fix44/MarketDataRequest.h"
#include "quickfix/fix44/NewOrderSingle.h"
#include "quickfix/fix44/OrderCancelReject.h"
#include "quickfix/fix44/OrderCancelReplaceRequest.h"
#include "quickfix/fix44/OrderCancelRequest.h"

// FIX 5.0 (FIXT) message types
#include "quickfix/fix50/ExecutionReport.h"
#include "quickfix/fix50/MarketDataRequest.h"
#include "quickfix/fix50/NewOrderSingle.h"
#include "quickfix/fix50/OrderCancelReject.h"
#include "quickfix/fix50/OrderCancelReplaceRequest.h"
#include "quickfix/fix50/OrderCancelRequest.h"

#include <queue>

// ============================================================================
// TRADE CLIENT APPLICATION CLASS
// ============================================================================
// Interactive FIX trading client with command-line interface.
//
// DESIGN PATTERN:
// - Uses MessageCracker for automatic message routing
// - Interactive menu system for user input
// - Version-aware message creation
// ============================================================================
class Application : public FIX::Application, public FIX::MessageCracker {
public:
  // Main interactive loop - called after connection established
  void run();

private:
  // ========================================================================
  // LIFECYCLE CALLBACKS
  // ========================================================================
  // These are called by QuickFIX engine during session lifecycle
  
  void onCreate(const FIX::SessionID &) {}        // Session created (before logon)
  void onLogon(const FIX::SessionID &sessionID);  // Successfully logged on
  void onLogout(const FIX::SessionID &sessionID); // Logged out or disconnected
  
  // ========================================================================
  // OUTBOUND MESSAGE CALLBACKS
  // ========================================================================
  
  // Called before admin messages sent (Logon, Heartbeat, etc.)
  void toAdmin(FIX::Message &, const FIX::SessionID &) {}
  
  // Called before application messages sent
  // Used to filter duplicate messages (PossDupFlag check)
  void toApp(FIX::Message &, const FIX::SessionID &) EXCEPT(FIX::DoNotSend);
  
  // ========================================================================
  // INBOUND MESSAGE CALLBACKS
  // ========================================================================
  
  // Receive administrative messages
  void fromAdmin(const FIX::Message &, const FIX::SessionID &)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon) {}
  
  // Receive application messages - routes to specific handlers via crack()
  void fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType);

  // ========================================================================
  // MESSAGE HANDLERS - ExecutionReport (all versions)
  // ========================================================================
  // ExecutionReports tell us what happened to our orders:
  // - NEW: Order accepted by executor
  // - PARTIALLY_FILLED: Part of order executed
  // - FILLED: Entire order executed
  // - CANCELED: Order canceled
  // - REJECTED: Order rejected (invalid, risk limit, etc.)
  //
  // For this simple client, we just print them. In production:
  // - Update order book / position tracking
  // - Trigger alerts / notifications
  // - Update risk calculations
  // - Record for compliance / auditing
  
  void onMessage(const FIX40::ExecutionReport &, const FIX::SessionID &);
  void onMessage(const FIX41::ExecutionReport &, const FIX::SessionID &);
  void onMessage(const FIX42::ExecutionReport &, const FIX::SessionID &);
  void onMessage(const FIX43::ExecutionReport &, const FIX::SessionID &);
  void onMessage(const FIX44::ExecutionReport &, const FIX::SessionID &);
  void onMessage(const FIX50::ExecutionReport &, const FIX::SessionID &);

  // ========================================================================
  // MESSAGE HANDLERS - OrderCancelReject (all versions)
  // ========================================================================
  // Received when a cancel or replace request is rejected.
  // Common reasons:
  // - Order already filled
  // - Order doesn't exist
  // - Order already canceled
  // - Too late to modify (market order already executing)
  
  void onMessage(const FIX40::OrderCancelReject &, const FIX::SessionID &);
  void onMessage(const FIX41::OrderCancelReject &, const FIX::SessionID &);
  void onMessage(const FIX42::OrderCancelReject &, const FIX::SessionID &);
  void onMessage(const FIX43::OrderCancelReject &, const FIX::SessionID &);
  void onMessage(const FIX44::OrderCancelReject &, const FIX::SessionID &);
  void onMessage(const FIX50::OrderCancelReject &, const FIX::SessionID &);

  // ========================================================================
  // USER INTERACTION METHODS
  // ========================================================================
  // These methods prompt the user for input and send corresponding messages
  
  void queryEnterOrder();           // Create and send NewOrderSingle
  void queryCancelOrder();          // Create and send OrderCancelRequest
  void queryReplaceOrder();         // Create and send OrderCancelReplaceRequest
  void queryMarketDataRequest();    // Create and send MarketDataRequest (4.3+)

  // ========================================================================
  // VERSION-SPECIFIC ORDER CREATION
  // ========================================================================
  // Each FIX version has different required fields and constructors.
  // These methods prompt for the appropriate fields and construct
  // version-specific messages.
  
  // NewOrderSingle constructors for each FIX version
  FIX40::NewOrderSingle queryNewOrderSingle40();
  FIX41::NewOrderSingle queryNewOrderSingle41();
  FIX42::NewOrderSingle queryNewOrderSingle42();
  FIX43::NewOrderSingle queryNewOrderSingle43();
  FIX44::NewOrderSingle queryNewOrderSingle44();
  FIX50::NewOrderSingle queryNewOrderSingle50();
  
  // OrderCancelRequest constructors for each FIX version
  FIX40::OrderCancelRequest queryOrderCancelRequest40();
  FIX41::OrderCancelRequest queryOrderCancelRequest41();
  FIX42::OrderCancelRequest queryOrderCancelRequest42();
  FIX43::OrderCancelRequest queryOrderCancelRequest43();
  FIX44::OrderCancelRequest queryOrderCancelRequest44();
  FIX50::OrderCancelRequest queryOrderCancelRequest50();
  
  // OrderCancelReplaceRequest constructors for each FIX version
  FIX40::OrderCancelReplaceRequest queryCancelReplaceRequest40();
  FIX41::OrderCancelReplaceRequest queryCancelReplaceRequest41();
  FIX42::OrderCancelReplaceRequest queryCancelReplaceRequest42();
  FIX43::OrderCancelReplaceRequest queryCancelReplaceRequest43();
  FIX44::OrderCancelReplaceRequest queryCancelReplaceRequest44();
  FIX50::OrderCancelReplaceRequest queryCancelReplaceRequest50();
  
  // MarketDataRequest constructors (FIX 4.3+ only)
  FIX43::MarketDataRequest queryMarketDataRequest43();
  FIX44::MarketDataRequest queryMarketDataRequest44();
  FIX50::MarketDataRequest queryMarketDataRequest50();

  // ========================================================================
  // INPUT HELPER METHODS
  // ========================================================================
  // These methods prompt the user and read field values from stdin.
  // They return FIX field objects that can be used in message construction.
  
  void queryHeader(FIX::Header &header);        // Prompt for routing fields
  char queryAction();                            // Prompt for menu choice
  int queryVersion();                            // Prompt for FIX version
  bool queryConfirm(const std::string &query);  // Yes/no confirmation

  // Prompt for specific FIX fields
  FIX::SenderCompID querySenderCompID();        // Who is sending (us)
  FIX::TargetCompID queryTargetCompID();        // Who to send to (executor)
  FIX::TargetSubID queryTargetSubID();          // Optional routing sub-ID
  FIX::ClOrdID queryClOrdID();                  // Client Order ID (unique per order)
  FIX::OrigClOrdID queryOrigClOrdID();          // Original Order ID (for cancel/replace)
  FIX::Symbol querySymbol();                    // Trading instrument (e.g., "AAPL")
  FIX::Side querySide();                        // Buy or Sell
  FIX::OrderQty queryOrderQty();                // Quantity (number of shares)
  FIX::OrdType queryOrdType();                  // Order type (Market, Limit, Stop, etc.)
  FIX::Price queryPrice();                      // Limit price (for limit orders)
  FIX::StopPx queryStopPx();                    // Stop price (for stop orders)
  FIX::TimeInForce queryTimeInForce();          // How long order is valid
};

#endif
