// ============================================================================
// ORDER MATCH APPLICATION - FIX Message Handler
// ============================================================================
// This class implements the FIX Application interface for the Order Matching
// Server. It processes incoming FIX messages (orders, cancels, market data
// requests) and manages order book interactions.
//
// PURPOSE:
// - Receive NewOrderSingle messages from clients
// - Insert orders into order books
// - Match orders continuously
// - Send ExecutionReports for order status changes
// - Handle OrderCancelRequest messages
// - Process MarketDataRequest messages
//
// MESSAGE FLOW:
// 1. Client → NewOrderSingle → Server
// 2. Server validates order
// 3. Server inserts into order book
// 4. Server sends ExecutionReport (Status=NEW)
// 5. Server attempts matching
// 6. If matched: Server sends ExecutionReport (Status=FILLED/PARTIAL)
//
// KEY DIFFERENCES FROM EXECUTOR:
// - **Executor**: Fills orders immediately (simulated fills)
// - **OrderMatch**: Maintains order books, matches against resting orders
// - OrderMatch orders may rest in book unfilled if no match
// - OrderMatch price determined by passive side (in book first)
//
// ORDER LIFECYCLE IN ORDERMATCH:
// 1. NEW: Order received and inserted into book
// 2. PARTIALLY_FILLED: Some quantity executed, rest still in book
// 3. FILLED: Entire quantity executed
// 4. CANCELED: Order cancelled by client before filled
// 5. REJECTED: Order rejected (unsupported TIF, etc.)
//
// SUPPORTED MESSAGE TYPES:
// - NewOrderSingle (D): New order submission
// - OrderCancelRequest (F): Cancel existing order
// - MarketDataRequest (V): Request market snapshot (stub implementation)
//
// RESTRICTIONS:
// - Only TimeInForce=DAY supported (orders cancelled at end of day)
// - Only FIX 4.2 messages (could extend to other versions)
// - No partial cancel or replace (only full cancel)
//
// PRODUCTION CONSIDERATIONS:
// Real matching engines would add:
// - More order types (Stop, StopLimit, IOC, FOK)
// - Order modification (cancel/replace)
// - Real market data publishing
// - Risk checks before accepting orders
// - Position tracking and limits
// - High-performance threading model
// ============================================================================

package quickfix.examples.ordermatch;

import quickfix.DoNotSend;
import quickfix.FieldNotFound;
import quickfix.IncorrectDataFormat;
import quickfix.IncorrectTagValue;
import quickfix.Message;
import quickfix.MessageCracker;
import quickfix.RejectLogon;
import quickfix.Session;
import quickfix.SessionID;
import quickfix.SessionNotFound;
import quickfix.UnsupportedMessageType;
import quickfix.field.AvgPx;
import quickfix.field.ClOrdID;
import quickfix.field.CumQty;
import quickfix.field.ExecID;
import quickfix.field.ExecTransType;
import quickfix.field.ExecType;
import quickfix.field.LastPx;
import quickfix.field.LastShares;
import quickfix.field.LeavesQty;
import quickfix.field.NoRelatedSym;
import quickfix.field.OrdStatus;
import quickfix.field.OrdType;
import quickfix.field.OrderID;
import quickfix.field.OrderQty;
import quickfix.field.OrigClOrdID;
import quickfix.field.Price;
import quickfix.field.SenderCompID;
import quickfix.field.Side;
import quickfix.field.SubscriptionRequestType;
import quickfix.field.Symbol;
import quickfix.field.TargetCompID;
import quickfix.field.Text;
import quickfix.field.TimeInForce;
import quickfix.fix42.ExecutionReport;
import quickfix.fix42.MarketDataRequest;
import quickfix.fix42.MarketDataSnapshotFullRefresh;
import quickfix.fix42.NewOrderSingle;
import quickfix.fix42.OrderCancelRequest;

import java.util.ArrayList;
import quickfix.field.CxlRejResponseTo;
import quickfix.field.MDEntryPx;
import quickfix.field.MDEntryType;
import quickfix.field.MDReqID;
import quickfix.field.OrdRejReason;
import quickfix.fix42.OrderCancelReject;

