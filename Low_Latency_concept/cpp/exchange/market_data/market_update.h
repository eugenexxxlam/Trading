#pragma once

#include <sstream>

#include "common/types.h"
#include "common/lf_queue.h"

using namespace Common;

/*
 * MARKET UPDATE MESSAGES - ORDER BOOK CHANGE NOTIFICATIONS
 * =========================================================
 * 
 * PURPOSE:
 * Defines message structures for broadcasting order book changes to market data subscribers.
 * Every order book event (add, cancel, modify, trade) generates a market update message.
 * 
 * MESSAGE FLOW:
 * 1. Matching Engine: Processes order, updates order book
 * 2. Generates MEMarketUpdate: Internal format
 * 3. Enqueues to market data publisher
 * 4. Publisher adds sequence number: Creates MDPMarketUpdate
 * 5. Broadcasts via multicast UDP to all subscribers
 * 
 * SUBSCRIBERS:
 * - Trading firms (maintain local order book copy)
 * - Market makers (adjust quotes based on book changes)
 * - Arbitrageurs (detect price discrepancies)
 * - Data vendors (Bloomberg, Reuters)
 * - Regulators (monitor market activity)
 * 
 * PERFORMANCE:
 * - Message size: ~40 bytes (packed struct)
 * - Generation: ~50-100 ns (matching engine)
 * - Transmission: ~5-50 μs (multicast latency)
 * - Rate: 1M+ messages/second (high activity)
 * 
 * RELIABILITY:
 * - Sequence numbers: Detect gaps (lost packets)
 * - Snapshots: Periodic full order book state
 * - Retransmission: Request missing messages via separate channel
 */

namespace Exchange {
  /*
   * MARKET UPDATE TYPE ENUMERATION
   * ===============================
   * Identifies the type of order book change.
   * 
   * UPDATE TYPES:
   * 
   * CLEAR: Clear all orders (start of day, halt, emergency)
   * - Removes all orders from book
   * - Rare: Trading halt, system restart
   * 
   * ADD: New order added to book
   * - Passive order (limit order at specific price)
   * - Increases liquidity at that price level
   * - Most common message type
   * 
   * MODIFY: Order quantity changed
   * - Partial fill (quantity reduced)
   * - Self-trade prevention (quantity adjusted)
   * - Priority may change (depends on exchange rules)
   * 
   * CANCEL: Order removed from book
   * - Client canceled
   * - Expired (time-in-force)
   * - Risk check failed
   * - Reduces liquidity
   * 
   * TRADE: Order matched (filled)
   * - Shows executed price and quantity
   * - Aggressive order crossed spread
   * - Two orders involved (aggressor + passive)
   * 
   * SNAPSHOT_START: Beginning of full book snapshot
   * - Followed by sequence of ADD messages
   * - Allows late joiners to rebuild book
   * - Sent periodically (e.g., every 1 second)
   * 
   * SNAPSHOT_END: End of snapshot sequence
   * - Marks completion of snapshot
   * - Resume processing incremental updates
   * 
   * WHY uint8_t?
   * - 1 byte (vs 4 bytes for int)
   * - Saves network bandwidth (millions of messages)
   * - Cache-friendly (more messages per cache line)
   */
  enum class MarketUpdateType : uint8_t {  // 1 byte enum
    INVALID = 0,         // Uninitialized or error
    CLEAR = 1,           // Clear order book
    ADD = 2,             // Add new order
    MODIFY = 3,          // Modify existing order
    CANCEL = 4,          // Cancel order
    TRADE = 5,           // Order matched/filled
    SNAPSHOT_START = 6,  // Begin snapshot sequence
    SNAPSHOT_END = 7     // End snapshot sequence
  };

  // Convert enum to string (for logging, debugging)
  inline std::string marketUpdateTypeToString(MarketUpdateType type) {
    switch (type) {
      case MarketUpdateType::CLEAR:
        return "CLEAR";
      case MarketUpdateType::ADD:
        return "ADD";
      case MarketUpdateType::MODIFY:
        return "MODIFY";
      case MarketUpdateType::CANCEL:
        return "CANCEL";
      case MarketUpdateType::TRADE:
        return "TRADE";
      case MarketUpdateType::SNAPSHOT_START:
        return "SNAPSHOT_START";
      case MarketUpdateType::SNAPSHOT_END:
        return "SNAPSHOT_END";
      case MarketUpdateType::INVALID:
        return "INVALID";
    }
    return "UNKNOWN";
  }

