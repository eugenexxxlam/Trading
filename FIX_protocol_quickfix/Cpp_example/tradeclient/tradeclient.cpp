// ============================================================================
// TRADE CLIENT MAIN - FIX Trading Client Application
// ============================================================================
// This is the main entry point for a FIX Protocol Trading Client (initiator).
//
// PURPOSE:
// Launches a FIX client that connects to a trading executor/broker, allowing
// users to interactively submit orders, cancel orders, replace orders, and
// request market data through a command-line interface.
//
// ARCHITECTURE OVERVIEW:
// 1. Load configuration from file
// 2. Create Application instance (user interface and order logic)
// 3. Create MessageStore (for persistence/recovery)
// 4. Create Logger (for debugging/auditing)
// 5. Create and start Initiator (client connection)
// 6. Run interactive menu loop
// 7. Graceful shutdown
//
// KEY DIFFERENCE FROM EXECUTOR:
// - Uses INITIATOR (connects to server) vs ACCEPTOR (waits for connections)
// - Interactive user interface vs automated processing
// - Sends orders vs receives and fills orders
//
// CONFIGURATION FILE STRUCTURE:
// [DEFAULT]
// ConnectionType=initiator
// SocketConnectHost=localhost
// SocketConnectPort=5001
// FileStorePath=store
// FileLogPath=log
// StartTime=00:00:00
// EndTime=23:59:59
//
// [SESSION]
// BeginString=FIX.4.2
// SenderCompID=CLIENT
// TargetCompID=EXECUTOR
//
// USE CASES:
// - Trading desk application for manual trading
// - Algorithmic trading client for automated strategies
// - Testing and validation of FIX executors
// - Order management system (OMS) interface
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "quickfix/FileStore.h"
#include "quickfix/SocketInitiator.h"
#ifdef HAVE_SSL
#include "quickfix/SSLSocketInitiator.h"
#include "quickfix/ThreadedSSLSocketInitiator.h"
#endif
#include "Application.h"
#include "quickfix/Log.h"
#include "quickfix/SessionSettings.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "../../src/getopt-repl.h"

// ============================================================================
// MAIN - Client Initialization and Startup
// ============================================================================
int main(int argc, char **argv) {
  // ========================================================================
  // COMMAND LINE ARGUMENT PARSING
  // ========================================================================
  // Expects: ./tradeclient <config_file> [SSL|SSL-ST]
  // - config_file: Path to QuickFIX configuration file
  // - SSL: Optional SSL mode with threading
  // - SSL-ST: Optional SSL mode single-threaded
  
  if (argc < 2) {
    std::cout << "usage: " << argv[0] << " FILE." << std::endl;
    return 0;
  }
  std::string file = argv[1];

#ifdef HAVE_SSL
  std::string isSSL;
  if (argc > 2) {
    isSSL.assign(argv[2]);
  }
#endif

  try {
    // ======================================================================
    // LOAD CONFIGURATION
    // ======================================================================
    // SessionSettings parses the configuration file to extract:
    // - Connection details (host, port to connect to)
    // - Session identifiers (SenderCompID, TargetCompID)
    // - FIX protocol version (BeginString)
    // - Timing parameters (connection times, heartbeat interval)
    // - Storage and logging paths
    // - Reconnection behavior (ReconnectInterval, etc.)
    
    FIX::SessionSettings settings(file);

    // ======================================================================
    // CREATE CORE COMPONENTS
    // ======================================================================
    
    // Application: Contains business logic and user interface
    // This is where orders are created and responses are handled
    Application application;
    
    // FileStoreFactory: Persists messages for crash recovery
    // Stores:
    // - Sequence numbers (to detect gaps after reconnect)
    // - Sent messages (for replay if requested by server)
    // - Received messages (for gap detection)
    // This enables recovery after disconnect without losing order state
    FIX::FileStoreFactory storeFactory(settings);
    
    // ScreenLogFactory: Displays FIX messages on console
    // Useful for development and debugging
    // In production, use FileLogFactory to write to disk
    // Can also implement custom logger for integration with monitoring
    FIX::ScreenLogFactory logFactory(settings);

    // ======================================================================
    // CREATE INITIATOR (CLIENT)
    // ======================================================================
    // Initiator establishes connections to FIX servers (acceptors).
    // Three modes supported:
    // 1. SocketInitiator: Standard TCP (most common, best performance)
    // 2. SSLSocketInitiator: SSL/TLS single-threaded
    // 3. ThreadedSSLSocketInitiator: SSL/TLS with thread pool
    //
    // INITIATOR BEHAVIOR:
    // - Connects to host:port specified in config
    // - Performs FIX logon handshake
    // - Maintains heartbeats to keep connection alive
    // - Automatically reconnects if connection drops
    // - Handles sequence number management and gap fills
    
    std::unique_ptr<FIX::Initiator> initiator;
#ifdef HAVE_SSL
    if (isSSL.compare("SSL") == 0) {
      // ThreadedSSLSocketInitiator: Each session in own thread
      // Better for clients managing multiple simultaneous connections
      initiator = std::unique_ptr<FIX::Initiator>(
          new FIX::ThreadedSSLSocketInitiator(application, storeFactory, settings, logFactory));
    } else if (isSSL.compare("SSL-ST") == 0) {
      // SSLSocketInitiator: Single-threaded event loop
      // More efficient for single connection scenarios
      initiator = std::unique_ptr<FIX::Initiator>(
          new FIX::SSLSocketInitiator(application, storeFactory, settings, logFactory));
    } else
#endif
      // Standard TCP initiator (no encryption)
      // Best performance, but data sent in clear text
      // Only use on trusted networks or with VPN
      initiator
          = std::unique_ptr<FIX::Initiator>(new FIX::SocketInitiator(application, storeFactory, settings, logFactory));

    // ======================================================================
    // START THE CLIENT
    // ======================================================================
    // initiator->start() performs:
    // 1. Connect to server specified in config
    // 2. Send Logon message with credentials
    // 3. Wait for Logon response
    // 4. Begin heartbeat monitoring
    // 5. Process incoming messages in background
    //
    // This is non-blocking - returns immediately after starting background threads
    
    initiator->start();
    
    // ======================================================================
    // RUN INTERACTIVE LOOP
    // ======================================================================
    // application.run() displays menu and processes user commands
    // This blocks until user selects "Quit" from menu
    // While this runs, FIX messages are being processed in background
    
    application.run();
    
    // ======================================================================
    // GRACEFUL SHUTDOWN
    // ======================================================================
    // initiator->stop() performs:
    // 1. Stop accepting user input
    // 2. Send Logout message to server
    // 3. Wait for Logout response
    // 4. Close connection
    // 5. Flush message store (save sequence numbers)
    //
    // IMPORTANT: Any pending orders should be handled before shutdown
    // In production systems:
    // - Cancel all working orders (or leave them active)
    // - Wait for outstanding ExecutionReports
    // - Log final state for audit trail
    
    initiator->stop();

    return 0;
  } catch (std::exception &e) {
    // Catch configuration errors, connection errors, protocol errors, etc.
    // Common errors:
    // - Config file not found or malformed
    // - Cannot connect to server (wrong host/port, server down)
    // - Authentication failure (wrong credentials)
    // - Protocol version mismatch
    // - Network issues
    //
    // In production:
    // - Log to monitoring system
    // - Alert operations team
    // - Implement retry logic with exponential backoff
    // - Provide meaningful error messages to user
    
    std::cout << e.what();
    return 1;
  }
}