// ============================================================================
// ORDER MATCH APPLICATION CLASS
// ============================================================================
// Extends MessageCracker for type-based message routing.
// Implements Application interface for FIX session callbacks.
// ============================================================================
public class Application extends MessageCracker implements quickfix.Application {
    // ========================================================================
    // STATE - Core Components
    // ========================================================================
    private final OrderMatcher orderMatcher = new OrderMatcher();  // Multi-symbol order book manager
    private final IdGenerator generator = new IdGenerator();       // Unique ID generation

    // ========================================================================
    // FIX SESSION LIFECYCLE CALLBACKS
    // ========================================================================
    
    public void fromAdmin(Message message, SessionID sessionId) throws FieldNotFound,
            IncorrectDataFormat, IncorrectTagValue, RejectLogon {
        // Admin messages (Logon, Logout, Heartbeat) - no special handling
    }

    /**
     * Routes incoming application messages to type-specific handlers.
     * Uses MessageCracker to dispatch based on message type.
     */
    public void fromApp(Message message, SessionID sessionId) throws FieldNotFound,
            IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType {
        crack(message, sessionId);  // Dispatch to onMessage() methods
    }

    // ========================================================================
    // NEW ORDER HANDLER
    // ========================================================================
    /**
     * Processes NewOrderSingle messages (MsgType=D).
     * 
     * FLOW:
     * 1. Extract order parameters from FIX message
     * 2. Validate TimeInForce (only DAY supported)
     * 3. Create internal Order object
     * 4. Process order (insert, match, send reports)
     * 5. If error: send reject ExecutionReport
     * 
     * FIELD EXTRACTION:
     * - SenderCompID/TargetCompID: From message header
     * - ClOrdID: Client order ID (tag 11)
     * - Symbol: Trading instrument (tag 55)
     * - Side: BUY/SELL (tag 54)
     * - OrdType: MARKET/LIMIT (tag 40)
     * - Price: Limit price if LIMIT order (tag 44)
     * - OrderQty: Order quantity (tag 38)
     * - TimeInForce: Order duration (tag 59)
     * 
     * VALIDATION:
     * Only DAY orders accepted. IOC, FOK, GTC rejected.
     * 
     * EXAMPLE:
     * Client sends: BUY 100 AAPL @ $150.00
     * Server:
     * 1. Creates Order object
     * 2. Inserts into AAPL bid book
     * 3. Sends ExecutionReport (Status=NEW)
     * 4. Attempts matching
     * 5. If matched: sends ExecutionReport (Status=FILLED)
     */
    public void onMessage(NewOrderSingle message, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        // ====================================================================
        // EXTRACT ORDER PARAMETERS
        // ====================================================================
        String senderCompId = message.getHeader().getString(SenderCompID.FIELD);
        String targetCompId = message.getHeader().getString(TargetCompID.FIELD);
        String clOrdId = message.getString(ClOrdID.FIELD);
        String symbol = message.getString(Symbol.FIELD);
        char side = message.getChar(Side.FIELD);
        char ordType = message.getChar(OrdType.FIELD);

        // Extract price (only for LIMIT orders)
        double price = 0;
        if (ordType == OrdType.LIMIT) {
            price = message.getDouble(Price.FIELD);
        }

        double qty = message.getDouble(OrderQty.FIELD);
        
        // TimeInForce defaults to DAY if not specified
        char timeInForce = TimeInForce.DAY;
        if (message.isSetField(TimeInForce.FIELD)) {
            timeInForce = message.getChar(TimeInForce.FIELD);
        }

        try {
            // ================================================================
            // VALIDATION
            // ================================================================
            if (timeInForce != TimeInForce.DAY) {
                throw new RuntimeException("Unsupported TIF, use Day");
            }

            // ================================================================
            // CREATE ORDER OBJECT
            // ================================================================
            Order order = new Order(clOrdId, symbol, senderCompId, targetCompId, side, ordType,
                    price, (int) qty);

            // ================================================================
            // PROCESS ORDER
            // ================================================================
            // Insert into book, match, send reports
            processOrder(order);
            
        } catch (Exception e) {
            // ================================================================
            // ERROR HANDLING
            // ================================================================
            // Send reject ExecutionReport
            rejectOrder(targetCompId, senderCompId, clOrdId, symbol, side, e.getMessage());
        }
    }

