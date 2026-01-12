// ============================================================================
// EXECUTOR MAIN - FIX Order Execution Engine
// ============================================================================
// This is the main entry point for a FIX Protocol Executor (server-side).
//
// PURPOSE:
// Launches a FIX server that accepts connections from trading clients,
// receives orders, and immediately executes them. This is a "straight-through
// processing" example where all orders are instantly filled.
//
// ARCHITECTURE:
// 1. Load configuration from file
// 2. Create Application instance (order handling logic)
// 3. Create MessageStore (for persistence/recovery)
// 4. Create Logger (for debugging/auditing)
// 5. Create and start Acceptor (server socket)
// 6. Run forever until Ctrl-C
//
// CONFIGURATION FILE STRUCTURE:
// [DEFAULT]
// ConnectionType=acceptor
// SocketAcceptPort=5001
// FileStorePath=store
// FileLogPath=log
//
// [SESSION]
// BeginString=FIX.4.2
// SenderCompID=EXECUTOR
// TargetCompID=CLIENT
//
// PRODUCTION CONSIDERATIONS:
// - Add graceful shutdown handling (SIGTERM, SIGINT)
// - Implement order book persistence
// - Add monitoring and metrics
// - Support multiple concurrent sessions
// - Implement risk checks and position limits
// ============================================================================

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "quickfix/FileStore.h"
#include "quickfix/SocketAcceptor.h"
#ifdef HAVE_SSL
#include "quickfix/SSLSocketAcceptor.h"
#include "quickfix/ThreadedSSLSocketAcceptor.h"
#endif
#include "Application.h"
#include "quickfix/Log.h"
#include "quickfix/SessionSettings.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

// ============================================================================
// WAIT LOOP - Keeps server running
// ============================================================================
// Simple blocking loop that keeps the server alive until Ctrl-C is pressed.
// In production, you would:
// - Handle signals gracefully (SIGTERM, SIGINT, SIGHUP)
// - Flush pending messages before shutdown
// - Close positions and notify clients
// - Implement administrative commands (reload config, rotate logs, etc.)
// ============================================================================
void wait() {
  std::cout << "Type Ctrl-C to quit" << std::endl;
  while (true) {
    FIX::process_sleep(1);  // Sleep 1 second to avoid busy-waiting
  }
}

// ============================================================================
// MAIN - Server Initialization and Startup
// ============================================================================
int main(int argc, char **argv) {
  // ========================================================================
  // COMMAND LINE ARGUMENT PARSING
  // ========================================================================
  // Expects: ./executor <config_file> [SSL|SSL-ST]
  // - config_file: Path to QuickFIX configuration file
  // - SSL: Optional SSL mode (threaded)
  // - SSL-ST: Optional SSL mode (single-threaded)
  
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
    // SessionSettings parses the config file and extracts:
    // - Connection settings (host, port, etc.)
    // - Session identifiers (SenderCompID, TargetCompID)
    // - FIX version (BeginString)
    // - Timing parameters (heartbeat, timeout)
    // - Store and log paths
    
    FIX::SessionSettings settings(file);

    // ======================================================================
    // CREATE CORE COMPONENTS
    // ======================================================================
    
    // Application: Contains the business logic (order handling)
    Application application;
    
    // FileStoreFactory: Persists messages for recovery/replay
    // In case of disconnect, both sides can resync using stored messages
    // Stores sequence numbers, sent/received messages for replay
    FIX::FileStoreFactory storeFactory(settings);
    
    // ScreenLogFactory: Outputs FIX messages to console
    // In production, use FileLogFactory to log to disk
    // Options: ScreenLog, FileLog, or custom logger
    FIX::ScreenLogFactory logFactory(settings);

    // ======================================================================
    // CREATE ACCEPTOR (SERVER)
    // ======================================================================
    // Acceptor listens for incoming connections from FIX clients.
    // Supports three modes:
    // 1. SocketAcceptor: Standard TCP (most common)
    // 2. SSLSocketAcceptor: SSL/TLS single-threaded
    // 3. ThreadedSSLSocketAcceptor: SSL/TLS with thread pool
    //
    // WHY UNIQUE_PTR?
    // Allows runtime selection of acceptor type while maintaining
    // single interface for start/stop operations.
    
    std::unique_ptr<FIX::Acceptor> acceptor;
#ifdef HAVE_SSL
    // SSL support requires OpenSSL libraries at compile time
    if (isSSL.compare("SSL") == 0) {
      // ThreadedSSLSocketAcceptor: Each session gets own thread
      // Better for high-throughput multi-client scenarios
      acceptor = std::unique_ptr<FIX::Acceptor>(
          new FIX::ThreadedSSLSocketAcceptor(application, storeFactory, settings, logFactory));
    } else if (isSSL.compare("SSL-ST") == 0) {
      // SSLSocketAcceptor: Single-threaded event loop
      // Lower overhead for small number of clients
      acceptor
          = std::unique_ptr<FIX::Acceptor>(new FIX::SSLSocketAcceptor(application, storeFactory, settings, logFactory));
    } else
#endif
      // Standard TCP acceptor (no encryption)
      // Fastest option but data transmitted in clear text
      acceptor
          = std::unique_ptr<FIX::Acceptor>(new FIX::SocketAcceptor(application, storeFactory, settings, logFactory));

    // ======================================================================
    // START THE SERVER
    // ======================================================================
    // acceptor->start() performs:
    // 1. Bind to configured port
    // 2. Start listening for connections
    // 3. For each connection, perform FIX logon handshake
    // 4. Begin processing messages
    
    acceptor->start();
    
    // Wait forever (or until Ctrl-C)
    wait();
    
    // ======================================================================
    // GRACEFUL SHUTDOWN
    // ======================================================================
    // acceptor->stop() performs:
    // 1. Stop accepting new connections
    // 2. Send Logout messages to connected clients
    // 3. Wait for Logout responses
    // 4. Close all sockets
    // 5. Flush message store
    
    acceptor->stop();

    return 0;
  } catch (std::exception &e) {
    // Catch configuration errors, network errors, etc.
    // In production, log to monitoring system and alert operations
    std::cout << e.what() << std::endl;
    return 1;
  }
}
