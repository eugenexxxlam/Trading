// ============================================================================
// BANZAI APPLICATION - FIX Message Handler
// ============================================================================
// This class is the heart of the Banzai trading client, implementing the
// QuickFIX Application interface to handle all FIX protocol communication.
//
// PURPOSE:
// - Process incoming ExecutionReports from trading servers
// - Send outgoing order messages (NewOrderSingle, Cancel, Replace)
// - Update GUI data models based on server responses
// - Handle FIX session lifecycle events
// - Support OpenFIX certification testing
//
// KEY RESPONSIBILITIES:
// 1. MESSAGE ROUTING: Direct incoming messages to appropriate handlers
// 2. ORDER MANAGEMENT: Track order state across ExecutionReports
// 3. EXECUTION TRACKING: Record all fills and calculate averages
// 4. GUI INTEGRATION: Update Swing models on Event Dispatch Thread
// 5. MULTI-VERSION SUPPORT: Handle FIX 4.0 through 5.0
// 6. ERROR HANDLING: Generate business and session rejects
//
// DESIGN PATTERNS:
// - Application Interface: QuickFIX callback pattern
// - Observer Pattern: Notifies UI of logon/order changes
// - Command Pattern: MessageProcessor for GUI thread safety
// - Strategy Pattern: Version-specific message construction
//
// THREADING MODEL:
// - FIX messages arrive on QuickFIX threads
// - MessageProcessor moves processing to Swing EDT
// - Ensures thread-safe GUI updates
//
// PRODUCTION CONSIDERATIONS:
// Real trading applications would add:
// - Position tracking and P&L calculation
// - Risk management and pre-trade checks
// - Order book management
// - Market data integration
// - Algorithmic trading strategies
// - Compliance and audit logging
// ============================================================================

package quickfix.examples.banzai;

import quickfix.Application;
import quickfix.DefaultMessageFactory;
import quickfix.DoNotSend;
import quickfix.FieldNotFound;
import quickfix.FixVersions;
import quickfix.IncorrectDataFormat;
import quickfix.IncorrectTagValue;
import quickfix.Message;
import quickfix.RejectLogon;
import quickfix.Session;
import quickfix.SessionID;
import quickfix.SessionNotFound;
import quickfix.UnsupportedMessageType;

// FIX field imports - represent individual message fields
import quickfix.field.AvgPx;
import quickfix.field.BeginString;
import quickfix.field.BusinessRejectReason;
import quickfix.field.ClOrdID;
import quickfix.field.CumQty;
import quickfix.field.CxlType;
import quickfix.field.DeliverToCompID;
import quickfix.field.ExecID;
import quickfix.field.HandlInst;
import quickfix.field.LastPx;
import quickfix.field.LastShares;
import quickfix.field.LeavesQty;
import quickfix.field.LocateReqd;
import quickfix.field.MsgSeqNum;
import quickfix.field.MsgType;
import quickfix.field.OrdStatus;
import quickfix.field.OrdType;
import quickfix.field.OrderQty;
import quickfix.field.OrigClOrdID;
import quickfix.field.Price;
import quickfix.field.RefMsgType;
import quickfix.field.RefSeqNum;
import quickfix.field.SenderCompID;
import quickfix.field.SessionRejectReason;
import quickfix.field.Side;
import quickfix.field.StopPx;
import quickfix.field.Symbol;
import quickfix.field.TargetCompID;
import quickfix.field.Text;
import quickfix.field.TimeInForce;
import quickfix.field.TransactTime;

import javax.swing.*;
import java.math.BigDecimal;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Observable;
import java.util.Observer;

// ============================================================================
// BANZAI APPLICATION CLASS
// ============================================================================
// Implements QuickFIX Application interface for FIX message handling.
// Manages the bridge between FIX protocol and GUI components.
// ============================================================================
public class BanzaiApplication implements Application {
    // ========================================================================
    // CORE COMPONENTS
    // ========================================================================
    
    // Factory for creating FIX messages across all versions
    private final DefaultMessageFactory messageFactory = new DefaultMessageFactory();
    
    // Data models for GUI tables
    private OrderTableModel orderTableModel = null;           // Active orders display
    private ExecutionTableModel executionTableModel = null;   // Fill history display
    
    // Observable wrappers for notifying UI components
    private final ObservableOrder observableOrder = new ObservableOrder();
    private final ObservableLogon observableLogon = new ObservableLogon();
    
    // OpenFIX certification testing flags
    private boolean isAvailable = true;      // Application availability flag
    private boolean isMissingField;          // Trigger missing field rejection
    
    // ========================================================================
    // TYPE CONVERSION MAPS
    // ========================================================================
    // Bidirectional maps between internal enums and FIX field values
    // Allow conversion in both directions: Internal <-> FIX
    