    // ========================================================================
    // ORDER REJECTION
    // ========================================================================
    /**
     * Sends ExecutionReport with REJECTED status.
     * 
     * USAGE:
     * - Validation failures (unsupported TIF, etc.)
     * - System errors
     * 
     * EXECUTIONREPORT FIELDS:
     * - ExecType=REJECTED
     * - OrdStatus=REJECTED
     * - OrdRejReason=OTHER
     * - Text=Error message
     * - LeavesQty=0 (nothing remains)
     * - CumQty=0 (nothing executed)
     * 
     * @param senderCompId Target for response (reverse of incoming)
     * @param targetCompId Sender for response (reverse of incoming)
     * @param clOrdId Client order ID
     * @param symbol Symbol
     * @param side BUY/SELL
     * @param message Rejection reason
     */
    private void rejectOrder(String senderCompId, String targetCompId, String clOrdId,
            String symbol, char side, String message) {

        ExecutionReport fixOrder = new ExecutionReport(
                new OrderID(clOrdId),                             // Use ClOrdID as OrderID
                new ExecID(generator.genExecutionID()),           // Unique execution ID
                new ExecTransType(ExecTransType.NEW),             // FIX 4.2 required field
                new ExecType(ExecType.REJECTED),                  // Execution type
                new OrdStatus(ExecType.REJECTED),                 // Order status
                new Symbol(symbol),
                new Side(side),
                new LeavesQty(0),                                 // Nothing remaining
                new CumQty(0),                                    // Nothing executed
                new AvgPx(0));                                    // No executions

        fixOrder.setString(ClOrdID.FIELD, clOrdId);
        fixOrder.setString(Text.FIELD, message);                  // Rejection reason
        fixOrder.setInt(OrdRejReason.FIELD, OrdRejReason.OTHER);  // Rejection code

        try {
            Session.sendToTarget(fixOrder, senderCompId, targetCompId);
        } catch (SessionNotFound e) {
            e.printStackTrace();
        }
    }

    // ========================================================================
    // ORDER PROCESSING
    // ========================================================================
    /**
     * Core order processing workflow.
     * 
     * STEPS:
     * 1. Insert order into order book
     * 2. If inserted: send NEW ExecutionReport
     * 3. Attempt matching for this symbol
     * 4. For each matched order: send FILLED/PARTIAL ExecutionReport
     * 5. Display updated order book
     * 6. If insert failed: send REJECTED ExecutionReport
     * 
     * MATCHING:
     * After inserting new order, matching runs for the entire symbol.
     * May match:
     * - Just the new order
     * - Multiple existing orders
     * - No orders (if no cross)
     * 
     * EXAMPLE:
     * New order: BUY 500 @ $150.30
     * Existing book:
     *   ASKS: $150.20 x 200, $150.30 x 300
     * 
     * After processing:
     * 1. Insert BUY into bid book
     * 2. Send ExecutionReport (NEW)
     * 3. Match:
     *    - BUY 500 @ $150.30 vs ASK 200 @ $150.20 → trade 200
     *    - BUY 300 @ $150.30 vs ASK 300 @ $150.30 → trade 300
     * 4. Send ExecutionReports:
     *    - BUY order: FILLED (500 @ avgPx)
     *    - ASK 200: FILLED
     *    - ASK 300: FILLED
     * 5. Display updated book
     * 
     * @param order Order to process
     */
    private void processOrder(Order order) {
        if (orderMatcher.insert(order)) {
            // ================================================================
            // ORDER ACCEPTED
            // ================================================================
            acceptOrder(order);  // Send NEW ExecutionReport

            // ================================================================
            // ATTEMPT MATCHING
            // ================================================================
            // Match returns list of orders that executed
            ArrayList<Order> orders = new ArrayList<>();
            orderMatcher.match(order.getSymbol(), orders);

            // ================================================================
            // SEND FILL REPORTS
            // ================================================================
            while (orders.size() > 0) {
                fillOrder(orders.remove(0));  // Send FILLED/PARTIAL report
            }
            
            // ================================================================
            // DISPLAY UPDATED BOOK
            // ================================================================
            orderMatcher.display(order.getSymbol());
        } else {
            // ================================================================
            // ORDER REJECTED
            // ================================================================
            rejectOrder(order);
        }
    }

