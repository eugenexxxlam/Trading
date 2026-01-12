// ============================================================================
// ORDERMATCH MAIN - FIX Order Matching Engine
// ============================================================================
// This is the main entry point for a FIX Protocol Order Matching Engine -
// a realistic simulation of a stock exchange matching orders.
//
// PURPOSE:
// Launches a FIX server that accepts orders from multiple trading clients,
// maintains separate order books for each trading instrument, and matches
// buy orders with sell orders using price-time priority algorithm.
//
// KEY DIFFERENCES FROM OTHER EXAMPLES:
// 1. EXECUTOR: Immediately fills all orders (straight-through)
// 2. ORDERMATCH: Maintains order books and matches contra-side orders
// 3. TRADECLIENT: Client that sends orders to executors/matchers
//
// ARCHITECTURE:
// 1. Load configuration from file
// 2. Create Application (contains OrderMatcher)
// 3. Create MessageStore and Logger
// 4. Create and start Acceptor (server)
// 5. Run interactive command loop for viewing order books
// 6. Support graceful shutdown
//
// INTERACTIVE COMMANDS:
// - #symbols : List all trading symbols with active orders
// - <symbol> : Display order book for specific symbol
// - #quit    : Shutdown the matching engine
//
// CONFIGURATION FILE:
// Same as executor - see executor.cpp for details
//
// TYPICAL USAGE:
// Terminal 1 (Start matching engine):
//   $ ./ordermatch config.cfg
//   > #symbols          (see what's trading)
//   > AAPL              (view AAPL order book)
//
// Terminal 2 (Start trading client):
//   $ ./tradeclient client1.cfg
//   > 1 (Enter order: BUY 100 AAPL @ $150)
//
// Terminal 3 (Start another trading client):
//   $ ./tradeclient client2.cfg
//   > 1 (Enter order: SELL 100 AAPL @ $150)
//   [Orders match in Terminal 1]
//
// PRODUCTION ENHANCEMENTS:
// Real matching engines need:
// - High-performance order book (lock-free, cache-friendly)
// - Market data dissemination (multicast UDP)
// - Risk management (position limits, credit checks)
// - Circuit breakers (halt on excessive volatility)
// - Multiple matching algorithms (pro-rata, FIFO)
// - Auction mechanisms (opening, closing, intraday)
// - Post-trade processing (clearing, settlement)
// - Regulatory reporting (audit trail, market surveillance)
// - Hot-standby failover for high availability
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "Application.h"
#include "quickfix/FileStore.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/SocketAcceptor.h"

#include <fstream>
#include <iostream>
#include <string>

