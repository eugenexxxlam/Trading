// ============================================================================
// EXECUTOR APPLICATION - FIX Order Execution Logic
// ============================================================================
// This class implements the core order execution logic for the Executor server.
// It receives orders from clients and executes them immediately (or checks market).
//
// PURPOSE:
// - Process NewOrderSingle messages from clients
// - Validate order types and parameters  
// - Determine execution price (fixed, market, or limit price)
// - Send ExecutionReport messages (NEW status + FILLED status)
// - Support multiple FIX versions (4.0 through 5.0)
//
// EXECUTION MODELS:
// 1. **Always Fill Limit** (AlwaysFillLimitOrders=Y):
//    - All LIMIT orders filled at their limit price immediately
//    - Simulates instant execution (test mode)
//
// 2. **Market Check** (AlwaysFillLimitOrders=N):
//    - Uses MarketDataProvider to get current prices
//    - BUY order executable if limit >= ask price
//    - SELL order executable if limit <= bid price
//    - Market orders always executable at market price
//
// 3. **Order Type Validation** (ValidOrderTypes setting):
//    - Only accepts configured order types
//    - Rejects unsupported types
//    - Common: "2" (LIMIT only) or "1,2" (MARKET and LIMIT)
//
// MESSAGE FLOW:
// Client → NewOrderSingle → Executor
// Executor → ExecutionReport (Status=NEW) → Client
// Executor → ExecutionReport (Status=FILLED) → Client
//
// COMPARISON TO C++ VERSION:
// Same logic but Java implementation:
// - Uses QuickFIX/J library (Java port)
// - Java exception handling vs C++ error codes
// - Java logging framework (SLF4J)
// - Anonymous inner class for market data provider
//
// CONFIGURATION SETTINGS:
// - ValidOrderTypes: "1,2,3,4" (MARKET,LIMIT,STOP,STOP_LIMIT)
// - DefaultMarketPrice: "100.0" (fixed price for market orders)
// - AlwaysFillLimitOrders: "Y" or "N" (immediate fill vs market check)
//
// PRODUCTION CONSIDERATIONS:
// Real executors would add:
// - Smart order routing to multiple venues
// - Partial fill logic and order queuing
// - Market making and liquidity provision
// - Risk checks and position limits
// - Compliance and audit logging
// ============================================================================

package quickfix.examples.executor;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import quickfix.ConfigError;
import quickfix.DataDictionaryProvider;
import quickfix.DoNotSend;
import quickfix.FieldConvertError;
import quickfix.FieldNotFound;
import quickfix.FixVersions;
import quickfix.IncorrectDataFormat;
import quickfix.IncorrectTagValue;
import quickfix.LogUtil;
import quickfix.Message;
import quickfix.MessageUtils;
import quickfix.RejectLogon;
import quickfix.Session;
import quickfix.SessionID;
import quickfix.SessionNotFound;
import quickfix.SessionSettings;
import quickfix.UnsupportedMessageType;
import quickfix.field.ApplVerID;
import quickfix.field.AvgPx;
import quickfix.field.CumQty;
import quickfix.field.ExecID;
import quickfix.field.ExecTransType;
import quickfix.field.ExecType;
import quickfix.field.LastPx;
import quickfix.field.LastQty;
import quickfix.field.LastShares;
import quickfix.field.LeavesQty;
import quickfix.field.OrdStatus;
import quickfix.field.OrdType;
import quickfix.field.OrderID;
import quickfix.field.OrderQty;
import quickfix.field.Price;
import quickfix.field.Side;
import quickfix.field.Symbol;

import java.math.BigDecimal;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;

// ============================================================================
// EXECUTOR APPLICATION CLASS
// ============================================================================
// Extends MessageCracker for automatic message routing to type-specific handlers.
// Implements Application interface for FIX session callbacks.
// ============================================================================
public class Application extends quickfix.MessageCracker implements quickfix.Application {
    // ========================================================================
    // CONFIGURATION KEYS
    // ========================================================================
    private static final String DEFAULT_MARKET_PRICE_KEY = "DefaultMarketPrice";
    private static final String ALWAYS_FILL_LIMIT_KEY = "AlwaysFillLimitOrders";
    private static final String VALID_ORDER_TYPES_KEY = "ValidOrderTypes";

    // ========================================================================
    // STATE AND CONFIGURATION
    // ========================================================================
    private final Logger log = LoggerFactory.getLogger(getClass());
    
