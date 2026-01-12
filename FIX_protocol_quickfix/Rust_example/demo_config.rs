// =============================================================================
// QuickFIX Rust Example: Programmatic Configuration Demo
// =============================================================================
// This example demonstrates how to create a FIX acceptor (server) with
// configuration built programmatically in code, rather than loading from
// a configuration file. This is useful for dynamic configuration scenarios.
//
// Key Learning Points:
// 1. Building SessionSettings without external config files
// 2. Creating a FIX acceptor that listens for incoming connections
// 3. Implementing minimal ApplicationCallback trait
// 4. Managing server lifecycle (start/stop)
// =============================================================================

use std::io::{stdin, Read};

use quickfix::{
    dictionary_item::*, // Pre-defined configuration items (types)
    Acceptor,           // FIX server that accepts incoming connections
    Application,        // Wrapper for our application callbacks
    ApplicationCallback,// Trait for implementing FIX lifecycle hooks
    ConnectionHandler,  // Trait for connection management
    Dictionary,         // Key-value configuration container
    FixSocketServerKind,// Type of socket server (single/multi-threaded)
    LogFactory,         // Factory for creating loggers
    MemoryMessageStoreFactory, // In-memory message persistence
    QuickFixError,      // Error type for QuickFIX operations
    SessionId,          // Unique identifier for a FIX session
    SessionSettings,    // Container for all session configurations
    StdLogger,          // Standard output logger
};

// =============================================================================
// Application Implementation
// =============================================================================
// This struct will handle all FIX session callbacks. In a production system,
// you would add fields to maintain state, order books, positions, etc.
// =============================================================================

#[derive(Default)]
pub struct MyApplication;

// Implement the ApplicationCallback trait to receive notifications about
// FIX session lifecycle events and message flow
impl ApplicationCallback for MyApplication {
    // Called when a new FIX session is created
    // This happens before logon, useful for initializing session-specific state
    fn on_create(&self, _session: &SessionId) {
        // In a real application, you might:
        // - Initialize order books for this session
        // - Set up monitoring/metrics
        // - Load session-specific configuration
        // - Allocate resources needed for this trading counterparty
    }
}

// =============================================================================
// Configuration Builder
// =============================================================================
// This function demonstrates building FIX session configuration programmatically
// instead of loading from an .ini file. This provides flexibility for
// dynamic configuration scenarios.
// =============================================================================

fn build_settings() -> Result<SessionSettings, QuickFixError> {
    // Create an empty settings object
    let mut settings = SessionSettings::new();

    // ---------------------------------------------------------------------
    // Global (Default) Settings
    // ---------------------------------------------------------------------
    // These settings apply to all sessions unless overridden
    // The 'None' parameter means these are default settings
    // ---------------------------------------------------------------------
    settings.set(
        None,  // None = default settings for all sessions
        Dictionary::try_from_items(&[
            // ConnectionType: Acceptor = server mode (listens for connections)
            // Alternative would be Initiator = client mode (initiates connections)
            &ConnectionType::Acceptor,
            
            // ReconnectInterval: Time in seconds to wait before reconnecting
            // Only relevant for initiator mode, but good to set globally
            &ReconnectInterval(60),
            
            // FileStorePath: Directory where message logs and sequence numbers persist
            // Critical for recovery after crashes/restarts
            &FileStorePath("store"),
        ])?,
    )?;

    // ---------------------------------------------------------------------
    // Session-Specific Settings
    // ---------------------------------------------------------------------
    // These settings are specific to one FIX session
    // A session is uniquely identified by: BeginString, SenderCompID, TargetCompID
    // ---------------------------------------------------------------------
    settings.set(
        // Define the session ID: FIX version, our ID, counterparty ID, qualifier
        Some(&SessionId::try_new("FIX.4.4", "ME", "THEIR", "")?),
        Dictionary::try_from_items(&[
            // StartTime: Session start time (HH:MM:SS in UTC)
            // Messages won't be processed outside of session hours
            &StartTime("12:30:00"),
            
            // EndTime: Session end time (HH:MM:SS in UTC)
            // Session will disconnect at this time
            &EndTime("23:30:00"),
            
            // HeartBtInt: Heartbeat interval in seconds
            // Both sides must send heartbeat messages at this interval
            // to prove the connection is still alive
            &HeartBtInt(20),
            
            // SocketAcceptPort: TCP port number to listen on
            // Counterparties will connect to this port
            &SocketAcceptPort(4000),
            
            // DataDictionary: Path to FIX data dictionary XML file
            // Defines valid message types, fields, and validation rules
            // Each FIX version has its own dictionary
            &DataDictionary("quickfix-ffi/libquickfix/spec/FIX41.xml"),
        ])?,
    )?;

    Ok(settings)
}

// =============================================================================
// Main Entry Point
// =============================================================================
// Sets up and runs the FIX acceptor server
// =============================================================================

fn main() -> Result<(), QuickFixError> {
    // Step 1: Build configuration
    // ---------------------------
    println!(">> Configuring application");
    let settings = build_settings()?;

    // Step 2: Create required components
    // -----------------------------------
    println!(">> Creating resources");
    
    // Message Store: Where messages are persisted
    // MemoryMessageStoreFactory = messages stored in RAM (lost on restart)
    // Alternative: FileMessageStoreFactory = persistent to disk
    let store_factory = MemoryMessageStoreFactory::new();
    
    // Log Factory: Where to send log output
    // StdLogger::Stdout = print to console
    // Alternative: file-based logging for production
    let log_factory = LogFactory::try_new(&StdLogger::Stdout)?;
    
    // Application Callbacks: Our custom logic
    let callbacks = MyApplication;
    
    // Application: Wraps our callbacks for the QuickFIX engine
    let app = Application::try_new(&callbacks)?;

    // Step 3: Create the Acceptor (FIX Server)
    // -----------------------------------------
    // The acceptor listens for incoming FIX connections
    let mut acceptor = Acceptor::try_new(
        &settings,      // Session configuration we built above
        &app,           // Our application callbacks
        &store_factory, // Message persistence strategy
        &log_factory,   // Logging strategy
        FixSocketServerKind::SingleThreaded, // Single-threaded for simplicity
        // In production, use MultiThreaded for better performance
    )?;

    // Step 4: Start the acceptor
    // --------------------------
    // This starts listening on the configured port
    // and begins accepting connections
    println!(">> connection handler START");
    acceptor.start()?;

    // Step 5: Keep running until user quits
    // --------------------------------------
    // Simple input loop: press 'q' to quit
    println!(">> App running, press 'q' to quit");
    let mut stdin = stdin().lock();
    let mut stdin_buf = [0];
    loop {
        // Read one character at a time
        let _ = stdin.read_exact(&mut stdin_buf);
        
        // Check if user pressed 'q' to quit
        if stdin_buf[0] == b'q' {
            break;
        }
    }

    // Step 6: Graceful shutdown
    // -------------------------
    // Stop accepting connections and disconnect existing sessions
    println!(">> connection handler STOP");
    acceptor.stop()?;

    println!(">> All cleared. Bye !");
    Ok(())
}

// =============================================================================
// Usage Notes
// =============================================================================
// To run this example:
//   cargo run --example demo_config
//
// To test connectivity:
//   1. Run this acceptor (it will listen on port 4000)
//   2. Use a FIX client to connect to localhost:4000
//   3. Configure the client with:
//      - BeginString: FIX.4.4
//      - SenderCompID: THEIR
//      - TargetCompID: ME
//
// The acceptor will accept the connection during session hours (12:30-23:30 UTC)
// and exchange heartbeats every 20 seconds.
// =============================================================================
