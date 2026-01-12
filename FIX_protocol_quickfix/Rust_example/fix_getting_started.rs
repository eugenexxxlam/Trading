// =============================================================================
// QuickFIX Rust Example: Getting Started with File-Based Configuration
// =============================================================================
// This example demonstrates the most common way to start a FIX acceptor:
// loading configuration from an external .ini file. This approach is preferred
// in production as it allows changing configuration without recompiling.
//
// Key Learning Points:
// 1. Loading SessionSettings from configuration files
// 2. Using FileMessageStoreFactory for persistent message storage
// 3. Command-line argument handling
// 4. Simple acceptor lifecycle management
// =============================================================================

use std::{
    env,      // For reading command-line arguments
    io::{stdin, Read}, // For user input handling
    process::exit,     // For exiting with error codes
};

// Import all QuickFIX types - wildcard import for convenience
// In production code, you might prefer explicit imports
use quickfix::*;

// =============================================================================
// Application Implementation
// =============================================================================
// Minimal application that handles FIX session callbacks
// In a real trading system, this would contain your business logic
// =============================================================================

#[derive(Default)]
pub struct MyApplication;

impl ApplicationCallback for MyApplication {
    // ==========================================================================
    // on_create: Session Initialization Hook
    // ==========================================================================
    // Called when a FIX session is first created (before any logon attempt)
    // 
    // Use cases:
    // - Initialize data structures for this trading counterparty
    // - Load reference data (instruments, trading limits, etc.)
    // - Set up monitoring/alerting for this session
    // - Initialize order books or position trackers
    // ==========================================================================
    fn on_create(&self, _session: &SessionId) {
        // Example of what you might do in production:
        // 
        // println!("Session created: {}", session);
        // 
        // - Load counterparty-specific configuration
        // - Initialize risk limits for this counterparty
        // - Set up metrics collection
        // - Pre-allocate memory pools for expected message volume
        // - Connect to internal order management system
    }

    // Additional callback methods you might implement in a real system:
    //
    // fn on_logon(&self, session: &SessionId) {
    //     // Called after successful logon
    //     // - Send initial market data subscriptions
    //     // - Request position reconciliation
    //     // - Enable trading for this counterparty
    // }
    //
    // fn on_logout(&self, session: &SessionId) {
    //     // Called when session logs out
    //     // - Cancel pending orders
    //     // - Stop sending market data
    //     // - Trigger alerts
    // }
    //
    // fn on_msg_from_app(&self, msg: &Message, session: &SessionId) 
    //     -> Result<(), MsgFromAppError> {
    //     // Process incoming application messages (orders, quotes, etc.)
    //     // This is where your core business logic lives
    //     Ok(())
    // }
}

// =============================================================================
// Main Entry Point
// =============================================================================
// Demonstrates loading configuration from a file and running an acceptor
// =============================================================================