    // Execution mode: true = fill all limits at their price, false = check market
    private final boolean alwaysFillLimitOrders;
    
    // Set of valid order type codes (e.g., "1" for MARKET, "2" for LIMIT)
    private final HashSet<String> validOrderTypes = new HashSet<>();
    
    // Market data source (can be fixed price or pluggable provider)
    private MarketDataProvider marketDataProvider;

    // ========================================================================
    // CONSTRUCTOR - Initialize from Configuration
    // ========================================================================
    /**
     * Creates Executor Application with configuration.
     * 
     * INITIALIZATION:
     * 1. Parse ValidOrderTypes setting
     * 2. Create MarketDataProvider if DefaultMarketPrice set
     * 3. Set AlwaysFillLimitOrders flag
     * 
     * @param settings QuickFIX configuration
     * @throws ConfigError If configuration invalid
     * @throws FieldConvertError If field conversion fails
     */
    public Application(SessionSettings settings) throws ConfigError, FieldConvertError {
        initializeValidOrderTypes(settings);
        initializeMarketDataProvider(settings);

        alwaysFillLimitOrders = settings.isSetting(ALWAYS_FILL_LIMIT_KEY) 
            && settings.getBool(ALWAYS_FILL_LIMIT_KEY);
    }

    // ========================================================================
    // CONFIGURATION INITIALIZATION
    // ========================================================================
    
    /**
     * Creates market data provider from configuration.
     * 
     * DEFAULT MARKET PRICE:
     * If set, creates simple provider that returns fixed price for all symbols.
     * This is useful for testing without real market data.
     * 
     * ANONYMOUS INNER CLASS:
     * Uses Java anonymous class to create inline MarketDataProvider implementation.
     * 
     * PRODUCTION:
     * Call setMarketDataProvider() to inject real market data source.
     * 
     * @param settings Configuration
     */
    private void initializeMarketDataProvider(SessionSettings settings) throws ConfigError, FieldConvertError {
        if (settings.isSetting(DEFAULT_MARKET_PRICE_KEY)) {
            if (marketDataProvider == null) {
                final double defaultMarketPrice = settings.getDouble(DEFAULT_MARKET_PRICE_KEY);
                
                // Create anonymous MarketDataProvider that returns fixed price
                marketDataProvider = new MarketDataProvider() {
                    public double getAsk(String symbol) {
                        return defaultMarketPrice;
                    }

                    public double getBid(String symbol) {
                        return defaultMarketPrice;
                    }
                };
            } else {
                log.warn("Ignoring {} since provider is already defined.", DEFAULT_MARKET_PRICE_KEY);
            }
        }
    }

    /**
     * Parses valid order types from configuration.
     * 
     * FORMAT:
     * Comma-separated list of FIX OrdType values:
     * - "1" = MARKET
     * - "2" = LIMIT
     * - "3" = STOP
     * - "4" = STOP_LIMIT
     * 
     * Example: "1,2" accepts MARKET and LIMIT only
     * 
     * DEFAULT:
     * If not specified, only LIMIT orders (type 2) accepted.
     * 
     * @param settings Configuration
     */
    private void initializeValidOrderTypes(SessionSettings settings) throws ConfigError, FieldConvertError {
        if (settings.isSetting(VALID_ORDER_TYPES_KEY)) {
            // Parse comma-separated list
            List<String> orderTypes = Arrays
                    .asList(settings.getString(VALID_ORDER_TYPES_KEY).trim().split("\\s*,\\s*"));
            validOrderTypes.addAll(orderTypes);
        } else {
            // Default: only LIMIT orders
            validOrderTypes.add(OrdType.LIMIT + "");
        }
    }

    // ========================================================================
    // FIX SESSION LIFECYCLE CALLBACKS
    // ========================================================================
    
    /**
     * Called when FIX session is created.
     * Logs valid order types for this session.
     */
    public void onCreate(SessionID sessionID) {
        Session.lookupSession(sessionID).getLog().onEvent("Valid order types: " + validOrderTypes);
    }

    public void onLogon(SessionID sessionID) {
    }

    public void onLogout(SessionID sessionID) {
    }

    public void toAdmin(quickfix.Message message, SessionID sessionID) {
    }

    public void toApp(quickfix.Message message, SessionID sessionID) throws DoNotSend {
    }

    public void fromAdmin(quickfix.Message message, SessionID sessionID) throws FieldNotFound, IncorrectDataFormat,
            IncorrectTagValue, RejectLogon {
    }

