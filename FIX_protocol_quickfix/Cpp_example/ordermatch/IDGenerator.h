// ============================================================================
// ID GENERATOR - Unique Identifier Generation
// ============================================================================
// This file provides a simple utility class for generating unique identifiers
// for orders and executions in a trading system.
//
// PURPOSE:
// In FIX protocol, every order needs a unique OrderID (assigned by executor)
// and every execution/fill needs a unique ExecID. This class generates these
// IDs using simple incrementing counters.
//
// PRODUCTION CONSIDERATIONS:
// This simple implementation is NOT suitable for production! Real systems need:
//
// 1. PERSISTENCE: IDs must survive crashes/restarts
//    - Save counter state to database or file
//    - Load counter on startup to avoid duplicates
//
// 2. DISTRIBUTED SYSTEMS: Multiple servers need coordinated IDs
//    - Use distributed ID generation (Snowflake, UUID, etc.)
//    - Or partition ID space per server (e.g., server 1: 1-1M, server 2: 1M-2M)
//
// 3. UNIQUENESS GUARANTEES:
//    - Timestamp-based IDs (milliseconds since epoch)
//    - UUID/GUID for guaranteed uniqueness
//    - Database sequences with gaps
//
// 4. AUDIT REQUIREMENTS:
//    - IDs should be sortable by time
//    - Should encode metadata (server ID, date, sequence)
//    - Example: "20260112-SRV01-000001"
//
// USAGE:
//   IDGenerator gen;
//   std::string orderId = gen.genOrderID();   // "1", "2", "3", ...
//   std::string execId = gen.genExecutionID(); // "1", "2", "3", ...
// ============================================================================

#ifndef ORDERMATCH_IDGENERATOR_H
#define ORDERMATCH_IDGENERATOR_H

#include <sstream>
#include <string>

// ============================================================================
// ID GENERATOR CLASS
// ============================================================================
// Simple counter-based ID generator for orders and executions.
// Maintains two separate counters to distinguish order IDs from execution IDs.
class IDGenerator {
public:
  // Constructor: Initialize both counters to zero
  IDGenerator()
      : m_orderID(0),
        m_executionID(0) {}

  // ========================================================================
  // ORDER ID GENERATION
  // ========================================================================
  // Generates unique Order IDs: "1", "2", "3", ...
  // Called when accepting a new order from a client.
  //
  // THREAD SAFETY WARNING:
  // This is NOT thread-safe! In multi-threaded environment, use:
  // - std::atomic<long> for counters
  // - Or mutex protection around increment and conversion
  //
  // Example:
  //   std::atomic<long> m_orderID;
  //   std::string genOrderID() {
  //     return std::to_string(++m_orderID);
  //   }
  
  std::string genOrderID() { return std::to_string(++m_orderID); }

  // ========================================================================
  // EXECUTION ID GENERATION
  // ========================================================================
  // Generates unique Execution IDs: "1", "2", "3", ...
  // Called for each fill, partial fill, or rejection.
  //
  // WHY SEPARATE FROM ORDER ID?
  // - One order can have multiple executions (partial fills)
  // - Need to track each individual fill for:
  //   * Audit trail
  //   * Settlement
  //   * Cancel/correct specific fills
  //
  // Example scenario:
  //   Order 100 shares: OrderID = "1"
  //   Fill 40 shares:   ExecID = "1"
  //   Fill 60 shares:   ExecID = "2"
  //   Both executions reference OrderID "1"
  
  std::string genExecutionID() { return std::to_string(++m_executionID); }

private:
  // Counter for Order IDs
  // In production: persist to database, use std::atomic, or use UUID
  long m_orderID;
  
  // Counter for Execution IDs
  // In production: must be unique across all servers and restarts
  long m_executionID;
};

#endif