    static private final TwoWayMap sideMap = new TwoWayMap();    // OrderSide <-> Side
    static private final TwoWayMap typeMap = new TwoWayMap();    // OrderType <-> OrdType
    static private final TwoWayMap tifMap = new TwoWayMap();     // OrderTIF <-> TimeInForce
    
    // ========================================================================
    // DUPLICATE DETECTION
    // ========================================================================
    // Tracks processed execution IDs to prevent duplicate processing
    // Key: SessionID, Value: Set of processed ExecIDs for that session
    // 
    // WHY NEEDED?
    // - Network issues can cause message duplication
    // - Resend requests replay messages
    // - Must not double-count fills
    static private final HashMap<SessionID, HashSet<ExecID>> execIDs = new HashMap<>();

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    /**
     * Creates BanzaiApplication with data model references.
     * 
     * @param orderTableModel Manages orders for GUI display
     * @param executionTableModel Manages executions for GUI display
     */
    public BanzaiApplication(OrderTableModel orderTableModel,
            ExecutionTableModel executionTableModel) {
        this.orderTableModel = orderTableModel;
        this.executionTableModel = executionTableModel;
    }

    // ========================================================================
    // FIX SESSION LIFECYCLE CALLBACKS
    // ========================================================================
    // These methods are called by QuickFIX engine at various points
    // in the session lifecycle. Empty implementations = no custom logic needed.
    
    /**
     * Called when FIX session is created (before logon).
     * Can initialize session-specific resources here.
     */
    public void onCreate(SessionID sessionID) {
    }

    /**
     * Called when FIX session successfully logs on.
     * Notifies UI that connection is established and ready for trading.
     */
    public void onLogon(SessionID sessionID) {
        observableLogon.logon(sessionID);
    }

    /**
     * Called when FIX session logs out (graceful or error).
     * Notifies UI that connection is lost.
     */
    public void onLogout(SessionID sessionID) {
        observableLogon.logoff(sessionID);
    }

    /**
     * Called before sending administrative messages (Logon, Heartbeat, etc.).
     * Can add custom fields to admin messages here.
     */
    public void toAdmin(quickfix.Message message, SessionID sessionID) {
    }

    /**
     * Called before sending application messages (orders, etc.).
     * Can validate or modify outgoing messages here.
     * Throw DoNotSend to prevent message from being sent.
     */
    public void toApp(quickfix.Message message, SessionID sessionID) throws DoNotSend {
    }

    /**
     * Called when receiving administrative messages.
     * Can handle custom admin message logic here.
     */
    public void fromAdmin(quickfix.Message message, SessionID sessionID) throws FieldNotFound,
            IncorrectDataFormat, IncorrectTagValue, RejectLogon {
    }

    // ========================================================================
    // INCOMING APPLICATION MESSAGE HANDLER
    // ========================================================================
    /**
     * Called when application messages are received from server.
     * 
     * THREADING STRATEGY:
     * QuickFIX calls this on its own thread, but Swing requires updates
     * on the Event Dispatch Thread (EDT). We use SwingUtilities.invokeLater
     * to move processing to EDT for thread-safe GUI updates.
     * 
     * FLOW:
     * 1. Receive message on QuickFIX thread
     * 2. Wrap in MessageProcessor (Runnable)
     * 3. Queue for execution on EDT
     * 4. Process message on EDT
     * 5. Update GUI models (safe on EDT)
     * 
     * @param message Received FIX message
     * @param sessionID Session that received the message
     */
    public void fromApp(quickfix.Message message, SessionID sessionID) throws FieldNotFound,
            IncorrectDataFormat, IncorrectTagValue, UnsupportedMessageType {
        try {
            // Move message processing to Swing Event Dispatch Thread
            SwingUtilities.invokeLater(new MessageProcessor(message, sessionID));
        } catch (Exception e) {
            // Silently ignore exceptions to prevent QuickFIX thread death
        }
    }

    // ========================================================================
    // MESSAGE PROCESSOR - Swing EDT Execution
    // ========================================================================
    /**
     * Processes FIX messages on the Swing Event Dispatch Thread.
     * 
     * DESIGN PATTERN: Command Pattern
     * Encapsulates message processing as a Runnable command that can be
     * executed on a different thread (EDT).
     * 
     * WHY EDT?
     * All Swing component updates must occur on EDT to avoid:
     * - Race conditions
     * - Inconsistent UI state
     * - ConcurrentModificationException
     * - Deadlocks
     */
    public class MessageProcessor implements Runnable {
        private final quickfix.Message message;
        private final SessionID sessionID;

        public MessageProcessor(quickfix.Message message, SessionID sessionID) {
            this.message = message;
            this.sessionID = sessionID;
        }