  /*
   * PRAGMA PACK - BINARY PROTOCOL OPTIMIZATION
   * ===========================================
   * 
   * #pragma pack(push, 1):
   * - Removes padding between struct members
   * - Ensures exact memory layout (byte-aligned)
   * - Critical for network protocols (binary compatibility)
   * 
   * WITHOUT PACKING:
   * struct Example {
   *   uint8_t a;   // 1 byte
   *   // 3 bytes padding (alignment)
   *   uint32_t b;  // 4 bytes
   * }; // Total: 8 bytes
   * 
   * WITH PACKING (#pragma pack(1)):
   * struct Example {
   *   uint8_t a;   // 1 byte
   *   uint32_t b;  // 4 bytes (no padding)
   * }; // Total: 5 bytes
   * 
   * WHY PACK?
   * - Smaller messages (less bandwidth)
   * - Exact wire format (cross-platform compatibility)
   * - More messages per packet
   * - Industry standard (FIX, ITCH, OUCH protocols)
   * 
   * TRADE-OFF:
   * - Smaller size vs unaligned access (slightly slower on some CPUs)
   * - Acceptable: Network I/O dominates, size matters more
   */
#pragma pack(push, 1)  // Begin packed struct section (1-byte alignment)

  /*
   * MATCHING ENGINE MARKET UPDATE (INTERNAL FORMAT)
   * ===============================================
   * Internal format used by matching engine and market data publisher.
   * Not sent over network directly (use MDPMarketUpdate for that).
   * 
   * FIELDS:
   * - type: What happened (ADD, CANCEL, TRADE, etc.)
   * - order_id: Unique order identifier
   * - ticker_id: Which instrument (0=AAPL, 1=MSFT, etc.)
   * - side: BUY or SELL
   * - price: Order price (fixed-point integer)
   * - qty: Order quantity (shares/contracts)
   * - priority: Time priority at price level (FIFO queue position)
   * 
   * SIZE: ~40 bytes (packed)
   * - type: 1 byte
   * - order_id: 8 bytes
   * - ticker_id: 4 bytes
   * - side: 1 byte
   * - price: 8 bytes
   * - qty: 4 bytes
   * - priority: 8 bytes
   * - padding: ~6 bytes (struct alignment)
   * 
   * USAGE:
   * - Generated by matching engine after each order book change
   * - Enqueued to market data publisher via lock-free queue
   * - Publisher adds sequence number and broadcasts
   */
  struct MEMarketUpdate {
    MarketUpdateType type_ = MarketUpdateType::INVALID;  // Type of update
    
    OrderId order_id_ = OrderId_INVALID;      // Order ID (0 for CLEAR, SNAPSHOT markers)
    TickerId ticker_id_ = TickerId_INVALID;   // Instrument ID
    Side side_ = Side::INVALID;               // BUY or SELL
    Price price_ = Price_INVALID;             // Price (fixed-point, divide by scale for dollars)
    Qty qty_ = Qty_INVALID;                   // Quantity (shares/contracts)
    Priority priority_ = Priority_INVALID;    // Time priority (lower = earlier)

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "MEMarketUpdate"
         << " ["
         << " type:" << marketUpdateTypeToString(type_)
         << " ticker:" << tickerIdToString(ticker_id_)
         << " oid:" << orderIdToString(order_id_)
         << " side:" << sideToString(side_)
         << " qty:" << qtyToString(qty_)
         << " price:" << priceToString(price_)
         << " priority:" << priorityToString(priority_)
         << "]";
      return ss.str();
    }
  };

  /*
   * MARKET DATA PUBLISHER UPDATE (NETWORK FORMAT)
   * =============================================
   * External format broadcast over network via multicast UDP.
   * Adds sequence number to internal MEMarketUpdate.
   * 
   * SEQUENCE NUMBER:
   * - Incremented for each message (1, 2, 3, ...)
   * - Allows gap detection: if received seq 100, then 102 -> missing 101
   * - Critical for reliability: UDP can drop packets
   * - Subscribers request retransmission if gap detected
   * 
   * GAP RECOVERY:
   * - Subscriber sees gap (100, 102)
   * - Sends retransmit request via separate TCP channel
   * - Publisher resends missing message from cache
   * - Or subscriber waits for next snapshot
   * 
   * SIZE: ~48 bytes (packed)
   * - seq_num: 8 bytes (size_t)
   * - me_market_update: ~40 bytes
   * 
   * WIRE PROTOCOL:
   * - Binary format (not text)
   * - Little-endian (x86/x64 native)
   * - Packed (no padding)
   * - Fast: memcpy to send, no parsing needed
   */
  struct MDPMarketUpdate {
    size_t seq_num_ = 0;                 // Sequence number (monotonically increasing)
    MEMarketUpdate me_market_update_;    // Embedded market update

    // String conversion for logging/debugging
    auto toString() const {
      std::stringstream ss;
      ss << "MDPMarketUpdate"
         << " ["
         << " seq:" << seq_num_
         << " " << me_market_update_.toString()
         << "]";
      return ss.str();
    }
  };