    /**
     * Sends REJECTED ExecutionReport for internal Order object.
     */
    private void rejectOrder(Order order) {
        updateOrder(order, OrdStatus.REJECTED);
    }

    /**
     * Sends NEW ExecutionReport for accepted order.
     */
    private void acceptOrder(Order order) {
        updateOrder(order, OrdStatus.NEW);
    }

    /**
     * Sends CANCELED ExecutionReport for cancelled order.
     */
    private void cancelOrder(Order order) {
        updateOrder(order, OrdStatus.CANCELED);
    }

    // ========================================================================
    // EXECUTION REPORT GENERATION
    // ========================================================================
    /**
     * Creates and sends ExecutionReport for order status update.
     * 
     * EXECUTIONREPORT FIELDS:
     * - OrderID: Server-assigned order ID (using ClOrdID here)
     * - ExecID: Unique execution ID
     * - ExecType: NEW/FILLED/PARTIALLY_FILLED/CANCELED/REJECTED
     * - OrdStatus: Same as ExecType
     * - Symbol, Side: Order attributes
     * - LeavesQty: Remaining open quantity
     * - CumQty: Total executed quantity
     * - AvgPx: Average execution price
     * - ClOrdID: Client's order ID
     * - OrderQty: Total order quantity
     * 
     * FOR FILLS (FILLED/PARTIALLY_FILLED):
     * - LastShares: Quantity from most recent fill
     * - LastPx: Price of most recent fill
     * 
     * ROUTING:
     * Reverses sender/target: response goes back to order originator.
     * 
     * @param order Order to report
     * @param status Order status (NEW/FILLED/etc.)
     */
    private void updateOrder(Order order, char status) {
        // Reverse routing: send back to order owner
        String targetCompId = order.getOwner();    // Original sender
        String senderCompId = order.getTarget();   // Original target (us)

        ExecutionReport fixOrder = new ExecutionReport(
                new OrderID(order.getClientOrderId()),            // Server order ID
                new ExecID(generator.genExecutionID()),           // Execution ID
                new ExecTransType(ExecTransType.NEW),             // FIX 4.2 field
                new ExecType(status),                             // Execution type
                new OrdStatus(status),                            // Order status
                new Symbol(order.getSymbol()),
                new Side(order.getSide()),
                new LeavesQty(order.getOpenQuantity()),           // Remaining qty
                new CumQty(order.getExecutedQuantity()),          // Total executed
                new AvgPx(order.getAvgExecutedPrice()));          // Avg price

        fixOrder.setString(ClOrdID.FIELD, order.getClientOrderId());
        fixOrder.setDouble(OrderQty.FIELD, order.getQuantity());

        // Add last fill details if this is a fill report
        if (status == OrdStatus.FILLED || status == OrdStatus.PARTIALLY_FILLED) {
            fixOrder.setDouble(LastShares.FIELD, order.getLastExecutedQuantity());
            fixOrder.setDouble(LastPx.FIELD, order.getPrice());
        }

        try {
            Session.sendToTarget(fixOrder, senderCompId, targetCompId);
        } catch (SessionNotFound e) {
            // Session disconnected - cannot send
        }
    }

