// ============================================================================
// EXECUTOR - FIX Order Execution Server
// ============================================================================
// This is the main entry point for a Java FIX Executor server that receives
// orders from trading clients and executes them immediately.
//
// PURPOSE:
// Launches a FIX server (acceptor) that:
// - Accepts connections from multiple trading clients
// - Receives NewOrderSingle messages
// - Validates order types and parameters
// - Executes orders (fills them immediately or checks market)
// - Sends ExecutionReport messages back to clients
//
// EXECUTION MODELS SUPPORTED:
// 1. Immediate Fill: All valid orders filled instantly (test mode)
// 2. Market Price Check: Uses MarketDataProvider to check executable price
// 3. Configurable Validation: Only accept specified order types
//
// KEY FEATURES:
// - Multi-client support (accepts multiple connections)
// - Dynamic session provisioning (template-based)
// - JMX monitoring and management
// - Configurable order type validation
// - Pluggable market data sources
// - Graceful shutdown handling
//
// COMPARISON TO C++ VERSION:
// Similar architecture but with Java-specific features:
// - JMX integration for monitoring
// - Dynamic session templates
// - Java logging framework
// - Exception handling vs error codes
//
// CONFIGURATION:
// Executor behavior is controlled via settings file:
// - ValidOrderTypes: Comma-separated list (2=LIMIT, 1=MARKET, etc.)
// - DefaultMarketPrice: Fixed price for market orders
// - AlwaysFillLimitOrders: Y/N - fill limits at their price or check market
// - AcceptorTemplate: Y - enables dynamic session creation
//
// USE CASES:
// - Testing trading clients in development
// - Simulating broker execution for backtesting
// - Educational demonstrations of FIX protocol
// - Prototyping before connecting to real exchanges
//
// PRODUCTION CONSIDERATIONS:
// Real executors would add:
// - Connection to actual exchanges/markets
// - Smart order routing across venues
// - Partial fill logic and order queuing
// - Risk management and position limits
// - Regulatory compliance (order audit trail)
// - Market making and liquidity provision
// ============================================================================

package quickfix.examples.executor;

import org.quickfixj.jmx.JmxExporter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import quickfix.ConfigError;
import quickfix.DefaultMessageFactory;
import quickfix.FieldConvertError;
import quickfix.FileStoreFactory;
import quickfix.LogFactory;
import quickfix.MessageFactory;
import quickfix.MessageStoreFactory;
import quickfix.RuntimeError;
import quickfix.ScreenLogFactory;
import quickfix.SessionID;
import quickfix.SessionSettings;
import quickfix.SocketAcceptor;
import quickfix.mina.acceptor.DynamicAcceptorSessionProvider;
import quickfix.mina.acceptor.DynamicAcceptorSessionProvider.TemplateMapping;

import javax.management.JMException;
import javax.management.ObjectName;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import static quickfix.Acceptor.SETTING_ACCEPTOR_TEMPLATE;
import static quickfix.Acceptor.SETTING_SOCKET_ACCEPT_ADDRESS;
import static quickfix.Acceptor.SETTING_SOCKET_ACCEPT_PORT;

// ============================================================================
// EXECUTOR CLASS - Server Coordinator
// ============================================================================
// Manages the FIX acceptor lifecycle and dynamic session provisioning.
// ============================================================================
public class Executor {
    private final static Logger log = LoggerFactory.getLogger(Executor.class);
    
    // Core FIX server component
    private final SocketAcceptor acceptor;
    
    // Dynamic session mappings: socket address -> session templates
    // Allows creating sessions on-demand when clients connect
    private final Map<InetSocketAddress, List<TemplateMapping>> dynamicSessionMappings = new HashMap<>();

    // JMX monitoring components
    private final JmxExporter jmxExporter;
    private final ObjectName connectorObjectName;

