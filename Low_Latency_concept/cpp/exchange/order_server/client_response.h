#pragma once

#include <sstream>

#include "common/types.h"
#include "common/lf_queue.h"

using namespace Common;

/*
 * CLIENT RESPONSE MESSAGES - EXECUTION REPORTS FROM EXCHANGE
 * ===========================================================
 * 
 * PURPOSE:
 * Defines message structures for execution reports sent by exchange to trading clients.
 * Every order action (accepted, filled, canceled) generates a response message.
 * 
 * MESSAGE FLOW:
 * 1. Matching Engine: Processes order, updates order book
 * 2. Generates MEClientResponse (execution report)
 * 3. Enqueues to order server via lock-free queue
 * 4. Order Server: Adds sequence number (OMClientResponse)
 * 5. Sends to client via TCP
 * 6. Client: Updates local order state
 * 
 * RESPONSE TYPES:
 * - ACCEPTED: Order received and booked
 * - CANCELED: Order removed from book
 * - FILLED: Order matched (full or partial)
 * - CANCEL_REJECTED: Cancel failed (already filled/doesn't exist)
 * 
 * CLIENT STATE MACHINE:
 * ```
 * PENDING (sent NEW)
 *    ↓
 * ACCEPTED (order in book)
 *    ↓
 * FILLED (partial) → More FILLEDs → FILLED (complete)
 * or
 * CANCELED (removed)
 * ```
 * 
 * PERFORMANCE:
 * - Message size: ~45 bytes (packed)
 * - Generation: ~50-100 ns (matching engine)
 * - Queue latency: 10-20 ns (to order server)
 * - TCP send: 1-5 μs (to client)
 * - Total: Client receives response in 1-6 μs
 */

namespace Exchange {
  /*
   * CLIENT RESPONSE TYPE ENUMERATION
   * =================================
   * Identifies the type of execution report.
   * 
   * ACCEPTED: Order successfully added to book
   * - NEW order received and validated
   * - Assigned market_order_id by exchange
   * - Waiting in order book (passive)
   * - Can be filled or canceled later
   * 
   * CANCELED: Order successfully removed
   * - In response to CANCEL request
   * - Or: Expired (time-in-force)
   * - Or: Risk check failed
   * - leaves_qty = 0 (no quantity remaining)
   * 
   * FILLED: Order matched (executed)
   * - Aggressive order crossed spread
   * - Or: Passive order hit by aggressive
   * - exec_qty: How much filled this time
   * - leaves_qty: Remaining quantity (0 if complete)
   * - May receive multiple FILLEDs (partial fills)
   * 
   * CANCEL_REJECTED: Cancel request failed
   * - Order already filled
   * - Order doesn't exist
   * - Wrong client ID (not your order)
   * - Client should update local state (order still active)
   * 
   * PRODUCTION ADDITIONS:
   * - REJECTED: NEW order rejected (validation failed)
   * - REPLACED: Order modified successfully
   * - SUSPENDED: Trading halt, order suspended
   * - EXPIRED: Time-in-force reached
   * 
   * WHY uint8_t?
   * - 1 byte (minimal size)
   * - Network bandwidth savings
   * - Cache-friendly
   */
  enum class ClientResponseType : uint8_t {  // 1 byte enum
    INVALID = 0,          // Uninitialized or error
    ACCEPTED = 1,         // Order accepted into book
    CANCELED = 2,         // Order canceled successfully
    FILLED = 3,           // Order matched (partial or full)
    CANCEL_REJECTED = 4   // Cancel failed
  };