    /**
     * Sends FILLED or PARTIALLY_FILLED ExecutionReport.
     * 
     * STATUS DETERMINATION:
     * - If openQuantity == 0: FILLED
     * - If openQuantity > 0: PARTIALLY_FILLED
     * 
     * @param order Order that executed
     */
    private void fillOrder(Order order) {
        updateOrder(order, order.isFilled() ? OrdStatus.FILLED : OrdStatus.PARTIALLY_FILLED);
    }

    // ========================================================================
    // ORDER CANCEL HANDLER
    // ========================================================================
    /**
     * Processes OrderCancelRequest messages (MsgType=F).
     * 
     * FLOW:
     * 1. Extract symbol, side, original client order ID
     * 2. Find order in order book
     * 3. If found:
     *    - Cancel order (set openQuantity=0)
     *    - Send CANCELED ExecutionReport
     *    - Remove from order book
     * 4. If not found:
     *    - Send OrderCancelReject
     * 
     * FIELDS:
     * - OrigClOrdID: Client order ID to cancel (tag 41)
     * - Symbol: Symbol (tag 55)
     * - Side: BUY/SELL (tag 54)
     * - ClOrdID: New client order ID for this cancel request (tag 11)
     * 
     * ORDERCANCELREJECT:
     * Sent when order cannot be cancelled:
     * - Order not found (already filled or never existed)
     * - Order already cancelled
     * 
     * EXAMPLE:
     * Client sent: BUY 100 AAPL @ $150.00, ClOrdID=ABC123
     * Order resting in book
     * Client sends: OrderCancelRequest, OrigClOrdID=ABC123
     * Server:
     * 1. Finds order in AAPL bid book
     * 2. Sets openQuantity=0
     * 3. Sends ExecutionReport (Status=CANCELED)
     * 4. Removes from book
     */
    public void onMessage(OrderCancelRequest message, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        // ====================================================================
        // EXTRACT CANCEL PARAMETERS
        // ====================================================================
        String symbol = message.getString(Symbol.FIELD);
        char side = message.getChar(Side.FIELD);
        String id = message.getString(OrigClOrdID.FIELD);  // Order to cancel
        
        // ====================================================================
        // FIND ORDER IN BOOK
        // ====================================================================
        Order order = orderMatcher.find(symbol, side, id);
        
        if (order != null) {
            // ================================================================
            // ORDER FOUND - CANCEL IT
            // ================================================================
            order.cancel();              // Set openQuantity=0
            cancelOrder(order);          // Send CANCELED ExecutionReport
            orderMatcher.erase(order);   // Remove from book
        } else {
            // ================================================================
            // ORDER NOT FOUND - SEND REJECT
            // ================================================================
            OrderCancelReject fixOrderReject = new OrderCancelReject(
                    new OrderID("NONE"),                                          // No order ID
                    new ClOrdID(message.getString(ClOrdID.FIELD)),                // Cancel request ID
                    new OrigClOrdID(message.getString(OrigClOrdID.FIELD)),        // Order being cancelled
                    new OrdStatus(OrdStatus.REJECTED),                            // Status
                    new CxlRejResponseTo(CxlRejResponseTo.ORDER_CANCEL_REQUEST)); // Response type

            // Reverse routing for response
            String senderCompId = message.getHeader().getString(SenderCompID.FIELD);
            String targetCompId = message.getHeader().getString(TargetCompID.FIELD);
            fixOrderReject.getHeader().setString(SenderCompID.FIELD, targetCompId);
            fixOrderReject.getHeader().setString(TargetCompID.FIELD, senderCompId);
            
            try {
                Session.sendToTarget(fixOrderReject, targetCompId, senderCompId);
            } catch (SessionNotFound e) {
                // Cannot send - session disconnected
            }
        }
    }