    // ========================================================================
    // CONSTRUCTOR - Initialize Executor Server
    // ========================================================================
    /**
     * Creates and configures the Executor server.
     * 
     * INITIALIZATION SEQUENCE:
     * 1. Create Application (order handling logic)
     * 2. Create MessageStore (persistence)
     * 3. Create LogFactory (logging)
     * 4. Create MessageFactory (message parsing)
     * 5. Create SocketAcceptor (server)
     * 6. Configure dynamic sessions (if templates present)
     * 7. Register with JMX (monitoring)
     * 
     * DYNAMIC SESSIONS:
     * If configuration includes session templates (AcceptorTemplate=Y),
     * the executor can create new sessions automatically when clients
     * connect, without restarting or reconfiguring.
     * 
     * BENEFITS:
     * - Support unlimited clients without pre-configuration
     * - Hot-add new clients
     * - Simplify deployment
     * 
     * @param settings QuickFIX configuration
     * @throws ConfigError If configuration is invalid
     * @throws FieldConvertError If field conversion fails
     * @throws JMException If JMX registration fails
     */
    public Executor(SessionSettings settings) throws ConfigError, FieldConvertError, JMException {
        // ====================================================================
        // CREATE CORE COMPONENTS
        // ====================================================================
        
        // Application: Handles FIX message processing
        // Contains order validation and execution logic
        Application application = new Application(settings);
        
        // MessageStoreFactory: Persists FIX messages to files
        // Enables crash recovery and message replay
        MessageStoreFactory messageStoreFactory = new FileStoreFactory(settings);
        
        // LogFactory: Outputs messages to console
        // Parameters: (incoming, outgoing, events, heartbeats)
        LogFactory logFactory = new ScreenLogFactory(true, true, true);
        
        // MessageFactory: Creates FIX message objects from bytes
        // Handles parsing and validation
        MessageFactory messageFactory = new DefaultMessageFactory();

        // ====================================================================
        // CREATE SOCKET ACCEPTOR
        // ====================================================================
        // SocketAcceptor listens for TCP connections from FIX clients
        // Manages multiple sessions simultaneously
        acceptor = new SocketAcceptor(application, messageStoreFactory, settings, logFactory,
                messageFactory);

        // ====================================================================
        // CONFIGURE DYNAMIC SESSIONS
        // ====================================================================
        // If configuration includes session templates, set up dynamic
        // session provider to auto-create sessions for new clients
        configureDynamicSessions(settings, application, messageStoreFactory, logFactory,
                messageFactory);

        // ====================================================================
        // JMX REGISTRATION
        // ====================================================================
        // Register acceptor with JMX for runtime monitoring and management
        // Allows viewing:
        // - Active sessions
        // - Message counts
        // - Connection status
        // - Triggering logon/logout
        // 
        // Access via: jconsole or visualvm
        jmxExporter = new JmxExporter();
        connectorObjectName = jmxExporter.register(acceptor);
        log.info("Acceptor registered with JMX, name={}", connectorObjectName);
    }

    // ========================================================================
    // DYNAMIC SESSION CONFIGURATION
    // ========================================================================
    /**
     * Configures dynamic session providers for template-based sessions.
     * 
     * DYNAMIC SESSION PROVISIONING:
     * Traditional FIX servers require each session to be pre-configured.
     * Dynamic provisioning allows:
     * - Template sessions (AcceptorTemplate=Y)
     * - Automatic session creation when client connects
     * - SenderCompID/TargetCompID from client's Logon message
     * 
     * HOW IT WORKS:
     * 1. Configuration includes template session
     * 2. Client connects to acceptor port
     * 3. Client sends Logon with SenderCompID/TargetCompID
     * 4. Server creates session dynamically from template
     * 5. Session operates normally
     * 
     * BENEFITS:
     * - Support many clients without individual config
     * - Add clients without server restart
     * - Simplify multi-tenant deployments
     * 
     * @param settings Configuration settings
     * @param application FIX application handler
     * @param messageStoreFactory Message persistence
     * @param logFactory Logging
     * @param messageFactory Message parsing
     */
    private void configureDynamicSessions(SessionSettings settings, Application application,
            MessageStoreFactory messageStoreFactory, LogFactory logFactory,
            MessageFactory messageFactory) throws ConfigError, FieldConvertError {
        
        // Iterate through all configured sessions
        Iterator<SessionID> sectionIterator = settings.sectionIterator();
        while (sectionIterator.hasNext()) {
            SessionID sessionID = sectionIterator.next();
            
            // Check if this session is a template
            if (isSessionTemplate(settings, sessionID)) {
                // Get socket address (host:port) for this template
                InetSocketAddress address = getAcceptorSocketAddress(settings, sessionID);
                
                // Register template mapping for this address
                // Uses same template for both initiator and acceptor IDs
                getMappings(address).add(new TemplateMapping(sessionID, sessionID));
            }
        }

        // Register dynamic session providers with acceptor
        // Each socket address gets its own provider with its templates
        for (Map.Entry<InetSocketAddress, List<TemplateMapping>> entry : dynamicSessionMappings
                .entrySet()) {
            acceptor.setSessionProvider(entry.getKey(), new DynamicAcceptorSessionProvider(
                    settings, entry.getValue(), application, messageStoreFactory, logFactory,
                    messageFactory));
        }
    }

    /**
     * Gets or creates mapping list for socket address.
     * Uses computeIfAbsent for thread-safe lazy initialization.
     */
    private List<TemplateMapping> getMappings(InetSocketAddress address) {
        return dynamicSessionMappings.computeIfAbsent(address, k -> new ArrayList<>());
    }

    /**
     * Extracts socket address (host:port) from session configuration.
     * 
     * @param settings Configuration
     * @param sessionID Session to extract address for
     * @return Socket address to listen on
     */
    private InetSocketAddress getAcceptorSocketAddress(SessionSettings settings, SessionID sessionID)
            throws ConfigError, FieldConvertError {
        // Default to all interfaces (0.0.0.0)
        String acceptorHost = "0.0.0.0";
        if (settings.isSetting(sessionID, SETTING_SOCKET_ACCEPT_ADDRESS)) {
            acceptorHost = settings.getString(sessionID, SETTING_SOCKET_ACCEPT_ADDRESS);
        }
        
        // Port is required
        int acceptorPort = (int) settings.getLong(sessionID, SETTING_SOCKET_ACCEPT_PORT);

        return new InetSocketAddress(acceptorHost, acceptorPort);
    }

