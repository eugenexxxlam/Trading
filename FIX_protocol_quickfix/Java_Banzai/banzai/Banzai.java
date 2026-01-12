// ============================================================================
// BANZAI - GUI-Based FIX Trading Client
// ============================================================================
// This is the main entry point for the Banzai application, a professional
// graphical trading client built with Java Swing that connects to FIX servers
// for electronic trading.
//
// PURPOSE:
// Banzai demonstrates a production-quality trading client with:
// - Modern GUI interface for order entry and management
// - Real-time order and execution tracking
// - Support for multiple FIX protocol versions
// - Interactive trading operations (submit, cancel, replace)
// - Visual feedback for order status changes
//
// NAME ORIGIN:
// "Banzai" in trading culture refers to aggressive, bold trading actions.
// This client enables rapid order submission and management.
//
// KEY FEATURES:
// - Graphical order entry with dropdown menus
// - Real-time order blotter (grid showing all orders)
// - Execution blotter (grid showing all fills)
// - Cancel and replace functionality
// - Session management (logon/logout)
// - JMX monitoring and management
//
// ARCHITECTURE:
// - Main: This class (initialization and lifecycle)
// - Application: BanzaiApplication (FIX message handling)
// - UI: BanzaiFrame and panels (Swing components)
// - Models: OrderTableModel, ExecutionTableModel (data management)
//
// PRODUCTION USE CASES:
// - Trading desk applications for manual trading
// - Portfolio manager interfaces
// - Broker workstations
// - Testing and validation of FIX servers
// ============================================================================

package quickfix.examples.banzai;

import java.io.FileInputStream;
import java.io.InputStream;
import java.util.concurrent.CountDownLatch;

import javax.swing.JFrame;
import javax.swing.UIManager;

import org.quickfixj.jmx.JmxExporter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import quickfix.DefaultMessageFactory;
import quickfix.FileStoreFactory;
import quickfix.Initiator;
import quickfix.LogFactory;
import quickfix.MessageFactory;
import quickfix.MessageStoreFactory;
import quickfix.ScreenLogFactory;
import quickfix.Session;
import quickfix.SessionID;
import quickfix.SessionSettings;
import quickfix.SocketInitiator;
import quickfix.examples.banzai.ui.BanzaiFrame;

// ============================================================================
// BANZAI CLASS - Application Coordinator
// ============================================================================
// Manages the lifecycle of the trading client:
// 1. Configuration loading
// 2. Component initialization (FIX engine, UI, data models)
// 3. Connection management
// 4. Graceful shutdown
//
// DESIGN PATTERN:
// Uses the Facade pattern to simplify complex FIX initialization
// and provide clean interface for application control.
// ============================================================================
public class Banzai {
    // Shutdown coordination - ensures graceful exit
    // CountDownLatch allows main thread to wait until shutdown requested
    private static final CountDownLatch shutdownLatch = new CountDownLatch(1);

    // Logger for application-level events
    private static final Logger log = LoggerFactory.getLogger(Banzai.class);
    
    // Singleton instance for global access
    private static Banzai banzai;
    
    // FIX engine state tracking
    private boolean initiatorStarted = false;
    
    // FIX initiator (client connection manager)
    private Initiator initiator = null;
    
    // Main application window
    private JFrame frame = null;