    // ========================================================================
    // MARKET DATA REQUEST HANDLER
    // ========================================================================
    /**
     * Processes MarketDataRequest messages (MsgType=V).
     * 
     * STUB IMPLEMENTATION:
     * This is a placeholder that returns dummy market data.
     * Real implementation would:
     * - Query order book for bid/ask prices and quantities
     * - Support multiple price levels (market depth)
     * - Handle subscription types (snapshot vs streaming)
     * 
     * CURRENT BEHAVIOR:
     * Returns MarketDataSnapshotFullRefresh with:
     * - Fixed price $123.45
     * - MDEntryType='0' (Bid)
     * 
     * MARKET DATA REQUEST FIELDS:
     * - MDReqID: Request ID (tag 262)
     * - SubscriptionRequestType: Snapshot/Subscribe/Unsubscribe (tag 263)
     * - NoRelatedSym: Number of symbols requested (tag 146)
     * - Symbol: Symbols in repeating group (tag 55)
     * 
     * PRODUCTION IMPLEMENTATION:
     * Would query orderMatcher for best bid/ask:
     * - Best bid: Highest price in bid book
     * - Best ask: Lowest price in ask book
     * - Bid/ask sizes
     * - Multiple depth levels
     * - Last trade price and volume
     */
    public void onMessage(MarketDataRequest message, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        MarketDataRequest.NoRelatedSym noRelatedSyms = new MarketDataRequest.NoRelatedSym();

        //String mdReqId = message.getString(MDReqID.FIELD);
        char subscriptionRequestType = message.getChar(SubscriptionRequestType.FIELD);

        // Only snapshot supported (not streaming subscription)
        if (subscriptionRequestType != SubscriptionRequestType.SNAPSHOT)
            throw new IncorrectTagValue(SubscriptionRequestType.FIELD);
            
        //int marketDepth = message.getInt(MarketDepth.FIELD);
        int relatedSymbolCount = message.getInt(NoRelatedSym.FIELD);

        // Create market data response
        MarketDataSnapshotFullRefresh fixMD = new MarketDataSnapshotFullRefresh();
        fixMD.setString(MDReqID.FIELD, message.getString(MDReqID.FIELD));  // Echo request ID
        
        // Extract symbols from request
        for (int i = 1; i <= relatedSymbolCount; ++i) {
            message.getGroup(i, noRelatedSyms);
            String symbol = noRelatedSyms.getString(Symbol.FIELD);
            fixMD.setString(Symbol.FIELD, symbol);
        }
        
        // Add dummy market data entry (stub implementation)
        MarketDataSnapshotFullRefresh.NoMDEntries noMDEntries = new MarketDataSnapshotFullRefresh.NoMDEntries();
        noMDEntries.setChar(MDEntryType.FIELD, '0');   // '0' = Bid
        noMDEntries.setDouble(MDEntryPx.FIELD, 123.45); // Dummy price
        fixMD.addGroup(noMDEntries);
        
        // Reverse routing for response
        String senderCompId = message.getHeader().getString(SenderCompID.FIELD);
        String targetCompId = message.getHeader().getString(TargetCompID.FIELD);
        fixMD.getHeader().setString(SenderCompID.FIELD, targetCompId);
        fixMD.getHeader().setString(TargetCompID.FIELD, senderCompId);
        
        try {
            Session.sendToTarget(fixMD, targetCompId, senderCompId);
        } catch (SessionNotFound e) {
            // Cannot send - session disconnected
        }
    }

    // ========================================================================
    // SESSION LIFECYCLE CALLBACKS
    // ========================================================================
    
    public void onCreate(SessionID sessionId) {
        // Session created - no action needed
    }

    public void onLogon(SessionID sessionId) {
        System.out.println("Logon - " + sessionId);
    }

    public void onLogout(SessionID sessionId) {
        System.out.println("Logout - " + sessionId);
    }

    public void toAdmin(Message message, SessionID sessionId) {
        // Outgoing admin messages - no action needed
    }

    public void toApp(Message message, SessionID sessionId) throws DoNotSend {
        // Outgoing application messages - no action needed
    }

    // ========================================================================
    // ACCESSOR
    // ========================================================================
    
    /**
     * Provides access to order matcher for console display commands.
     * Used by Main.java to show order books on demand.
     */
    public OrderMatcher orderMatcher() {
        return orderMatcher;
    }
}