    /**
     * Routes incoming application messages to type-specific handlers.
     * Uses MessageCracker.crack() to dispatch based on message type and FIX version.
     */
    public void fromApp(quickfix.Message message, SessionID sessionID) throws FieldNotFound, IncorrectDataFormat,
            IncorrectTagValue, UnsupportedMessageType {
        crack(message, sessionID);
    }

    // ========================================================================
    // FIX 4.0 NEW ORDER HANDLER
    // ========================================================================
    /**
     * Processes FIX 4.0 NewOrderSingle messages.
     * 
     * FLOW:
     * 1. Validate order type
     * 2. Determine execution price
     * 3. Send ExecutionReport with NEW status
     * 4. Check if executable
     * 5. If yes, send ExecutionReport with FILLED status
     * 
     * FIX 4.0 SPECIFICS:
     * - Uses ExecTransType (deprecated in later versions)
     * - Uses LastShares instead of LastQty
     * - Different constructor parameters
     */
    public void onMessage(quickfix.fix40.NewOrderSingle order, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        try {
            // Validate order type is in validOrderTypes set
            validateOrder(order);

            OrderQty orderQty = order.getOrderQty();

            // Determine execution price based on order type and settings
            Price price = getPrice(order);

            // ================================================================
            // SEND ACKNOWLEDGMENT (NEW STATUS)
            // ================================================================
            quickfix.fix40.ExecutionReport accept = new quickfix.fix40.ExecutionReport(
                    genOrderID(),                           // Executor-assigned order ID
                    genExecID(),                           // Execution ID
                    new ExecTransType(ExecTransType.NEW),  // FIX 4.0 required field
                    new OrdStatus(OrdStatus.NEW),          // Order accepted, resting
                    order.getSymbol(),
                    order.getSide(),
                    orderQty,
                    new LastShares(0),                     // No fill yet
                    new LastPx(0),
                    new CumQty(0),                         // No cumulative fills
                    new AvgPx(0));

            accept.set(order.getClOrdID());  // Echo client order ID
            sendMessage(sessionID, accept);

            // ================================================================
            // CHECK IF ORDER EXECUTABLE
            // ================================================================
            if (isOrderExecutable(order, price)) {
                // Order can execute - send fill report
                quickfix.fix40.ExecutionReport fill = new quickfix.fix40.ExecutionReport(
                        genOrderID(),
                        genExecID(),
                        new ExecTransType(ExecTransType.NEW),
                        new OrdStatus(OrdStatus.FILLED),           // Completely filled
                        order.getSymbol(),
                        order.getSide(),
                        orderQty,
                        new LastShares(orderQty.getValue()),       // Filled entire quantity
                        new LastPx(price.getValue()),              // Fill price
                        new CumQty(orderQty.getValue()),           // All filled
                        new AvgPx(price.getValue()));              // Average price

                fill.set(order.getClOrdID());

                sendMessage(sessionID, fill);
            }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    // ========================================================================
    // ORDER EXECUTABILITY CHECK
    // ========================================================================
    /**
     * Determines if order can be executed.
     * 
     * MARKET ORDERS:
     * Always executable (assume infinite liquidity at market price)
     * 
     * LIMIT ORDERS:
     * Executable if limit price crosses market:
     * - BUY:  limit >= ask  (willing to pay at least ask)
     * - SELL: limit <= bid  (willing to receive at most bid)
     * 
     * EXAMPLES:
     * - BUY 100 @ $105 limit, market ask = $104 → EXECUTABLE (limit > ask)
     * - BUY 100 @ $103 limit, market ask = $104 → NOT EXECUTABLE (limit < ask)
     * - SELL 100 @ $99 limit, market bid = $100 → EXECUTABLE (limit < bid)
     * - SELL 100 @ $101 limit, market bid = $100 → NOT EXECUTABLE (limit > bid)
     * 
     * @param order Order to check
     * @param price Execution price from getPrice()
     * @return true if order can execute immediately
     */
    private boolean isOrderExecutable(Message order, Price price) throws FieldNotFound {
        if (order.getChar(OrdType.FIELD) == OrdType.LIMIT) {
            BigDecimal limitPrice = new BigDecimal(order.getString(Price.FIELD));
            char side = order.getChar(Side.FIELD);
            BigDecimal thePrice = new BigDecimal("" + price.getValue());

            // BUY:  executable if limit >= market ask
            // SELL: executable if limit <= market bid
            return (side == Side.BUY && thePrice.compareTo(limitPrice) <= 0)
                    || ((side == Side.SELL || side == Side.SELL_SHORT) && thePrice.compareTo(limitPrice) >= 0);
        }
        // MARKET orders always executable
        return true;
    }

    // ========================================================================
    // PRICE DETERMINATION
    // ========================================================================
    /**
     * Determines execution price for an order.
     * 
     * LOGIC:
     * 1. LIMIT order + AlwaysFillLimitOrders=Y → Use limit price
     * 2. LIMIT order + AlwaysFillLimitOrders=N → Use market price
     * 3. MARKET order → Use market price
     * 
     * MARKET PRICE DETERMINATION:
     * - BUY order: Use ask price (price to lift offer)
     * - SELL order: Use bid price (price to hit bid)
     * 
     * @param message Order message
     * @return Price to execute at
     */
    private Price getPrice(Message message) throws FieldNotFound {
        Price price;
        if (message.getChar(OrdType.FIELD) == OrdType.LIMIT && alwaysFillLimitOrders) {
            // LIMIT order in "always fill" mode: use limit price
            price = new Price(message.getDouble(Price.FIELD));
        } else {
            // Need market price
            if (marketDataProvider == null) {
                throw new RuntimeException("No market data provider specified for market order");
            }
            
            char side = message.getChar(Side.FIELD);
            if (side == Side.BUY) {
                // BUY: pay the ask (offer) price
                price = new Price(marketDataProvider.getAsk(message.getString(Symbol.FIELD)));
            } else if (side == Side.SELL || side == Side.SELL_SHORT) {
                // SELL: receive the bid price
                price = new Price(marketDataProvider.getBid(message.getString(Symbol.FIELD)));
            } else {
                throw new RuntimeException("Invalid order side: " + side);
            }
        }
        return price;
    }

    // ========================================================================
    // MESSAGE SENDING WITH VALIDATION
    // ========================================================================
    /**
     * Sends message with optional validation.
     * 
     * VALIDATION:
     * If DataDictionary configured, validates message before sending.
     * Ensures all required fields present and values valid.
     * 
     * @param sessionID Target session
     * @param message Message to send
     */
    private void sendMessage(SessionID sessionID, Message message) {
        try {
            Session session = Session.lookupSession(sessionID);
            if (session == null) {
                throw new SessionNotFound(sessionID.toString());
            }

            // Validate message if data dictionary available
            DataDictionaryProvider dataDictionaryProvider = session.getDataDictionaryProvider();
            if (dataDictionaryProvider != null) {
                try {
                    dataDictionaryProvider.getApplicationDataDictionary(
                            getApplVerID(session, message)).validate(message, true, session.getValidationSettings());
                } catch (Exception e) {
                    LogUtil.logThrowable(sessionID, "Outgoing message failed validation: "
                            + e.getMessage(), e);
                    return;
                }
            }

            session.send(message);
        } catch (SessionNotFound e) {
            log.error(e.getMessage(), e);
        }
    }

    /**
     * Gets ApplVerID for message validation.
     * FIX 5.0 (FIXT) uses separate application version.
     */
    private ApplVerID getApplVerID(Session session, Message message) {
        String beginString = session.getSessionID().getBeginString();
        if (FixVersions.BEGINSTRING_FIXT11.equals(beginString)) {
            return new ApplVerID(ApplVerID.FIX50);
        } else {
            return MessageUtils.toApplVerID(beginString);
        }
    }

    // ========================================================================
    // FIX 4.1+ MESSAGE HANDLERS
    // ========================================================================
    // Similar to FIX 4.0 but with version-specific differences
    // Each version has slightly different required fields and constructors
    
    public void onMessage(quickfix.fix41.NewOrderSingle order, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        try {
        validateOrder(order);

        OrderQty orderQty = order.getOrderQty();
        Price price = getPrice(order);

        // FIX 4.1: Added ExecType and LeavesQty fields
        quickfix.fix41.ExecutionReport accept = new quickfix.fix41.ExecutionReport(genOrderID(), genExecID(),
                new ExecTransType(ExecTransType.NEW), new ExecType(ExecType.NEW), new OrdStatus(OrdStatus.NEW), order
                        .getSymbol(), order.getSide(), orderQty, new LastShares(0), new LastPx(0), new LeavesQty(0),
                new CumQty(0), new AvgPx(0));

        accept.set(order.getClOrdID());
        sendMessage(sessionID, accept);

        if (isOrderExecutable(order, price)) {
            quickfix.fix41.ExecutionReport executionReport = new quickfix.fix41.ExecutionReport(genOrderID(),
                    genExecID(), new ExecTransType(ExecTransType.NEW), new ExecType(ExecType.FILL), new OrdStatus(
                            OrdStatus.FILLED), order.getSymbol(), order.getSide(), orderQty, new LastShares(orderQty
                            .getValue()), new LastPx(price.getValue()), new LeavesQty(0), new CumQty(orderQty
                            .getValue()), new AvgPx(price.getValue()));

            executionReport.set(order.getClOrdID());

            sendMessage(sessionID, executionReport);
        }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    public void onMessage(quickfix.fix42.NewOrderSingle order, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        try {
        validateOrder(order);

        OrderQty orderQty = order.getOrderQty();
        Price price = getPrice(order);

        // FIX 4.2: Removed some constructor fields, set separately
        quickfix.fix42.ExecutionReport accept = new quickfix.fix42.ExecutionReport(genOrderID(), genExecID(),
                new ExecTransType(ExecTransType.NEW), new ExecType(ExecType.NEW), new OrdStatus(OrdStatus.NEW), order
                        .getSymbol(), order.getSide(), new LeavesQty(0), new CumQty(0), new AvgPx(0));

        accept.set(order.getClOrdID());
        sendMessage(sessionID, accept);

        if (isOrderExecutable(order, price)) {
            quickfix.fix42.ExecutionReport executionReport = new quickfix.fix42.ExecutionReport(genOrderID(),
                    genExecID(), new ExecTransType(ExecTransType.NEW), new ExecType(ExecType.FILL), new OrdStatus(
                            OrdStatus.FILLED), order.getSymbol(), order.getSide(), new LeavesQty(0), new CumQty(
                            orderQty.getValue()), new AvgPx(price.getValue()));

            executionReport.set(order.getClOrdID());
            executionReport.set(orderQty);
            executionReport.set(new LastShares(orderQty.getValue()));
            executionReport.set(new LastPx(price.getValue()));

            sendMessage(sessionID, executionReport);
        }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    /**
     * Validates order against configured rules.
     * 
     * CHECKS:
     * 1. Order type in validOrderTypes set
     * 2. If MARKET order, marketDataProvider configured
     * 
     * @param order Order to validate
     * @throws IncorrectTagValue if validation fails
     */
    private void validateOrder(Message order) throws IncorrectTagValue, FieldNotFound {
        OrdType ordType = new OrdType(order.getChar(OrdType.FIELD));
        
        // Check if order type is valid
        if (!validOrderTypes.contains(Character.toString(ordType.getValue()))) {
            log.error("Order type not in ValidOrderTypes setting");
            throw new IncorrectTagValue(ordType.getField());
        }
        
        // Check if market order without market data
        if (ordType.getValue() == OrdType.MARKET && marketDataProvider == null) {
            log.error("DefaultMarketPrice setting not specified for market order");
            throw new IncorrectTagValue(ordType.getField());
        }
    }

    public void onMessage(quickfix.fix43.NewOrderSingle order, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        try {
        validateOrder(order);

        OrderQty orderQty = order.getOrderQty();
        Price price = getPrice(order);

        // FIX 4.3: Removed ExecTransType, changed ExecType values
        quickfix.fix43.ExecutionReport accept = new quickfix.fix43.ExecutionReport(
                    genOrderID(), genExecID(), new ExecType(ExecType.NEW), new OrdStatus(
                            OrdStatus.NEW), order.getSide(), new LeavesQty(order.getOrderQty()
                            .getValue()), new CumQty(0), new AvgPx(0));

        accept.set(order.getClOrdID());
        accept.set(order.getSymbol());
        sendMessage(sessionID, accept);

        if (isOrderExecutable(order, price)) {
            quickfix.fix43.ExecutionReport executionReport = new quickfix.fix43.ExecutionReport(genOrderID(),
                    genExecID(), new ExecType(ExecType.TRADE), new OrdStatus(OrdStatus.FILLED), order.getSide(),
                    new LeavesQty(0), new CumQty(orderQty.getValue()), new AvgPx(price.getValue()));

            executionReport.set(order.getClOrdID());
            executionReport.set(order.getSymbol());
            executionReport.set(orderQty);
            executionReport.set(new LastQty(orderQty.getValue()));  // Changed from LastShares
            executionReport.set(new LastPx(price.getValue()));

            sendMessage(sessionID, executionReport);
        }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    public void onMessage(quickfix.fix44.NewOrderSingle order, SessionID sessionID) throws FieldNotFound,
            UnsupportedMessageType, IncorrectTagValue {
        try {
        validateOrder(order);

        OrderQty orderQty = order.getOrderQty();
        Price price = getPrice(order);

        // FIX 4.4: Similar to 4.3, ExecType TRADE for fills
        quickfix.fix44.ExecutionReport accept = new quickfix.fix44.ExecutionReport(
                    genOrderID(), genExecID(), new ExecType(ExecType.NEW), new OrdStatus(
                            OrdStatus.NEW), order.getSide(), new LeavesQty(order.getOrderQty()
                            .getValue()), new CumQty(0), new AvgPx(0));

        accept.set(order.getClOrdID());
        accept.set(order.getSymbol());
        sendMessage(sessionID, accept);

        if (isOrderExecutable(order, price)) {
            quickfix.fix44.ExecutionReport executionReport = new quickfix.fix44.ExecutionReport(genOrderID(),
                    genExecID(), new ExecType(ExecType.TRADE), new OrdStatus(OrdStatus.FILLED), order.getSide(),
                    new LeavesQty(0), new CumQty(orderQty.getValue()), new AvgPx(price.getValue()));

            executionReport.set(order.getClOrdID());
            executionReport.set(order.getSymbol());
            executionReport.set(orderQty);
            executionReport.set(new LastQty(orderQty.getValue()));
            executionReport.set(new LastPx(price.getValue()));

            sendMessage(sessionID, executionReport);
        }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    public void onMessage(quickfix.fix50.NewOrderSingle order, SessionID sessionID)
            throws FieldNotFound, UnsupportedMessageType, IncorrectTagValue {
        try {
            validateOrder(order);

            OrderQty orderQty = order.getOrderQty();
            Price price = getPrice(order);

            // FIX 5.0: Removed AvgPx from constructor
            quickfix.fix50.ExecutionReport accept = new quickfix.fix50.ExecutionReport(
                    genOrderID(), genExecID(), new ExecType(ExecType.NEW), new OrdStatus(
                            OrdStatus.NEW), order.getSide(), new LeavesQty(order.getOrderQty()
                            .getValue()), new CumQty(0));

            accept.set(order.getClOrdID());
            accept.set(order.getSymbol());
            sendMessage(sessionID, accept);

            if (isOrderExecutable(order, price)) {
                quickfix.fix50.ExecutionReport executionReport = new quickfix.fix50.ExecutionReport(
                        genOrderID(), genExecID(), new ExecType(ExecType.TRADE), new OrdStatus(
                                OrdStatus.FILLED), order.getSide(), new LeavesQty(0), new CumQty(
                                orderQty.getValue()));

                executionReport.set(order.getClOrdID());
                executionReport.set(order.getSymbol());
                executionReport.set(orderQty);
                executionReport.set(new LastQty(orderQty.getValue()));
                executionReport.set(new LastPx(price.getValue()));
                executionReport.set(new AvgPx(price.getValue()));

                sendMessage(sessionID, executionReport);
            }
        } catch (RuntimeException e) {
            LogUtil.logThrowable(sessionID, e.getMessage(), e);
        }
    }

    // ========================================================================
    // ID GENERATION
    // ========================================================================
    /**
     * Generates unique OrderID.
     * Simple counter-based approach.
     * Production would use persistent, distributed ID generation.
     */
    public OrderID genOrderID() {
        return new OrderID(Integer.toString(++m_orderID));
    }

    /**
     * Generates unique ExecID.
     * Each fill gets unique execution ID for tracking.
     */
    public ExecID genExecID() {
        return new ExecID(Integer.toString(++m_execID));
    }

    /**
     * Allows custom market data provider to be injected.
     * Supports testing and integration with real market data sources.
     * 
     * @param marketDataProvider Custom market data provider
     */
    public void setMarketDataProvider(MarketDataProvider marketDataProvider) {
        this.marketDataProvider = marketDataProvider;
    }

    // ID counters (not thread-safe, fine for simple executor)
    private int m_orderID = 0;
    private int m_execID = 0;
}