    // ========================================================================
    // CONSTRUCTOR - Initialize Trading Client
    // ========================================================================
    // Sets up all components needed for trading:
    // 1. Load configuration file
    // 2. Create data models (orders, executions)
    // 3. Create FIX application (message handler)
    // 4. Initialize FIX engine components
    // 5. Create GUI
    //
    // CONFIGURATION FILE:
    // Standard QuickFIX settings file specifying:
    // - Connection details (host, port)
    // - Session parameters (SenderCompID, TargetCompID)
    // - FIX version
    // - Heartbeat intervals
    // - Store and log locations
    //
    // @param args Command-line arguments (optional config file path)
    // ========================================================================
    public Banzai(String[] args) throws Exception {
        // ====================================================================
        // CONFIGURATION LOADING
        // ====================================================================
        // Load from embedded resource or external file
        InputStream inputStream = null;
        if (args.length == 0) {
            // No arguments: use embedded default configuration
            inputStream = Banzai.class.getResourceAsStream("banzai.cfg");
        } else if (args.length == 1) {
            // One argument: use specified configuration file
            inputStream = new FileInputStream(args[0]);
        }
        
        if (inputStream == null) {
            System.out.println("usage: " + Banzai.class.getName() + " [configFile].");
            return;
        }
        
        // Parse configuration file into settings object
        SessionSettings settings = new SessionSettings(inputStream);
        inputStream.close();

        // ====================================================================
        // LOGGING CONFIGURATION
        // ====================================================================
        // Control whether heartbeat messages are logged
        // Heartbeats are FIX keep-alive messages sent regularly
        // In production, often disabled to reduce log volume
        boolean logHeartbeats = Boolean.valueOf(System.getProperty("logHeartbeats", "true"));

        // ====================================================================
        // DATA MODELS CREATION
        // ====================================================================
        // These models manage application state and provide MVC pattern
        
        // OrderTableModel: Manages all orders (submitted, filled, canceled)
        // Backed by Swing TableModel for automatic UI updates
        OrderTableModel orderTableModel = orderTableModel();
        
        // ExecutionTableModel: Manages all executions (fills)
        // Displays trade history with prices and quantities
        ExecutionTableModel executionTableModel = executionTableModel();
        
        // ====================================================================
        // FIX APPLICATION CREATION
        // ====================================================================
        // BanzaiApplication handles all FIX message processing:
        // - Receiving ExecutionReports
        // - Sending NewOrderSingle, Cancel, Replace messages
        // - Updating data models based on server responses
        BanzaiApplication application = application(orderTableModel, executionTableModel);
        
        // ====================================================================
        // FIX ENGINE COMPONENTS
        // ====================================================================
        // QuickFIX requires several factories for different services:
        
        // MessageStoreFactory: Persists FIX messages for recovery
        // Uses file-based storage for crash recovery and message replay
        MessageStoreFactory messageStoreFactory = new FileStoreFactory(settings);
        
        // LogFactory: Outputs FIX messages and events
        // ScreenLogFactory writes to console with formatting
        // Parameters: (incoming, outgoing, events, heartbeats)
        LogFactory logFactory = new ScreenLogFactory(true, true, true, logHeartbeats);
        
        // MessageFactory: Creates FIX message objects
        // DefaultMessageFactory supports all standard FIX versions
        MessageFactory messageFactory = new DefaultMessageFactory();

        // ====================================================================
        // FIX INITIATOR CREATION
        // ====================================================================
        // Initiator = FIX client that connects to servers
        // SocketInitiator uses TCP sockets for connections
        // Manages:
        // - Connection establishment
        // - Session logon/logout
        // - Heartbeat monitoring
        // - Sequence number management
        // - Message routing
        initiator = new SocketInitiator(application, messageStoreFactory, settings, logFactory,
                messageFactory);

        // ====================================================================
        // JMX MONITORING SETUP
        // ====================================================================
        // JMX (Java Management Extensions) allows runtime monitoring
        // Provides:
        // - View active sessions
        // - Check connection status
        // - Monitor message rates
        // - Trigger logon/logout
        // Access via JConsole or VisualVM
        JmxExporter exporter = new JmxExporter();
        exporter.register(initiator);

        // ====================================================================
        // GUI CREATION
        // ====================================================================
        // BanzaiFrame is the main window containing:
        // - Order entry panel (top)
        // - Order blotter (middle)
        // - Execution blotter (bottom)
        // - Menu bar (File, Actions, Help)
        frame = new BanzaiFrame(orderTableModel, executionTableModel, application);
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
    }

    // ========================================================================
    // FACTORY METHODS - Override for Customization
    // ========================================================================
    // These methods are protected to allow subclasses to provide
    // custom implementations for testing or specialized behavior.
    // Follows the Factory Method pattern.
    
    /**
     * Creates the order table model.
     * Override to provide custom order management logic.
     * @return OrderTableModel instance
     */
    protected OrderTableModel orderTableModel() {
        return new OrderTableModel();
    }
    
