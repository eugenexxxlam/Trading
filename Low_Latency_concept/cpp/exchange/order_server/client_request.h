#pragma once

#include <sstream>

#include "common/types.h"
#include "common/lf_queue.h"

using namespace Common;

/*
 * CLIENT REQUEST MESSAGES - ORDER INSTRUCTIONS FROM TRADERS
 * ==========================================================
 * 
 * PURPOSE:
 * Defines message structures for orders sent by trading clients to the exchange.
 * Clients send NEW orders and CANCEL requests via TCP to the order server.
 * 
 * MESSAGE FLOW:
 * 1. Trading Client: Generates order (new/cancel)
 * 2. Sends OMClientRequest over TCP (with sequence number)
 * 3. Order Server: Receives, validates
 * 4. Converts to MEClientRequest (internal format)
 * 5. Enqueues to matching engine via lock-free queue
 * 6. Matching Engine: Processes order
 * 
 * REQUEST TYPES:
 * - NEW: Submit new order to order book
 * - CANCEL: Remove existing order from book
 * - (Could add MODIFY: Change price/quantity)
 * 
 * VALIDATION (Order Server):
 * - Valid client ID
 * - Valid instrument (ticker exists)
 * - Positive price and quantity
 * - Order ID not duplicate
 * - Risk checks (position limits)
 * 
 * PERFORMANCE:
 * - Message size: ~30 bytes (packed)
 * - Receive latency: 1-5 μs (TCP + processing)
 * - Queue latency: 10-20 ns (to matching engine)
 * - Total: Order reaches matching engine in 1-5 μs
 */

namespace Exchange {
  /*
   * CLIENT REQUEST TYPE ENUMERATION
   * ================================
   * Identifies the type of order instruction.
   * 
   * NEW: Submit new order
   * - Limit order: Buy/sell at specific price or better
   * - Adds liquidity (if not immediately matchable)
   * - Or takes liquidity (if crosses spread)
   * - Generates ACCEPTED or FILLED response
   * 
   * CANCEL: Cancel existing order
   * - Remove order from book
   * - Must specify same client ID + order ID
   * - Generates CANCELED or CANCEL_REJECTED response
   * - Rejection: Order already filled, or doesn't exist
   * 
   * PRODUCTION ADDITIONS:
   * - MODIFY: Change price/quantity (cancel/replace)
   * - MARKET: Execute at best available price
   * - STOP: Conditional order (activate at trigger price)
   * - IOC: Immediate-or-cancel (fill immediately or cancel)
   * - FOK: Fill-or-kill (all-or-nothing)
   * 
   * WHY uint8_t?
   * - 1 byte (minimal size)
   * - Sufficient for all order types
   * - Network bandwidth savings
   */
  enum class ClientRequestType : uint8_t {  // 1 byte enum
    INVALID = 0,  // Uninitialized or error
    NEW = 1,      // New order submission
    CANCEL = 2    // Cancel existing order
  };

