// ============================================================================
// EXECUTOR APPLICATION HEADER
// ============================================================================
// This file defines the Application class for a FIX Protocol Executor.
//
// PURPOSE:
// An "Executor" in FIX terminology is a component that receives orders from
// clients and executes them (fills them immediately). This is a simplified
// trading system that acts as the server-side order execution engine.
//
// KEY CONCEPTS:
// 1. FIX::Application - Base interface for all QuickFIX applications
// 2. FIX::MessageCracker - Utility that routes messages to type-specific handlers
// 3. Multi-version support - Handles FIX 4.0, 4.1, 4.2, 4.3, 4.4, and 5.0
//
// WORKFLOW:
// 1. Client sends NewOrderSingle message
// 2. Executor validates the order (only LIMIT orders supported)
// 3. Executor immediately fills the order
// 4. Executor sends ExecutionReport back to client
//
// PRODUCTION CONSIDERATIONS:
// - Real executors would check available liquidity
// - Would implement risk management and validation
// - Would support multiple order types (market, stop, etc.)
// - Would handle partial fills and order queuing
// ============================================================================

#ifndef EXECUTOR_APPLICATION_H
#define EXECUTOR_APPLICATION_H

#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/Mutex.h"
#include "quickfix/Utility.h"
#include "quickfix/Values.h"

// Include NewOrderSingle message definitions for all supported FIX versions
// Each version has slightly different required fields and structure
#include "quickfix/fix40/NewOrderSingle.h"
#include "quickfix/fix41/NewOrderSingle.h"
#include "quickfix/fix42/NewOrderSingle.h"
#include "quickfix/fix43/NewOrderSingle.h"
#include "quickfix/fix44/NewOrderSingle.h"
#include "quickfix/fix50/NewOrderSingle.h"

// ============================================================================
// APPLICATION CLASS
// ============================================================================
// This class implements the FIX Application interface and uses MessageCracker
// to automatically route incoming messages to the appropriate handler methods.
//
// INHERITANCE:
// - FIX::Application: Core lifecycle callbacks (onCreate, onLogon, etc.)
// - FIX::MessageCracker: Message type routing (calls onMessage() variants)
//
// DESIGN PATTERN:
// This uses the "Visitor" pattern via MessageCracker, which inspects the
// message type and calls the appropriate onMessage() overload.
// ============================================================================
class Application : public FIX::Application, public FIX::MessageCracker {
public:
  // Constructor initializes order and execution ID counters
  // In production, these would be persisted to survive restarts
  Application()
      : m_orderID(0),
        m_execID(0) {}

  // ========================================================================
  // APPLICATION LIFECYCLE CALLBACKS
  // ========================================================================
  // These methods are called by the QuickFIX engine at various points in
  // the session lifecycle. They provide hooks for custom application logic.
  
  // Called when a session is created (before logon)
  // Use this to initialize session-specific resources
  void onCreate(const FIX::SessionID &);
  
  // Called when a FIX session successfully logs on
  // This is when you can start accepting orders
  void onLogon(const FIX::SessionID &sessionID);
  
  // Called when a FIX session logs out
  // Use this to clean up resources and handle open orders
  void onLogout(const FIX::SessionID &sessionID);
  
  // ========================================================================
  // OUTBOUND MESSAGE CALLBACKS
  // ========================================================================
  
  // Called for administrative messages before they're sent (Logon, Heartbeat, etc.)
  // Use this to add custom fields to admin messages
  void toAdmin(FIX::Message &, const FIX::SessionID &);
  
  // Called for application messages before they're sent
  // Use this to validate or modify outgoing orders/executions
  // Can throw FIX::DoNotSend to prevent message from being sent
  void toApp(FIX::Message &, const FIX::SessionID &) EXCEPT(FIX::DoNotSend);
  
  // ========================================================================
  // INBOUND MESSAGE CALLBACKS
  // ========================================================================
  
  // Called when administrative messages are received
  // Use this to handle custom admin message logic
  // Can throw various exceptions to reject the message
  void fromAdmin(const FIX::Message &, const FIX::SessionID &)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::RejectLogon);
  
  // Called when application messages are received
  // This typically calls crack() to route to specific onMessage() handlers
  void fromApp(const FIX::Message &message, const FIX::SessionID &sessionID)
      EXCEPT(FIX::FieldNotFound, FIX::IncorrectDataFormat, FIX::IncorrectTagValue, FIX::UnsupportedMessageType);

  // ========================================================================
  // MESSAGE-SPECIFIC HANDLERS (MessageCracker pattern)
  // ========================================================================
  // These methods are called automatically by MessageCracker.crack() when
  // a message of the corresponding type is received.
  //
  // WHY MULTIPLE VERSIONS?
  // Each FIX version has slightly different required fields:
  // - FIX 4.0: ExecTransType field (deprecated in later versions)
  // - FIX 4.1: Added LeavesQty field
  // - FIX 4.2+: Changed field requirements and ordering
  // - FIX 4.3+: Symbol field became optional in some contexts
  // - FIX 4.4+: Changed ExecType values (TRADE vs FILL)
  // - FIX 5.0: Major restructuring with component blocks
  
  void onMessage(const FIX40::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX41::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX42::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX43::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX44::NewOrderSingle &, const FIX::SessionID &);
  void onMessage(const FIX50::NewOrderSingle &, const FIX::SessionID &);

  // ========================================================================
  // ID GENERATION
  // ========================================================================
  // These methods generate unique IDs for orders and executions.
  //
  // PRODUCTION CONSIDERATIONS:
  // - In real systems, these would be distributed ID generators
  // - Must be unique across all sessions and system restarts
  // - Often use timestamps + sequence numbers
  // - May incorporate server ID for distributed systems
  
  std::string genOrderID() { return std::to_string(++m_orderID); }
  std::string genExecID() { return std::to_string(++m_execID); }

private:
  // Simple counters for generating unique IDs
  // In production, these would be persisted and cluster-aware
  int m_orderID, m_execID;
};

#endif