  // Convert enum to string (for logging)
  inline std::string clientResponseTypeToString(ClientResponseType type) {
    switch (type) {
      case ClientResponseType::ACCEPTED:
        return "ACCEPTED";
      case ClientResponseType::CANCELED:
        return "CANCELED";
      case ClientResponseType::FILLED:
        return "FILLED";
      case ClientResponseType::CANCEL_REJECTED:
        return "CANCEL_REJECTED";
      case ClientResponseType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /*
   * PRAGMA PACK - BINARY PROTOCOL COMPATIBILITY
   * See market_update.h for detailed explanation.
   */
#pragma pack(push, 1)  // Begin packed structs

  /*
   * MATCHING ENGINE CLIENT RESPONSE (INTERNAL FORMAT)
   * =================================================
   * Internal format used between matching engine and order server.
   * 
   * FIELDS:
   * - type: ACCEPTED, CANCELED, FILLED, CANCEL_REJECTED
   * - client_id: Which client this response is for
   * - ticker_id: Which instrument
   * - client_order_id: Client's original order ID
   * - market_order_id: Exchange's assigned order ID
   * - side: BUY or SELL
   * - price: Execution price (for FILLED)
   * - exec_qty: Quantity filled (this execution)
   * - leaves_qty: Quantity remaining (0 if fully filled)
   * 
   * ACCEPTED RESPONSE:
   * - type: ACCEPTED
   * - client_order_id: From client's request
   * - market_order_id: Exchange assigns unique ID
   * - price: Order's limit price
   * - leaves_qty: Full order quantity (nothing filled yet)
   * - exec_qty: 0 (no execution yet)
   * 
   * FILLED RESPONSE:
   * - type: FILLED
   * - price: Actual execution price
   * - exec_qty: How much filled (this time)
   * - leaves_qty: Remaining (0 if fully filled)
   * - Example: Order 100 shares, filled 30
   *   - First FILLED: exec_qty=30, leaves_qty=70
   *   - Second FILLED: exec_qty=70, leaves_qty=0
   * 
   * CANCELED RESPONSE:
   * - type: CANCELED
   * - leaves_qty: 0 (order completely removed)
   * - exec_qty: 0 (no execution)
   * 
   * CANCEL_REJECTED RESPONSE:
   * - type: CANCEL_REJECTED
   * - leaves_qty: Original quantity (still in book)
   * - Client should NOT consider order canceled
   * 
   * SIZE: ~45 bytes (packed)
   * - type: 1 byte
   * - client_id: 4 bytes
   * - ticker_id: 4 bytes
   * - client_order_id: 8 bytes
   * - market_order_id: 8 bytes
   * - side: 1 byte
   * - price: 8 bytes
   * - exec_qty: 4 bytes
   * - leaves_qty: 4 bytes
   * - padding: ~3 bytes
   * 
   * EXAMPLE ACCEPTED:
   * ```cpp
   * MEClientResponse resp;
   * resp.type_ = ClientResponseType::ACCEPTED;
   * resp.client_id_ = 42;
   * resp.client_order_id_ = 12345;    // Client's ID
   * resp.market_order_id_ = 9876543;  // Exchange assigns
   * resp.price_ = 15000;              // $150.00
   * resp.leaves_qty_ = 100;           // Full quantity
   * resp.exec_qty_ = 0;               // Nothing filled yet
   * ```
   * 
   * EXAMPLE PARTIAL FILL:
   * ```cpp
   * MEClientResponse resp;
   * resp.type_ = ClientResponseType::FILLED;
   * resp.price_ = 15005;              // $150.05 execution
   * resp.exec_qty_ = 30;              // 30 shares filled
   * resp.leaves_qty_ = 70;            // 70 shares remain
   * ```
   */
  struct MEClientResponse {
    ClientResponseType type_ = ClientResponseType::INVALID;  // Response type
    
    ClientId client_id_ = ClientId_INVALID;              // Target client
    TickerId ticker_id_ = TickerId_INVALID;              // Instrument
    OrderId client_order_id_ = OrderId_INVALID;          // Client's order ID
    OrderId market_order_id_ = OrderId_INVALID;          // Exchange's order ID
    Side side_ = Side::INVALID;                          // BUY or SELL
    Price price_ = Price_INVALID;                        // Execution price
    Qty exec_qty_ = Qty_INVALID;                         // Quantity filled (this time)
    Qty leaves_qty_ = Qty_INVALID;                       // Quantity remaining

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "MEClientResponse"
         << " ["
         << "type:" << clientResponseTypeToString(type_)
         << " client:" << clientIdToString(client_id_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " coid:" << orderIdToString(client_order_id_)
         << " moid:" << orderIdToString(market_order_id_)
         << " side:" << sideToString(side_)
         << " exec_qty:" << qtyToString(exec_qty_)
         << " leaves_qty:" << qtyToString(leaves_qty_)
         << " price:" << priceToString(price_)
         << "]";
      return ss.str();
    }
  };

  /*
   * ORDER MANAGER CLIENT RESPONSE (NETWORK FORMAT)
   * ==============================================
   * External format sent over network to trading clients.
   * Adds sequence number to internal MEClientResponse.
   * 
   * SEQUENCE NUMBER:
   * - Exchange increments for each response (per client)
   * - Allows client to detect dropped TCP packets
   * - Ensures all execution reports received in order
   * 
   * SIZE: ~53 bytes (packed)
   * - seq_num: 8 bytes
   * - me_client_response: ~45 bytes
   * 
   * WIRE PROTOCOL:
   * - Sent over TCP (reliable, ordered)
   * - Binary format (fast, compact)
   * - Little-endian (x86/x64 native)
   * - Client must handle partial fills (multiple FILLEDs)
   */
  struct OMClientResponse {
    size_t seq_num_ = 0;                          // Sequence number
    MEClientResponse me_client_response_;         // Embedded response

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "OMClientResponse"
         << " ["
         << "seq:" << seq_num_
         << " " << me_client_response_.toString()
         << "]";
      return ss.str();
    }
  };

#pragma pack(pop)  // End packed structs

  /*
   * LOCK-FREE QUEUE TYPE DEFINITION
   * ================================
   * 
   * ClientResponseLFQueue:
   * - Matching Engine -> Order Server
   * - Internal format (MEClientResponse)
   * - Capacity: 256K messages
   * - Latency: 10-20 ns enqueue/dequeue
   * 
   * USAGE:
   * ```cpp
   * ClientResponseLFQueue queue(ME_MAX_CLIENT_UPDATES);
   * 
   * // Matching engine (producer)
   * auto* slot = queue.getNextToWriteTo();
   * *slot = response;  // Write response
   * queue.updateWriteIndex();  // Commit
   * 
   * // Order server (consumer)
   * auto* resp = queue.getNextToRead();
   * if (resp) {
   *   send_to_client(*resp);
   *   queue.updateReadIndex();
   * }
   * ```
   */
  typedef LFQueue<MEClientResponse> ClientResponseLFQueue;
}

/*
 * EXECUTION REPORT HANDLING BEST PRACTICES
 * =========================================
 * 
 * 1. ORDER STATE TRACKING (CLIENT-SIDE):
 *    ```cpp
 *    enum OrderState { PENDING, ACTIVE, FILLED, CANCELED };
 *    
 *    void on_response(MEClientResponse resp) {
 *      switch (resp.type_) {
 *        case ACCEPTED:
 *          state = ACTIVE;
 *          market_order_id = resp.market_order_id_;
 *          break;
 *        case FILLED:
 *          filled_qty += resp.exec_qty_;
 *          if (resp.leaves_qty_ == 0) {
 *            state = FILLED;  // Fully filled
 *          }
 *          break;
 *        case CANCELED:
 *          state = CANCELED;
 *          break;
 *        case CANCEL_REJECTED:
 *          // Order still active, don't update state
 *          break;
 *      }
 *    }
 *    ```
 * 
 * 2. PARTIAL FILL HANDLING:
 *    - Multiple FILLED messages possible
 *    - Track cumulative exec_qty
 *    - Check leaves_qty to detect completion
 *    - Update position after each fill
 * 
 * 3. SEQUENCE NUMBER VALIDATION:
 *    - Expect seq 1, 2, 3, ...
 *    - If gap detected: TCP dropped packet (rare but possible)
 *    - Request retransmission or reconnect
 * 
 * 4. MARKET ORDER ID:
 *    - Exchange assigns unique market_order_id
 *    - Use for cancels (more reliable than client_order_id)
 *    - Store mapping: client_order_id -> market_order_id
 * 
 * 5. ERROR RECOVERY:
 *    - CANCEL_REJECTED: Order might have filled, check state
 *    - Connection loss: Reconnect, request order status
 *    - Sequence gap: Request missing messages or full reconciliation
 * 
 * LATENCY BREAKDOWN (EXECUTION REPORT):
 * - Matching engine: 0.05-0.1 μs (generate response)
 * - Queue to order server: 0.01-0.02 μs (lock-free queue)
 * - Order server: 0.1-0.5 μs (sequence, serialize)
 * - Network (LAN): 0.1-1 μs (co-located)
 * - Client receive: 0.1-1 μs (TCP + deserialize)
 * - TOTAL: 0.5-3 μs (matching to client notification)
 * 
 * AUDIT TRAIL:
 * - Log every execution report
 * - Include: Timestamp, client, order IDs, price, quantity
 * - Regulatory requirement (MiFID II, SEC CAT)
 * - Microsecond timestamps mandatory
 * 
 * PRODUCTION ENHANCEMENTS:
 * - REJECTED response (order validation failed)
 * - Execution report details (contra party, fees)
 * - Multi-leg execution (spreads, options strategies)
 * - FIX protocol support (ExecutionReport message)
 * - Drop copy (send reports to clearing firm)
 */