  // Convert enum to string (for logging)
  inline std::string clientRequestTypeToString(ClientRequestType type) {
    switch (type) {
      case ClientRequestType::NEW:
        return "NEW";
      case ClientRequestType::CANCEL:
        return "CANCEL";
      case ClientRequestType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /*
   * PRAGMA PACK - ENSURE BINARY PROTOCOL COMPATIBILITY
   * ===================================================
   * See market_update.h for detailed explanation of #pragma pack.
   * 
   * Summary: Removes padding, creates compact binary wire format.
   * Critical for: Network efficiency, cross-platform compatibility.
   */
#pragma pack(push, 1)  // Begin packed structs (1-byte alignment, no padding)

  /*
   * MATCHING ENGINE CLIENT REQUEST (INTERNAL FORMAT)
   * ================================================
   * Internal format used between order server and matching engine.
   * 
   * FIELDS:
   * - type: NEW or CANCEL
   * - client_id: Which trading firm/trader
   * - ticker_id: Which instrument (AAPL, MSFT, etc.)
   * - order_id: Client's order identifier
   * - side: BUY or SELL
   * - price: Limit price (fixed-point integer)
   * - qty: Order quantity (shares/contracts)
   * 
   * NEW ORDER FIELDS:
   * - All fields required
   * - price: Must be positive, valid tick size
   * - qty: Must be positive, within limits
   * 
   * CANCEL FIELDS:
   * - type: CANCEL
   * - client_id: Must match original order
   * - ticker_id: Must match
   * - order_id: Must match
   * - side, price, qty: Ignored (can be 0/INVALID)
   * 
   * SIZE: ~30 bytes (packed)
   * - type: 1 byte
   * - client_id: 4 bytes
   * - ticker_id: 4 bytes
   * - order_id: 8 bytes
   * - side: 1 byte
   * - price: 8 bytes
   * - qty: 4 bytes
   * 
   * EXAMPLE NEW ORDER:
   * ```cpp
   * MEClientRequest req;
   * req.type_ = ClientRequestType::NEW;
   * req.client_id_ = 42;        // Client 42
   * req.ticker_id_ = 0;         // AAPL
   * req.order_id_ = 12345;      // Client's order ID
   * req.side_ = Side::BUY;      // Buy order
   * req.price_ = 15000;         // $150.00 (assuming 2 decimals)
   * req.qty_ = 100;             // 100 shares
   * ```
   * 
   * EXAMPLE CANCEL:
   * ```cpp
   * MEClientRequest req;
   * req.type_ = ClientRequestType::CANCEL;
   * req.client_id_ = 42;
   * req.order_id_ = 12345;      // Cancel order 12345
   * // Other fields can be INVALID
   * ```
   */
  struct MEClientRequest {
    ClientRequestType type_ = ClientRequestType::INVALID;  // NEW or CANCEL
    
    ClientId client_id_ = ClientId_INVALID;    // Trading client ID
    TickerId ticker_id_ = TickerId_INVALID;    // Instrument ID
    OrderId order_id_ = OrderId_INVALID;       // Client's order ID
    Side side_ = Side::INVALID;                // BUY or SELL
    Price price_ = Price_INVALID;              // Limit price (fixed-point)
    Qty qty_ = Qty_INVALID;                    // Order quantity

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "MEClientRequest"
         << " ["
         << "type:" << clientRequestTypeToString(type_)
         << " client:" << clientIdToString(client_id_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " oid:" << orderIdToString(order_id_)
         << " side:" << sideToString(side_)
         << " qty:" << qtyToString(qty_)
         << " price:" << priceToString(price_)
         << "]";
      return ss.str();
    }
  };

  /*
   * ORDER MANAGER CLIENT REQUEST (NETWORK FORMAT)
   * =============================================
   * External format sent over network by trading clients.
   * Adds sequence number to internal MEClientRequest.
   * 
   * SEQUENCE NUMBER:
   * - Client increments for each message (1, 2, 3, ...)
   * - Exchange validates: Detect dropped messages, replay attacks
   * - Reject if: Gap detected (missing messages), duplicate seq number
   * 
   * PURPOSE:
   * - Reliable TCP delivery: Detect missing messages
   * - Order flow audit trail
   * - Replay protection (prevent duplicate orders)
   * - Debugging (trace message flow)
   * 
   * SIZE: ~38 bytes (packed)
   * - seq_num: 8 bytes (size_t)
   * - me_client_request: ~30 bytes
   * 
   * WIRE PROTOCOL:
   * - Sent over TCP (reliable, ordered delivery)
   * - Binary format (not FIX, not JSON)
   * - Little-endian (x86/x64 native)
   * - Packed (no padding between fields)
   * 
   * CLIENT-SIDE:
   * ```cpp
   * OMClientRequest request;
   * request.seq_num_ = next_seq++;  // Increment sequence
   * request.me_client_request_ = /* fill order details */;
   * send(socket, &request, sizeof(request), 0);  // TCP send
   * ```
   * 
   * SERVER-SIDE:
   * ```cpp
   * OMClientRequest request;
   * recv(socket, &request, sizeof(request), 0);  // TCP receive
   * if (request.seq_num_ != expected_seq) {
   *   // Gap detected or duplicate
   * }
   * // Forward me_client_request_ to matching engine
   * ```
   */
  struct OMClientRequest {
    size_t seq_num_ = 0;                      // Sequence number (monotonic)
    MEClientRequest me_client_request_;       // Embedded client request

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "OMClientRequest"
         << " ["
         << "seq:" << seq_num_
         << " " << me_client_request_.toString()
         << "]";
      return ss.str();
    }
  };

#pragma pack(pop)  // End packed struct section (restore default alignment)

  /*
   * LOCK-FREE QUEUE TYPE DEFINITION
   * ================================
   * 
   * ClientRequestLFQueue:
   * - Order Server -> Matching Engine
   * - Internal format (MEClientRequest, no sequence numbers)
   * - Capacity: 256K messages (ME_MAX_CLIENT_UPDATES)
   * - Latency: 10-20 ns per enqueue/dequeue
   * - Throughput: 50M+ messages/second
   * 
   * USAGE:
   * ```cpp
   * ClientRequestLFQueue queue(ME_MAX_CLIENT_UPDATES);
   * 
   * // Order server (producer)
   * auto* slot = queue.getNextToWriteTo();
   * *slot = request;  // Write request
   * queue.updateWriteIndex();  // Commit
   * 
   * // Matching engine (consumer)
   * auto* req = queue.getNextToRead();
   * if (req) {
   *   process_order(*req);
   *   queue.updateReadIndex();
   * }
   * ```
   */
  typedef LFQueue<MEClientRequest> ClientRequestLFQueue;
}

/*
 * ORDER SUBMISSION BEST PRACTICES
 * ================================
 * 
 * 1. VALIDATION (CLIENT-SIDE):
 *    - Validate before sending (reduce rejections)
 *    - Check: Positive price/qty, valid ticker, proper side
 *    - Risk checks: Position limits, order size limits
 * 
 * 2. ORDER ID MANAGEMENT:
 *    - Client assigns order_id (unique per client)
 *    - Typical: Monotonically increasing (1, 2, 3, ...)
 *    - Or: Timestamp + counter for uniqueness
 *    - Never reuse until confirmed filled/canceled
 * 
 * 3. SEQUENCE NUMBERS:
 *    - Start from 1, increment for each message
 *    - Validate on server: Reject gaps or duplicates
 *    - Reset on reconnect (session-based)
 * 
 * 4. ERROR HANDLING:
 *    - TCP disconnect: Reconnect, resync state
 *    - Timeout: Cancel unacknowledged orders
 *    - Rejection: Log reason, don't retry blindly
 * 
 * 5. PERFORMANCE:
 *    - Batch orders when possible (reduce TCP overhead)
 *    - Non-blocking send (don't wait for ack)
 *    - Pre-validate (avoid round-trip for rejections)
 *    - Reuse connections (TCP setup is expensive)
 * 
 * LATENCY BREAKDOWN (ORDER SUBMISSION):
 * - Client send: 0.1-1 μs (local processing)
 * - Network (LAN): 0.1-1 μs (co-located)
 * - Server receive: 1-5 μs (TCP + validation)
 * - Queue to matching: 0.01-0.02 μs (lock-free queue)
 * - Matching: 0.2-0.5 μs (order book processing)
 * - TOTAL: 1-8 μs (co-located, LAN)
 * 
 * PRODUCTION ENHANCEMENTS:
 * - Pre-trade risk checks (order server)
 * - Rate limiting (per client)
 * - Order types (market, stop, IOC, FOK)
 * - Time-in-force (DAY, GTC, IOC, FOK)
 * - FIX protocol support (industry standard)
 * - Encryption (TLS for security)
 * - Authentication (API keys, certs)
 */