    /**
     * Checks if session is configured as a template.
     * Template sessions have AcceptorTemplate=Y setting.
     */
    private boolean isSessionTemplate(SessionSettings settings, SessionID sessionID)
            throws ConfigError, FieldConvertError {
        return settings.isSetting(sessionID, SETTING_ACCEPTOR_TEMPLATE)
                && settings.getBool(sessionID, SETTING_ACCEPTOR_TEMPLATE);
    }

    // ========================================================================
    // SERVER LIFECYCLE MANAGEMENT
    // ========================================================================
    
    /**
     * Starts the FIX acceptor server.
     * 
     * ACTIONS PERFORMED:
     * - Binds to configured port
     * - Starts listening for connections
     * - Begins accepting FIX sessions
     * - Starts heartbeat monitors
     * 
     * BLOCKING:
     * This method returns immediately. Server runs in background threads.
     * 
     * @throws RuntimeError If server cannot start
     * @throws ConfigError If configuration is invalid
     */
    private void start() throws RuntimeError, ConfigError {
        acceptor.start();
    }

    /**
     * Stops the FIX acceptor server gracefully.
     * 
     * SHUTDOWN SEQUENCE:
     * 1. Stop accepting new connections
     * 2. Send Logout to all connected clients
     * 3. Wait for Logout responses
     * 4. Close all connections
     * 5. Flush message stores
     * 6. Unregister from JMX
     * 
     * IMPORTANT:
     * Allows time for graceful disconnect before terminating.
     * In production, may want timeout for unresponsive clients.
     */
    private void stop() {
        try {
            // Unregister from JMX monitoring
            jmxExporter.getMBeanServer().unregisterMBean(connectorObjectName);
        } catch (Exception e) {
            log.error("Failed to unregister acceptor from JMX", e);
        }
        
        // Stop acceptor (sends Logout, closes connections)
        acceptor.stop();
    }

    // ========================================================================
    // MAIN METHOD - Application Entry Point
    // ========================================================================
    /**
     * Starts the Executor server and waits for shutdown signal.
     * 
     * EXECUTION FLOW:
     * 1. Load configuration file
     * 2. Create Executor instance
     * 3. Start server
     * 4. Wait for user to press Enter
     * 5. Stop server gracefully
     * 
     * CONFIGURATION FILE FORMAT:
     * Standard QuickFIX settings:
     * [DEFAULT]
     * ConnectionType=acceptor
     * SocketAcceptPort=5001
     * FileStorePath=data/executor
     * ValidOrderTypes=2    # Only LIMIT orders
     * DefaultMarketPrice=100.0
     * AlwaysFillLimitOrders=Y
     * 
     * [SESSION]
     * BeginString=FIX.4.2
     * SenderCompID=EXECUTOR
     * TargetCompID=CLIENT
     * 
     * SHUTDOWN:
     * Press Enter key to initiate graceful shutdown.
     * Server will send Logout to all clients before terminating.
     * 
     * @param args Optional: path to configuration file
     */
    public static void main(String[] args) throws Exception {
        try {
            // ================================================================
            // LOAD CONFIGURATION
            // ================================================================
            InputStream inputStream = getSettingsInputStream(args);
            SessionSettings settings = new SessionSettings(inputStream);
            inputStream.close();

            // ================================================================
            // CREATE AND START EXECUTOR
            // ================================================================
            Executor executor = new Executor(settings);
            executor.start();

            // ================================================================
            // WAIT FOR SHUTDOWN SIGNAL
            // ================================================================
            // Simple blocking wait for user input
            // In production, might use:
            // - Signal handlers (SIGTERM, SIGINT)
            // - Admin port for remote shutdown
            // - Health check endpoint
            // - Scheduled shutdown time
            System.out.println("press <enter> to quit");
            System.in.read();

            // ================================================================
            // GRACEFUL SHUTDOWN
            // ================================================================
            executor.stop();
            
        } catch (Exception e) {
            log.error(e.getMessage(), e);
        }
    }

    /**
     * Loads configuration from file or embedded resource.
     * 
     * CONFIGURATION SOURCES:
     * 1. No args: Load embedded executor.cfg resource
     * 2. One arg: Load from specified file path
     * 
     * @param args Command line arguments
     * @return InputStream for configuration
     * @throws FileNotFoundException If file not found
     */
    private static InputStream getSettingsInputStream(String[] args) throws FileNotFoundException {
        InputStream inputStream = null;
        
        if (args.length == 0) {
            // No arguments: use embedded default configuration
            inputStream = Executor.class.getResourceAsStream("executor.cfg");
        } else if (args.length == 1) {
            // One argument: use specified configuration file
            inputStream = new FileInputStream(args[0]);
        }
        
        if (inputStream == null) {
            System.out.println("usage: " + Executor.class.getName() + " [configFile].");
            System.exit(1);
        }
        
        return inputStream;
    }
}