fn main() -> Result<(), QuickFixError> {
    // =========================================================================
    // Step 1: Parse Command-Line Arguments
    // =========================================================================
    // This acceptor requires a configuration file path as an argument
    // =========================================================================
    
    let args: Vec<_> = env::args().collect();
    
    // Check if config file was provided
    // Pattern matching with Some/None for safe unwrapping
    let Some(config_file) = args.get(1) else {
        // If no config file provided, print usage and exit with error code
        eprintln!("Bad program usage: {} <config_file>", args[0]);
        exit(1);
    };

    // =========================================================================
    // Step 2: Create FIX Engine Components
    // =========================================================================
    // Build all the components needed to run a FIX acceptor
    // =========================================================================
    
    println!(">> Creating resources");
    
    // Load session settings from the configuration file
    // The file should be in INI format with [DEFAULT] and [SESSION] sections
    // Example config file format:
    //   [DEFAULT]
    //   ConnectionType=acceptor
    //   FileStorePath=/var/log/fix
    //   
    //   [SESSION]
    //   BeginString=FIX.4.4
    //   SenderCompID=SERVER
    //   TargetCompID=CLIENT
    //   SocketAcceptPort=5000
    let settings = SessionSettings::try_from_path(config_file)?;
    
    // Create a file-based message store
    // This persists:
    // - All sent and received messages (for regulatory compliance)
    // - Sequence numbers (for recovery after restart)
    // - Session state
    // 
    // Critical for production: ensures exactly-once message delivery
    // even after crashes or restarts
    let store_factory = FileMessageStoreFactory::try_new(&settings)?;
    
    // Create a logger that outputs to stdout
    // In production, you would typically log to files with rotation
    // Alternatives:
    // - StdLogger::Stderr for error output
    // - Custom logger implementation for structured logging
    let log_factory = LogFactory::try_new(&StdLogger::Stdout)?;
    
    // Instantiate our application callbacks
    let callbacks = MyApplication;
    
    // Wrap our callbacks in a QuickFIX Application object
    // This bridges our Rust code with the underlying C++ QuickFIX engine
    let app = Application::try_new(&callbacks)?;

    // =========================================================================
    // Step 3: Create and Configure the Acceptor
    // =========================================================================
    // The acceptor is the core component that manages FIX sessions
    // =========================================================================
    
    let mut acceptor = Acceptor::try_new(
        &settings,      // All session configuration loaded from file
        &app,           // Our application callback handlers
        &store_factory, // Persistent message storage
        &log_factory,   // Logging destination
        FixSocketServerKind::SingleThreaded, // Threading model
        // SingleThreaded: Simpler, easier to debug
        // MultiThreaded: Better performance, handles multiple sessions concurrently
    )?;

    // =========================================================================
    // Step 4: Start the Acceptor
    // =========================================================================
    // Begin listening for incoming connections and processing messages
    // =========================================================================
    
    println!(">> connection handler START");
    acceptor.start()?;
    // At this point:
    // - TCP socket is listening on the configured port
    // - Ready to accept incoming FIX connections
    // - Will validate logon messages against configured sessions
    // - Heartbeat monitoring is active

    // =========================================================================
    // Step 5: Run Until User Quits
    // =========================================================================
    // Keep the acceptor running in a simple input loop
    // In production, you might use signals or a more sophisticated event loop
    // =========================================================================
    
    println!(">> App running, press 'q' to quit");
    
    let mut stdin = stdin().lock();
    let mut stdin_buf = [0];
    
    loop {
        // Blocking read - wait for user input
        let _ = stdin.read_exact(&mut stdin_buf);
        
        // Check if user wants to quit
        if stdin_buf[0] == b'q' {
            break;
        }
        // Any other key is ignored - acceptor keeps running
    }

    // =========================================================================
    // Step 6: Graceful Shutdown
    // =========================================================================
    // Properly close all sessions before exiting
    // =========================================================================
    
    println!(">> connection handler STOP");
    acceptor.stop()?;
    // This will:
    // - Send logout messages to all connected counterparties
    // - Flush all pending messages to disk
    // - Close TCP connections
    // - Save sequence numbers for recovery

    println!(">> All cleared. Bye !");
    Ok(())
}

// =============================================================================
// Example Configuration File
// =============================================================================
// Save this as 'acceptor.cfg' and run:
//   cargo run --example fix_getting_started -- acceptor.cfg
//
// [DEFAULT]
// ConnectionType=acceptor
// ReconnectInterval=60
// FileStorePath=./fixdata
// FileLogPath=./fixlog
// StartTime=00:00:00
// EndTime=00:00:00
// UseDataDictionary=Y
// ValidateFieldsOutOfOrder=N
// 
// [SESSION]
// BeginString=FIX.4.4
// SenderCompID=ACCEPTOR
// TargetCompID=INITIATOR
// HeartBtInt=30
// SocketAcceptPort=5001
// DataDictionary=spec/FIX44.xml
// 
// =============================================================================
// Configuration Parameters Explained
// =============================================================================
// 
// [DEFAULT] Section - Applies to all sessions:
// 
// ConnectionType: acceptor (server) or initiator (client)
// ReconnectInterval: Seconds to wait between reconnection attempts
// FileStorePath: Directory for message store files
// FileLogPath: Directory for log files
// StartTime/EndTime: Session hours (00:00:00 = 24/7 operation)
// UseDataDictionary: Enable message validation
// ValidateFieldsOutOfOrder: Strict field ordering validation
// 
// [SESSION] Section - Specific to each trading counterparty:
// 
// BeginString: FIX protocol version (FIX.4.0, FIX.4.2, FIX.4.4, FIXT.1.1)
// SenderCompID: Our identifier (must be unique)
// TargetCompID: Counterparty identifier
// HeartBtInt: Heartbeat interval in seconds
// SocketAcceptPort: TCP port to listen on
// DataDictionary: Path to FIX dictionary XML file
// 
// You can have multiple [SESSION] sections for different counterparties
// =============================================================================