    /**
     * Creates the execution table model.
     * Override to provide custom execution tracking.
     * @return ExecutionTableModel instance
     */
    protected ExecutionTableModel executionTableModel() {
        return new ExecutionTableModel();
    }
    
    /**
     * Creates the FIX application.
     * Override to provide custom message handling logic.
     * @param orderTableModel Order management
     * @param executionTableModel Execution tracking
     * @return BanzaiApplication instance
     */
    protected BanzaiApplication application(OrderTableModel orderTableModel, ExecutionTableModel executionTableModel) {
        return new BanzaiApplication(orderTableModel, executionTableModel);
    }
    
    // ========================================================================
    // SESSION MANAGEMENT - Logon
    // ========================================================================
    // Connects to FIX server and establishes trading session.
    //
    // TWO SCENARIOS:
    // 1. First logon: Start initiator (creates connections)
    // 2. Subsequent logon: Reconnect existing sessions
    //
    // SYNCHRONIZED:
    // Thread-safe to prevent concurrent logon attempts
    // ========================================================================
    public synchronized void logon() {
        if (!initiatorStarted) {
            // First time: start the FIX engine
            try {
                initiator.start();
                initiatorStarted = true;
            } catch (Exception e) {
                log.error("Logon failed", e);
            }
        } else {
            // Already started: just logon all sessions
            // Useful for reconnecting after logout
            for (SessionID sessionId : initiator.getSessions()) {
                Session.lookupSession(sessionId).logon();
            }
        }
    }

    // ========================================================================
    // SESSION MANAGEMENT - Logout
    // ========================================================================
    // Gracefully disconnects from FIX server.
    // Sends Logout message to server before closing connection.
    // All open orders remain on server (not automatically canceled).
    //
    // PRODUCTION CONSIDERATION:
    // Before logout, consider:
    // - Canceling working orders
    // - Ensuring all fills are acknowledged
    // - Saving session state
    // ========================================================================
    public void logout() {
        for (SessionID sessionId : initiator.getSessions()) {
            Session.lookupSession(sessionId).logout("user requested");
        }
    }

    // ========================================================================
    // APPLICATION SHUTDOWN
    // ========================================================================
    // Triggers graceful application shutdown.
    // Releases the countdown latch, allowing main thread to exit.
    // ========================================================================
    public void stop() {
        shutdownLatch.countDown();
    }

    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Returns the main application window.
     * @return JFrame instance
     */
    public JFrame getFrame() {
        return frame;
    }

    /**
     * Returns the singleton Banzai instance.
     * Allows global access to application from UI components.
     * @return Banzai instance
     */
    public static Banzai get() {
        return banzai;
    }

    // ========================================================================
    // MAIN METHOD - Application Entry Point
    // ========================================================================
    // Launches the Banzai trading client.
    //
    // STARTUP SEQUENCE:
    // 1. Set native look and feel (platform appearance)
    // 2. Create Banzai instance (loads config, initializes components)
    // 3. Auto-logon (unless "openfix" property set for certification testing)
    // 4. Wait for shutdown signal
    //
    // EXECUTION MODEL:
    // Main thread blocks on shutdownLatch.await()
    // FIX message processing occurs in background threads
    // GUI updates occur on Event Dispatch Thread (EDT)
    //
    // @param args Optional configuration file path
    // ========================================================================
    public static void main(String[] args) throws Exception {
        try {
            // ================================================================
            // SET LOOK AND FEEL
            // ================================================================
            // Use native OS appearance (Windows, Mac, Linux)
            // Provides familiar UI for users
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        } catch (Exception e) {
            log.info(e.getMessage(), e);
        }
        
        // ====================================================================
        // CREATE AND INITIALIZE APPLICATION
        // ====================================================================
        banzai = new Banzai(args);
        
        // ====================================================================
        // AUTO-LOGON
        // ====================================================================
        // Automatically connect unless in OpenFIX certification mode
        // OpenFIX testing requires manual connection control
        if (!System.getProperties().containsKey("openfix")) {
            banzai.logon();
        }
        
        // ====================================================================
        // WAIT FOR SHUTDOWN
        // ====================================================================
        // Block main thread until shutdown requested
        // Application runs until user closes window or calls stop()
        shutdownLatch.await();
    }

}