        /**
         * Processes message on EDT, routing to appropriate handler.
         * 
         * MESSAGE TYPES HANDLED:
         * - ExecutionReport (MsgType=8): Order status updates
         * - OrderCancelReject (MsgType=9): Cancel/replace rejections
         * 
         * SPECIAL HANDLING:
         * - isAvailable flag: Simulates application unavailability
         * - isMissingField flag: Triggers conditional field rejection
         * - DeliverToCompID: Detects routing errors
         * 
         * OPENFIX CERTIFICATION:
         * The special handling supports OpenFIX certification testing,
         * which validates proper error handling and rejection behavior.
         */
        public void run() {
            try {
                MsgType msgType = new MsgType();
                
                if (isAvailable) {
                    // ========================================================
                    // OPENFIX TESTING - Conditional Field Missing
                    // ========================================================
                    if (isMissingField) {
                        sendBusinessReject(message, BusinessRejectReason.CONDITIONALLY_REQUIRED_FIELD_MISSING, 
                            "Conditionally required field missing");
                    }
                    // ========================================================
                    // OPENFIX TESTING - Routing Problem
                    // ========================================================
                    else if (message.getHeader().isSetField(DeliverToCompID.FIELD)) {
                        // DeliverToCompID indicates message routing error
                        sendSessionReject(message, SessionRejectReason.COMPID_PROBLEM);
                    }
                    // ========================================================
                    // EXECUTION REPORT (MsgType=8)
                    // ========================================================
                    else if (message.getHeader().getField(msgType).valueEquals("8")) {
                        executionReport(message, sessionID);
                    }
                    // ========================================================
                    // ORDER CANCEL REJECT (MsgType=9)
                    // ========================================================
                    else if (message.getHeader().getField(msgType).valueEquals("9")) {
                        cancelReject(message, sessionID);
                    }
                    // ========================================================
                    // UNSUPPORTED MESSAGE TYPE
                    // ========================================================
                    else {
                        sendBusinessReject(message, BusinessRejectReason.UNSUPPORTED_MESSAGE_TYPE,
                                "Unsupported Message Type");
                    }
                } else {
                    // ========================================================
                    // APPLICATION UNAVAILABLE
                    // ========================================================
                    sendBusinessReject(message, BusinessRejectReason.APPLICATION_NOT_AVAILABLE,
                            "Application not available");
                }
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    // ========================================================================
    // REJECTION MESSAGE GENERATION
    // ========================================================================
    
    /**
     * Sends Session-level Reject message.
     * 
     * SESSION REJECTS:
     * Used for FIX protocol violations:
     * - Invalid tag numbers
     * - Missing required fields
     * - Incorrect data format
     * - CompID problems
     * 
     * @param message Message to reject
     * @param rejectReason Rejection reason code
     */
    private void sendSessionReject(Message message, int rejectReason) throws FieldNotFound,
            SessionNotFound {
        Message reply = createMessage(message, MsgType.REJECT);
        reverseRoute(message, reply);
        String refSeqNum = message.getHeader().getString(MsgSeqNum.FIELD);
        reply.setString(RefSeqNum.FIELD, refSeqNum);
        reply.setString(RefMsgType.FIELD, message.getHeader().getString(MsgType.FIELD));
        reply.setInt(SessionRejectReason.FIELD, rejectReason);
        Session.sendToTarget(reply);
    }

    /**
     * Sends Business-level Reject message.
     * 
     * BUSINESS REJECTS:
     * Used for application-level problems:
     * - Unsupported message type
     * - Application not available
     * - Conditionally required field missing
     * - Invalid business data
     * 
     * @param message Message to reject
     * @param rejectReason Business rejection reason code
     * @param rejectText Human-readable explanation
     */
    private void sendBusinessReject(Message message, int rejectReason, String rejectText)
            throws FieldNotFound, SessionNotFound {
        Message reply = createMessage(message, MsgType.BUSINESS_MESSAGE_REJECT);
        reverseRoute(message, reply);
        String refSeqNum = message.getHeader().getString(MsgSeqNum.FIELD);
        reply.setString(RefSeqNum.FIELD, refSeqNum);
        reply.setString(RefMsgType.FIELD, message.getHeader().getString(MsgType.FIELD));
        reply.setInt(BusinessRejectReason.FIELD, rejectReason);
        reply.setString(Text.FIELD, rejectText);
        Session.sendToTarget(reply);
    }

    /**
     * Creates FIX message of specified type using same version as original.
     * Ensures reject uses same FIX version as incoming message.
     */
    private Message createMessage(Message message, String msgType) throws FieldNotFound {
        return messageFactory.create(message.getHeader().getString(BeginString.FIELD), msgType);
    }

    /**
     * Reverses message routing for reply.
     * Swaps SenderCompID and TargetCompID so reply goes back to sender.
     */
    private void reverseRoute(Message message, Message reply) throws FieldNotFound {
        reply.getHeader().setString(SenderCompID.FIELD,
                message.getHeader().getString(TargetCompID.FIELD));
        reply.getHeader().setString(TargetCompID.FIELD,
                message.getHeader().getString(SenderCompID.FIELD));
    }

    // ========================================================================
    // EXECUTION REPORT HANDLER
    // ========================================================================
    /**
     * Processes ExecutionReport messages (order status updates).
     * 
     * EXECUTION REPORT CONTAINS:
     * - Order status (NEW, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED)
     * - Fill details (quantity, price)
     * - Cumulative totals (total filled, average price)
     * - Order parameters (potentially updated for replacements)
     * 
     * PROCESSING FLOW:
     * 1. Check for duplicate (via ExecID)
     * 2. Find order in table model
     * 3. Calculate fill size
     * 4. Update order state based on OrdStatus
     * 5. Update order model and notify observers
     * 6. Create execution record for fills
     * 
     * @param message ExecutionReport message
     * @param sessionID Session that received the message
     */
    private void executionReport(Message message, SessionID sessionID) throws FieldNotFound {

        // ====================================================================
        // DUPLICATE DETECTION
        // ====================================================================
        ExecID execID = (ExecID) message.getField(new ExecID());
        if (alreadyProcessed(execID, sessionID))
            return;

        // ====================================================================
        // ORDER LOOKUP
        // ====================================================================
        Order order = orderTableModel.getOrder(message.getField(new ClOrdID()).getValue());
        if (order == null) {
            return;  // Order not in our table (maybe from previous session)
        }

        // ====================================================================
        // CALCULATE FILL SIZE
        // ====================================================================
        // Different calculation for FIX 4.1 vs later versions
        BigDecimal fillSize;

        if (message.isSetField(LastShares.FIELD)) {
            // FIX 4.0-4.1: Uses LastShares for fill quantity
            LastShares lastShares = new LastShares();
            message.getField(lastShares);
            fillSize = new BigDecimal("" + lastShares.getValue());
        } else {
            // FIX 4.2+: Calculate from LeavesQty
            // Fill size = Original quantity - Leaves quantity
            LeavesQty leavesQty = new LeavesQty();
            message.getField(leavesQty);
            fillSize = new BigDecimal(order.getQuantity()).subtract(
                new BigDecimal("" + leavesQty.getValue()));
        }

        // ====================================================================
        // UPDATE ORDER WITH FILL
        // ====================================================================
        if (fillSize.compareTo(BigDecimal.ZERO) > 0) {
            order.setOpen(order.getOpen() - (int) Double.parseDouble(fillSize.toPlainString()));
            order.setExecuted(Double.parseDouble(message.getString(CumQty.FIELD)));
            order.setAvgPx(Double.parseDouble(message.getString(AvgPx.FIELD)));
        }

        // ====================================================================
        // PROCESS ORDER STATUS
        // ====================================================================
        OrdStatus ordStatus = (OrdStatus) message.getField(new OrdStatus());
        
        if (ordStatus.valueEquals(OrdStatus.REJECTED)) {
            // Order rejected by server (validation, risk, etc.)
            order.setRejected(true);
            order.setOpen(0);
            
        } else if (ordStatus.valueEquals(OrdStatus.CANCELED)
                || ordStatus.valueEquals(OrdStatus.DONE_FOR_DAY)) {
            // Order canceled or expired
            order.setCanceled(true);
            order.setOpen(0);
            
        } else if (ordStatus.valueEquals(OrdStatus.NEW)) {
            // Order accepted and resting on book
            if (order.isNew()) {
                order.setNew(false);  // Mark as acknowledged
            }
            
        } else if (ordStatus.valueEquals(OrdStatus.REPLACED)) {
            // Order successfully replaced - update parameters
            OrderQty orderQty = new OrderQty();
            message.getField(orderQty);
            order.setQuantity((int)orderQty.getValue());

            LeavesQty leavesQty = new LeavesQty();
            message.getField(leavesQty);
            order.setOpen((int)leavesQty.getValue());

            CumQty cumQty = new CumQty();
            message.getField(cumQty);
            order.setExecuted((int)cumQty.getValue());

            // Update price if provided
            if (message.isSetField(Price.FIELD)){
                Price price = new Price();
                message.getField(price);
                order.setLimit(price.getValue());
            }
            // Update stop price if provided
            if (message.isSetField(StopPx.FIELD)){
                StopPx stopPx = new StopPx();
                message.getField(stopPx);
                order.setStop(stopPx.getValue());
            }
        }

        // ====================================================================
        // EXTRACT OPTIONAL MESSAGE TEXT
        // ====================================================================
        try {
            order.setMessage(message.getField(new Text()).getValue());
        } catch (FieldNotFound e) {
            // No text field - that's OK
        }

        // ====================================================================
        // UPDATE GUI MODELS
        // ====================================================================
        orderTableModel.updateOrder(order, message.getField(new ClOrdID()).getValue());
        observableOrder.update(order);

        // ====================================================================
        // CREATE EXECUTION RECORD FOR FILLS
        // ====================================================================
        if (fillSize.compareTo(BigDecimal.ZERO) > 0) {
            Execution execution = new Execution();
            execution.setExchangeID(sessionID + message.getField(new ExecID()).getValue());

            execution.setSymbol(message.getField(new Symbol()).getValue());
            execution.setQuantity(fillSize.intValue());
            
            if (message.isSetField(LastPx.FIELD)) {
                execution.setPrice(Double.parseDouble(message.getString(LastPx.FIELD)));
            }
            
            Side side = (Side) message.getField(new Side());
            execution.setSide(FIXSideToSide(side));
            
            // Add to execution blotter
            executionTableModel.addExecution(execution);
        }
    }

    // ========================================================================
    // ORDER CANCEL REJECT HANDLER
    // ========================================================================
    /**
     * Processes OrderCancelReject messages.
     * 
     * RECEIVED WHEN:
     * - Cancel request rejected (order already filled, doesn't exist, etc.)
     * - Replace request rejected (invalid parameters, etc.)
     * 
     * PROCESSING:
     * - Find order by ClOrdID (or OrigClOrdID for replacements)
     * - Update order with rejection message
     * - Notify user via GUI
     * 
     * @param message OrderCancelReject message
     * @param sessionID Session that received the message
     */
    private void cancelReject(Message message, SessionID sessionID) throws FieldNotFound {

        String id = message.getField(new ClOrdID()).getValue();
        Order order = orderTableModel.getOrder(id);
        
        if (order == null)
            return;
            
        // If this is a replacement, find the original order
        if (order.getOriginalID() != null)
            order = orderTableModel.getOrder(order.getOriginalID());

        // Extract rejection reason text
        try {
            order.setMessage(message.getField(new Text()).getValue());
        } catch (FieldNotFound e) {
            // No text field
        }
        
        orderTableModel.updateOrder(order, message.getField(new OrigClOrdID()).getValue());
    }

    // ========================================================================
    // DUPLICATE DETECTION
    // ========================================================================
    /**
     * Checks if ExecID has already been processed for this session.
     * 
     * WHY NEEDED?
     * - Network issues can cause duplicate messages
     * - Resend requests replay historical messages
     * - Must not double-count fills or update order state twice
     * 
     * ALGORITHM:
     * - Maintain set of processed ExecIDs per session
     * - Check if ExecID in set
     * - Add to set if new
     * 
     * @param execID Execution ID to check
     * @param sessionID Session ID
     * @return true if already processed
     */
    private boolean alreadyProcessed(ExecID execID, SessionID sessionID) {
        HashSet<ExecID> set = execIDs.get(sessionID);
        if (set == null) {
            set = new HashSet<>();
            set.add(execID);
            execIDs.put(sessionID, set);
            return false;
        } else {
            if (set.contains(execID))
                return true;
            set.add(execID);
            return false;
        }
    }

    // ========================================================================
    // GENERIC SEND METHOD
    // ========================================================================
    /**
     * Sends FIX message to specified session.
     * Handles SessionNotFound exception gracefully.
     */
    private void send(quickfix.Message message, SessionID sessionID) {
        try {
            Session.sendToTarget(message, sessionID);
        } catch (SessionNotFound e) {
            System.out.println(e);
        }
    }

    // ========================================================================
    // ORDER SUBMISSION - Version Router
    // ========================================================================
    /**
     * Sends order using appropriate FIX version.
     * 
     * VERSION DETECTION:
     * Examines order's SessionID.BeginString to determine FIX version
     * and calls version-specific send method.
     * 
     * SUPPORTED VERSIONS:
     * - FIX 4.0, 4.1, 4.2, 4.3, 4.4
     * - FIXT 1.1 (FIX 5.0)
     * 
     * @param order Order to send
     */
    public void send(Order order) {
        String beginString = order.getSessionID().getBeginString();
        switch (beginString) {
            case FixVersions.BEGINSTRING_FIX40:
                send40(order);
                break;
            case FixVersions.BEGINSTRING_FIX41:
                send41(order);
                break;
            case FixVersions.BEGINSTRING_FIX42:
                send42(order);
                break;
            case FixVersions.BEGINSTRING_FIX43:
                send43(order);
                break;
            case FixVersions.BEGINSTRING_FIX44:
                send44(order);
                break;
            case FixVersions.BEGINSTRING_FIXT11:
                send50(order);
                break;
        }
    }

    // ========================================================================
    // VERSION-SPECIFIC ORDER SENDERS
    // ========================================================================
    // Each FIX version has different constructor requirements and field ordering.
    // These methods construct NewOrderSingle messages correctly for each version.
    
    /**
     * Sends order using FIX 4.0 format.
     * FIX 4.0 SPECIFICS:
     * - OrderQty in constructor
     * - HandlInst required ('1' = automated execution, private)
     */
    public void send40(Order order) {
        quickfix.fix40.NewOrderSingle newOrderSingle = new quickfix.fix40.NewOrderSingle(
                new ClOrdID(order.getID()), 
                new HandlInst('1'), 
                new Symbol(order.getSymbol()),
                sideToFIXSide(order.getSide()), 
                new OrderQty(order.getQuantity()),
                typeToFIXType(order.getType()));

        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    /**
     * Sends order using FIX 4.1 format.
     * FIX 4.1 SPECIFICS:
     * - OrderQty set separately (not in constructor)
     */
    public void send41(Order order) {
        quickfix.fix41.NewOrderSingle newOrderSingle = new quickfix.fix41.NewOrderSingle(
                new ClOrdID(order.getID()), 
                new HandlInst('1'), 
                new Symbol(order.getSymbol()),
                sideToFIXSide(order.getSide()), 
                typeToFIXType(order.getType()));
        newOrderSingle.set(new OrderQty(order.getQuantity()));

        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    /**
     * Sends order using FIX 4.2 format.
     * FIX 4.2 SPECIFICS:
     * - Added TransactTime (order creation timestamp)
     */
    public void send42(Order order) {
        quickfix.fix42.NewOrderSingle newOrderSingle = new quickfix.fix42.NewOrderSingle(
                new ClOrdID(order.getID()), 
                new HandlInst('1'), 
                new Symbol(order.getSymbol()),
                sideToFIXSide(order.getSide()), 
                new TransactTime(), 
                typeToFIXType(order.getType()));
        newOrderSingle.set(new OrderQty(order.getQuantity()));

        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    /**
     * Sends order using FIX 4.3 format.
     * FIX 4.3 SPECIFICS:
     * - Symbol set separately (not in constructor)
     */
    public void send43(Order order) {
        quickfix.fix43.NewOrderSingle newOrderSingle = new quickfix.fix43.NewOrderSingle(
                new ClOrdID(order.getID()), 
                new HandlInst('1'), 
                sideToFIXSide(order.getSide()),
                new TransactTime(), 
                typeToFIXType(order.getType()));
        newOrderSingle.set(new OrderQty(order.getQuantity()));
        newOrderSingle.set(new Symbol(order.getSymbol()));
        
        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    /**
     * Sends order using FIX 4.4 format.
     * FIX 4.4 SPECIFICS:
     * - HandlInst set separately (not in constructor)
     */
    public void send44(Order order) {
        quickfix.fix44.NewOrderSingle newOrderSingle = new quickfix.fix44.NewOrderSingle(
                new ClOrdID(order.getID()), 
                sideToFIXSide(order.getSide()),
                new TransactTime(), 
                typeToFIXType(order.getType()));
        newOrderSingle.set(new OrderQty(order.getQuantity()));
        newOrderSingle.set(new Symbol(order.getSymbol()));
        newOrderSingle.set(new HandlInst('1'));
        
        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    /**
     * Sends order using FIX 5.0 (FIXT 1.1) format.
     * FIX 5.0 SPECIFICS:
     * - Same structure as 4.4 for NewOrderSingle
     * - Transport layer (FIXT) separate from application layer
     */
    public void send50(Order order) {
        quickfix.fix50.NewOrderSingle newOrderSingle = new quickfix.fix50.NewOrderSingle(
                new ClOrdID(order.getID()), 
                sideToFIXSide(order.getSide()),
                new TransactTime(), 
                typeToFIXType(order.getType()));
        newOrderSingle.set(new OrderQty(order.getQuantity()));
        newOrderSingle.set(new Symbol(order.getSymbol()));
        newOrderSingle.set(new HandlInst('1'));
        
        send(populateOrder(order, newOrderSingle), order.getSessionID());
    }

    // ========================================================================
    // ORDER POPULATION - Add Type-Specific Fields
    // ========================================================================
    /**
     * Populates order-type-specific fields in message.
     * 
     * FIELD LOGIC:
     * - LIMIT: Set Price
     * - STOP: Set StopPx
     * - STOP_LIMIT: Set both Price and StopPx
     * - MARKET: Neither
     * 
     * SHORT SELLING:
     * - Sets LocateReqd=false for short sales
     * - In production, would handle locate requirements
     * 
     * @param order Source order object
     * @param newOrderSingle Target FIX message
     * @return Populated message
     */
    public quickfix.Message populateOrder(Order order, quickfix.Message newOrderSingle) {

        OrderType type = order.getType();

        if (type == OrderType.LIMIT)
            newOrderSingle.setField(new Price(order.getLimit()));
        else if (type == OrderType.STOP) {
            newOrderSingle.setField(new StopPx(order.getStop()));
        } else if (type == OrderType.STOP_LIMIT) {
            newOrderSingle.setField(new Price(order.getLimit()));
            newOrderSingle.setField(new StopPx(order.getStop()));
        }

        // Handle short selling
        if (order.getSide() == OrderSide.SHORT_SELL
                || order.getSide() == OrderSide.SHORT_SELL_EXEMPT) {
            newOrderSingle.setField(new LocateReqd(false));
        }

        // Set time in force
        newOrderSingle.setField(tifToFIXTif(order.getTIF()));
        return newOrderSingle;
    }

    // ========================================================================
    // ORDER CANCELLATION - Version Router
    // ========================================================================
    /**
     * Cancels order using appropriate FIX version.
     * Only FIX 4.0-4.2 shown (later versions similar).
     * 
     * @param order Order to cancel
     */
    public void cancel(Order order) {
        String beginString = order.getSessionID().getBeginString();
        switch (beginString) {
            case "FIX.4.0":
                cancel40(order);
                break;
            case "FIX.4.1":
                cancel41(order);
                break;
            case "FIX.4.2":
                cancel42(order);
                break;
        }
    }

    // Cancel implementations for each version...
    public void cancel40(Order order) {
        String id = order.generateID();
        quickfix.fix40.OrderCancelRequest message = new quickfix.fix40.OrderCancelRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(id), 
                new CxlType(CxlType.FULL_REMAINING_QUANTITY), 
                new Symbol(order.getSymbol()), 
                sideToFIXSide(order.getSide()), 
                new OrderQty(order.getQuantity()));

        orderTableModel.addID(order, id);
        send(message, order.getSessionID());
    }

    public void cancel41(Order order) {
        String id = order.generateID();
        quickfix.fix41.OrderCancelRequest message = new quickfix.fix41.OrderCancelRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(id), 
                new Symbol(order.getSymbol()),
                sideToFIXSide(order.getSide()));
        message.setField(new OrderQty(order.getQuantity()));

        orderTableModel.addID(order, id);
        send(message, order.getSessionID());
    }

    public void cancel42(Order order) {
        String id = order.generateID();
        quickfix.fix42.OrderCancelRequest message = new quickfix.fix42.OrderCancelRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(id), 
                new Symbol(order.getSymbol()),
                sideToFIXSide(order.getSide()), 
                new TransactTime());
        message.setField(new OrderQty(order.getQuantity()));

        orderTableModel.addID(order, id);
        send(message, order.getSessionID());
    }

    // ========================================================================
    // ORDER REPLACEMENT - Version Router
    // ========================================================================
    /**
     * Replaces order with new parameters using appropriate FIX version.
     * 
     * @param order Original order
     * @param newOrder New order with updated parameters
     */
    public void replace(Order order, Order newOrder) {
        String beginString = order.getSessionID().getBeginString();
        switch (beginString) {
            case "FIX.4.0":
                replace40(order, newOrder);
                break;
            case "FIX.4.1":
                replace41(order, newOrder);
                break;
            case "FIX.4.2":
                replace42(order, newOrder);
                break;
        }
    }

    // Replace implementations for each version...
    public void replace40(Order order, Order newOrder) {
        quickfix.fix40.OrderCancelReplaceRequest message = new quickfix.fix40.OrderCancelReplaceRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(newOrder.getID()), 
                new HandlInst('1'),
                new Symbol(order.getSymbol()), 
                sideToFIXSide(order.getSide()), 
                new OrderQty(newOrder.getQuantity()), 
                typeToFIXType(order.getType()));

        orderTableModel.addID(order, newOrder.getID());
        send(populateCancelReplace(order, newOrder, message), order.getSessionID());
    }

    public void replace41(Order order, Order newOrder) {
        quickfix.fix41.OrderCancelReplaceRequest message = new quickfix.fix41.OrderCancelReplaceRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(newOrder.getID()), 
                new HandlInst('1'),
                new Symbol(order.getSymbol()), 
                sideToFIXSide(order.getSide()), 
                typeToFIXType(order.getType()));

        orderTableModel.addID(order, newOrder.getID());
        send(populateCancelReplace(order, newOrder, message), order.getSessionID());
    }

    public void replace42(Order order, Order newOrder) {
        quickfix.fix42.OrderCancelReplaceRequest message = new quickfix.fix42.OrderCancelReplaceRequest(
                new OrigClOrdID(order.getID()), 
                new ClOrdID(newOrder.getID()), 
                new HandlInst('1'),
                new Symbol(order.getSymbol()), 
                sideToFIXSide(order.getSide()), 
                new TransactTime(),
                typeToFIXType(order.getType()));

        orderTableModel.addID(order, newOrder.getID());
        send(populateCancelReplace(order, newOrder, message), order.getSessionID());
    }

    /**
     * Populates cancel/replace request with changed parameters.
     * Only includes fields that changed (quantity and/or price).
     */
    Message populateCancelReplace(Order order, Order newOrder, quickfix.Message message) {
        if (order.getQuantity() != newOrder.getQuantity())
            message.setField(new OrderQty(newOrder.getQuantity()));
        if (!order.getLimit().equals(newOrder.getLimit()))
            message.setField(new Price(newOrder.getLimit()));
        return message;
    }

    // ========================================================================
    // TYPE CONVERSION METHODS
    // ========================================================================
    // Convert between internal enums and FIX field values using bidirectional maps.
    
    public Side sideToFIXSide(OrderSide side) {
        return (Side) sideMap.getFirst(side);
    }

    public OrderSide FIXSideToSide(Side side) {
        return (OrderSide) sideMap.getSecond(side);
    }

    public OrdType typeToFIXType(OrderType type) {
        return (OrdType) typeMap.getFirst(type);
    }

    public OrderType FIXTypeToType(OrdType type) {
        return (OrderType) typeMap.getSecond(type);
    }

    public TimeInForce tifToFIXTif(OrderTIF tif) {
        return (TimeInForce) tifMap.getFirst(tif);
    }

    public OrderTIF FIXTifToTif(TimeInForce tif) {
        return (OrderTIF) typeMap.getSecond(tif);
    }

    // ========================================================================
    // OBSERVER PATTERN - Event Notification
    // ========================================================================
    
    public void addLogonObserver(Observer observer) {
        observableLogon.addObserver(observer);
    }

    public void deleteLogonObserver(Observer observer) {
        observableLogon.deleteObserver(observer);
    }

    public void addOrderObserver(Observer observer) {
        observableOrder.addObserver(observer);
    }

    public void deleteOrderObserver(Observer observer) {
        observableOrder.deleteObserver(observer);
    }

    // ========================================================================
    // OBSERVABLE WRAPPERS
    // ========================================================================
    
    /**
     * Observable wrapper for order updates.
     * Notifies UI when order state changes.
     */
    private static class ObservableOrder extends Observable {
        public void update(Order order) {
            setChanged();
            notifyObservers(order);
            clearChanged();
        }
    }

    /**
     * Observable wrapper for logon/logoff events.
     * Notifies UI when connection state changes.
     */
    private static class ObservableLogon extends Observable {
        public void logon(SessionID sessionID) {
            setChanged();
            notifyObservers(new LogonEvent(sessionID, true));
            clearChanged();
        }

        public void logoff(SessionID sessionID) {
            setChanged();
            notifyObservers(new LogonEvent(sessionID, false));
            clearChanged();
        }
    }

    // ========================================================================
    // STATIC INITIALIZATION - Type Conversion Maps
    // ========================================================================
    // Initialize bidirectional maps for type conversions.
    // These are shared across all BanzaiApplication instances.
    static {
        // Side conversions
        sideMap.put(OrderSide.BUY, new Side(Side.BUY));
        sideMap.put(OrderSide.SELL, new Side(Side.SELL));
        sideMap.put(OrderSide.SHORT_SELL, new Side(Side.SELL_SHORT));
        sideMap.put(OrderSide.SHORT_SELL_EXEMPT, new Side(Side.SELL_SHORT_EXEMPT));
        sideMap.put(OrderSide.CROSS, new Side(Side.CROSS));
        sideMap.put(OrderSide.CROSS_SHORT, new Side(Side.CROSS_SHORT));

        // Order type conversions
        typeMap.put(OrderType.MARKET, new OrdType(OrdType.MARKET));
        typeMap.put(OrderType.LIMIT, new OrdType(OrdType.LIMIT));
        typeMap.put(OrderType.STOP, new OrdType(OrdType.STOP));
        typeMap.put(OrderType.STOP_LIMIT, new OrdType(OrdType.STOP_LIMIT));

        // Time in force conversions
        tifMap.put(OrderTIF.DAY, new TimeInForce(TimeInForce.DAY));
        tifMap.put(OrderTIF.IOC, new TimeInForce(TimeInForce.IMMEDIATE_OR_CANCEL));
        tifMap.put(OrderTIF.OPG, new TimeInForce(TimeInForce.AT_THE_OPENING));
        tifMap.put(OrderTIF.GTC, new TimeInForce(TimeInForce.GOOD_TILL_CANCEL));
        tifMap.put(OrderTIF.GTX, new TimeInForce(TimeInForce.GOOD_TILL_CROSSING));
    }

    // ========================================================================
    // OPENFIX CERTIFICATION GETTERS/SETTERS
    // ========================================================================
    
    public boolean isMissingField() {
        return isMissingField;
    }

    public void setMissingField(boolean isMissingField) {
        this.isMissingField = isMissingField;
    }

    public boolean isAvailable() {
        return isAvailable;
    }

    public void setAvailable(boolean isAvailable) {
        this.isAvailable = isAvailable;
    }
}