#pragma pack(pop)  // End packed struct section (restore default alignment)
                   // Important: Don't leave packing enabled (affects subsequent structs)

  /*
   * LOCK-FREE QUEUE TYPE DEFINITIONS
   * =================================
   * Type aliases for queues used in inter-component communication.
   * 
   * MEMarketUpdateLFQueue:
   * - Matching Engine -> Market Data Publisher
   * - Internal format (no sequence numbers yet)
   * - High throughput: 1M+ messages/second
   * - Low latency: 10-20 ns enqueue/dequeue
   * 
   * MDPMarketUpdateLFQueue:
   * - Not currently used (could be for internal distribution)
   * - Future: Multiple publishers, advanced routing
   */
  typedef Common::LFQueue<Exchange::MEMarketUpdate> MEMarketUpdateLFQueue;
  typedef Common::LFQueue<Exchange::MDPMarketUpdate> MDPMarketUpdateLFQueue;
}

/*
 * MARKET DATA PROTOCOL BEST PRACTICES
 * ====================================
 * 
 * 1. SEQUENCING:
 *    - Every message has sequence number
 *    - Detect gaps immediately
 *    - Request retransmission or wait for snapshot
 * 
 * 2. SNAPSHOTS:
 *    - Send full order book periodically (e.g., every 1 second)
 *    - SNAPSHOT_START -> ADD messages -> SNAPSHOT_END
 *    - Allows recovery from any state
 * 
 * 3. MESSAGE ORDERING:
 *    - Process in sequence number order
 *    - Buffer out-of-order messages
 *    - Timeout for gaps (request retrans or resync)
 * 
 * 4. BOOK RECONSTRUCTION:
 *    - Start with snapshot (SNAPSHOT_START ... SNAPSHOT_END)
 *    - Apply incremental updates (ADD, MODIFY, CANCEL, TRADE)
 *    - Validate: Check prices match, quantities consistent
 * 
 * 5. PERFORMANCE:
 *    - Binary protocol (not JSON/XML) - 10-100x faster
 *    - Packed structs - 20-30% smaller
 *    - Multicast - One sender, many receivers efficiently
 *    - Lock-free queue - 10-20 ns internal routing
 * 
 * EXAMPLE UPDATE SEQUENCE:
 * ```
 * seq=1: ADD AAPL BUY 100@150.00 (new bid)
 * seq=2: ADD AAPL SELL 100@150.05 (new ask)
 * seq=3: TRADE AAPL 50@150.05 (someone bought)
 * seq=4: MODIFY AAPL SELL 50@150.05 (ask reduced by trade)
 * seq=5: CANCEL AAPL BUY 150.00 (bid canceled)
 * ```
 * 
 * INDUSTRY PROTOCOLS:
 * - NASDAQ ITCH: Similar binary protocol
 * - NYSE PILLAR: Binary with sequence numbers
 * - CME MDP 3.0: Multicast market data
 * - This implementation: Simplified version for education
 */