// ============================================================================
// MAIN - Matching Engine Initialization
// ============================================================================
int main(int argc, char **argv) {
  // ========================================================================
  // COMMAND LINE VALIDATION
  // ========================================================================
  // Expects: ./ordermatch <config_file>
  // - config_file: Path to QuickFIX configuration file
  
  if (argc != 2) {
    std::cout << "usage: " << argv[0] << " FILE." << std::endl;
    return 0;
  }
  std::string file = argv[1];

  try {
    // ======================================================================
    // LOAD CONFIGURATION
    // ======================================================================
    // Parse configuration file to extract:
    // - Network settings (port to listen on)
    // - Session settings (which clients can connect)
    // - FIX versions supported
    // - Storage and logging paths
    // - Timing parameters (heartbeat, timeouts)
    
    FIX::SessionSettings settings(file);

    // ======================================================================
    // CREATE CORE COMPONENTS
    // ======================================================================
    
    // Application: Contains the order matching engine
    // This is where all the magic happens:
    // - Receives orders from clients
    // - Maintains order books per symbol
    // - Matches buy/sell orders
    // - Sends execution reports
    Application application;
    
    // FileStoreFactory: Persists FIX messages
    // Critical for:
    // - Recovering order state after crashes
    // - Replaying messages after disconnect
    // - Gap fill requests
    // - Audit trail
    FIX::FileStoreFactory storeFactory(settings);
    
    // ScreenLogFactory: Logs FIX messages to console
    // For production, use FileLogFactory or database logging
    // Consider structured logging (JSON) for better analysis
    FIX::ScreenLogFactory logFactory(settings);
    
    // SocketAcceptor: FIX server that accepts client connections
    // - Listens on configured port
    // - Handles logon/logout
    // - Routes messages to application
    // - Manages heartbeats
    // - Enforces sequence number integrity
    FIX::SocketAcceptor acceptor(application, storeFactory, settings, logFactory);

    // ======================================================================
    // START THE MATCHING ENGINE
    // ======================================================================
    // acceptor.start() performs:
    // 1. Bind to configured port
    // 2. Start listening for connections
    // 3. For each connection:
    //    a. Perform FIX logon handshake
    //    b. Validate credentials
    //    c. Begin processing messages
    // 4. Launch background threads for heartbeat monitoring
    
    acceptor.start();
    
    // ======================================================================
    // INTERACTIVE COMMAND LOOP
    // ======================================================================
    // While matching engine runs in background, provide interactive
    // commands for monitoring and debugging order book state.
    //
    // COMMANDS:
    // #symbols - Show all symbols with active orders
    // <symbol> - Show order book for specific symbol (e.g., "AAPL")
    // #quit    - Graceful shutdown
    //
    // WHY INTERACTIVE?
    // - Monitor order book state in real-time
    // - Debug matching behavior
    // - Verify orders are in correct price levels
    // - See spread (best bid vs best ask)
    //
    // PRODUCTION ALTERNATIVES:
    // Instead of stdin commands, real systems use:
    // - Web dashboard (real-time order book visualization)
    // - RESTful API (query order book programmatically)
    // - Admin console (trading hours, halt/resume, etc.)
    // - Market data feed (publish snapshots and updates)
    
    while (true) {
      std::string value;
      std::cin >> value;

      if (value == "#symbols") {
        // Display all trading symbols with active orders
        // Example output:
        // SYMBOLS:
        // --------
        // AAPL
        // GOOGL
        // MSFT
        application.orderMatcher().display();
        
      } else if (value == "#quit") {
        // Graceful shutdown requested
        break;
        
      } else {
        // Display order book for specific symbol
        // Example: User types "AAPL"
        // Output:
        // BIDS:
        // -----
        // ID: ..., OWNER: CLIENT1, PRICE: $150.00, QUANTITY: 100
        // ID: ..., OWNER: CLIENT2, PRICE: $149.50, QUANTITY: 200
        //
        // ASKS:
        // -----
        // ID: ..., OWNER: CLIENT3, PRICE: $150.25, QUANTITY: 150
        // ID: ..., OWNER: CLIENT4, PRICE: $150.50, QUANTITY: 100
        application.orderMatcher().display(value);
      }

      std::cout << std::endl;
    }
    
    // ======================================================================
    // GRACEFUL SHUTDOWN
    // ======================================================================
    // acceptor.stop() performs:
    // 1. Stop accepting new connections
    // 2. Send Logout messages to all connected clients
    // 3. Wait for Logout responses
    // 4. Close all connections
    // 5. Flush message store (save sequence numbers and order state)
    //
    // PRODUCTION SHUTDOWN CONSIDERATIONS:
    // Before shutdown, should:
    // - Cancel all resting orders (or leave them for next session)
    // - Notify clients of impending shutdown
    // - Complete any pending settlements
    // - Generate end-of-day reports
    // - Archive logs and audit trail
    // - Save order book snapshot for next day's opening
    
    acceptor.stop();
    return 0;
    
  } catch (std::exception &e) {
    // Catch configuration errors, network errors, etc.
    // Common errors:
    // - Config file not found or malformed
    // - Port already in use
    // - Insufficient permissions to bind port
    // - Storage directory doesn't exist or not writable
    //
    // Production error handling:
    // - Log to monitoring system
    // - Send alerts to operations team
    // - Provide detailed error messages
    // - Attempt automatic recovery if possible
    // - Create incident ticket for investigation
    
    std::cout << e.what() << std::endl;
    return 1;
  }
}
