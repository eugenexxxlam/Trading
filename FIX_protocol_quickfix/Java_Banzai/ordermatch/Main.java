// ============================================================================
// ORDER MATCH SERVER - Main Entry Point
// ============================================================================
// This is the main entry point for the Order Matching Server application.
// It launches a FIX acceptor that maintains order books and matches orders.
//
// PURPOSE:
// - Start FIX server (acceptor) for receiving orders
// - Maintain real-time order books for multiple symbols
// - Match buy and sell orders continuously
// - Send execution reports for filled orders
// - Provide interactive console for viewing order books
//
// WHAT IS ORDER MATCHING?
// An order matching engine maintains order books (bids/asks) and executes
// trades when buy and sell orders cross (bid >= ask). It's the core of
// exchanges like NASDAQ, NYSE, crypto exchanges, etc.
//
// KEY FEATURES:
// - Multi-symbol support (AAPL, MSFT, GOOGL, etc.)
// - Price-time priority matching
// - MARKET and LIMIT order support
// - Interactive console commands (#symbols, #quit)
// - Real-time order book display
//
// COMPARISON TO OTHER EXAMPLES:
// - **Executor**: Fills orders immediately (simulated fills)
// - **OrderMatch**: Maintains order books, matches against resting orders
// - **Banzai**: GUI trading client (sends orders to Executor/OrderMatch)
//
// CONFIGURATION:
// Uses standard QuickFIX settings file (ordermatch.cfg):
// [DEFAULT]
// ConnectionType=acceptor
// SocketAcceptPort=5002
// FileStorePath=data/ordermatch
// 
// [SESSION]
// BeginString=FIX.4.2
// SenderCompID=ORDERMATCH
// TargetCompID=*  # Accept any client
//
// CONSOLE COMMANDS:
// - #symbols : Display all order books
// - #quit    : Shutdown server
// - <Enter>  : Display all order books (default)
//
// PRODUCTION CONSIDERATIONS:
// Real matching engines would add:
// - High-performance data structures (lock-free queues)
// - Market data distribution (publish order book updates)
// - Order types (stop, iceberg, fill-or-kill)
// - Auction matching (opening/closing crosses)
// - Circuit breakers and trading halts
// - Risk management and position limits
// - Regulatory reporting (audit trail)
// ============================================================================

package quickfix.examples.ordermatch;

import quickfix.DefaultMessageFactory;
import quickfix.FileStoreFactory;
import quickfix.LogFactory;
import quickfix.ScreenLogFactory;
import quickfix.SessionSettings;
import quickfix.SocketAcceptor;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

public class Main {
    // ========================================================================
    // MAIN METHOD - Application Entry Point
    // ========================================================================
    /**
     * Starts Order Matching Server and interactive console.
     * 
     * EXECUTION FLOW:
     * 1. Load configuration file
     * 2. Create Application (order handling logic)
     * 3. Create MessageStoreFactory (persistence)
     * 4. Create LogFactory (logging)
     * 5. Create SocketAcceptor (server)
     * 6. Start acceptor (begin accepting connections)
     * 7. Enter interactive command loop
     * 8. On #quit: stop acceptor and exit
     * 
     * CONFIGURATION FILE:
     * - No args: Load embedded ordermatch.cfg resource
     * - One arg: Load specified configuration file
     * 
     * INTERACTIVE LOOP:
     * Server runs in background threads while main thread
     * waits for console commands to display order books or quit.
     * 
     * @param args Optional: path to configuration file
     */
    public static void main(String[] args) {
        InputStream inputStream = null;
        try {
            // ================================================================
            // LOAD CONFIGURATION
            // ================================================================
            if (args.length == 0) {
                // No arguments - use embedded default configuration
                inputStream = OrderMatcher.class.getResourceAsStream("ordermatch.cfg");
            } else if (args.length == 1) {
                // One argument - load specified configuration file
                inputStream = new FileInputStream(args[0]);
            }
            
            if (inputStream == null) {
                System.out.println("usage: " + OrderMatcher.class.getName() + " [configFile].");
                return;
            }
            
            SessionSettings settings = new SessionSettings(inputStream);

            // ================================================================
            // CREATE QUICKFIX COMPONENTS
            // ================================================================
            
            // Application: Implements FIX message handling and order matching logic
            Application application = new Application();
            
            // FileStoreFactory: Persists FIX messages to disk for recovery
            FileStoreFactory storeFactory = new FileStoreFactory(settings);
            
            // ScreenLogFactory: Outputs messages to console
            LogFactory logFactory = new ScreenLogFactory(settings);
            
            // SocketAcceptor: Listens for TCP connections from FIX clients
            // Manages multiple sessions simultaneously
            SocketAcceptor acceptor = new SocketAcceptor(
                    application,                    // Message handler
                    storeFactory,                   // Message persistence
                    settings,                       // Configuration
                    logFactory,                     // Logging
                    new DefaultMessageFactory());   // Message parsing

            // ================================================================
            // START SERVER
            // ================================================================
            BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
            acceptor.start();  // Start listening for connections
            
            // ================================================================
            // INTERACTIVE COMMAND LOOP
            // ================================================================
            // Main thread enters command loop while server runs in background
            // Commands allow viewing order books and graceful shutdown
            label:  // Label for breaking out of nested loop
            while (true) {
                System.out.println("type #quit to quit");
                String value = in.readLine();
                
                if (value != null) {
                    switch (value) {
                        case "#symbols":
                            // Display all order books (all symbols)
                            application.orderMatcher().display();
                            break;
                            
                        case "#quit":
                            // Exit command loop, shutdown server
                            break label;
                            
                        default:
                            // Any other input: display order books
                            application.orderMatcher().display();
                            break;
                    }
                }
            }
            
            // ================================================================
            // GRACEFUL SHUTDOWN
            // ================================================================
            // Stop acceptor: send Logout to all clients, close connections
            acceptor.stop();
            System.exit(0);
            
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            // ================================================================
            // CLEANUP
            // ================================================================
            try {
                if (inputStream != null) {
                    inputStream.close();
                }
            } catch (IOException ex) {
                // Ignore exceptions during cleanup
            }
        }
    }
}
