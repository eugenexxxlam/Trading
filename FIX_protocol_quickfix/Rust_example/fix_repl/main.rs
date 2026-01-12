// =============================================================================
// QuickFIX Rust Example: Interactive FIX REPL (Read-Eval-Print Loop)
// =============================================================================
// This example provides a full-featured interactive shell for testing FIX
// connections. It supports both acceptor (server) and initiator (client) modes
// and allows you to send messages and inspect connection state in real-time.
//
// Key Learning Points:
// 1. Supporting both acceptor and initiator modes
// 2. Interactive command-line interface for FIX operations
// 3. Generic programming with ConnectionHandler trait
// 4. Real-time message sending and connection management
// =============================================================================

use std::{env, process::exit};

use quickfix::{
    Acceptor,          // FIX server (accepts connections)
    Application,       // Wrapper for callbacks
    ConnectionHandler, // Common trait for Acceptor and Initiator
    FileMessageStoreFactory, // Persistent message storage
    FixSocketServerKind,     // Threading model
    Initiator,               // FIX client (initiates connections)
    LogFactory,              // Logging factory
    QuickFixError,           // Error type
    SessionSettings,         // Configuration container
    StdLogger,               // Standard output logger
};

// Import our custom modules
use crate::{
    command_exec::FixShell,  // Interactive shell implementation
    fix_app::MyApplication,  // FIX callback handlers
};

// Module declarations - these files must exist in the same directory
mod command_exec;    // Shell execution logic
mod command_parser;  // Command parsing logic
mod fix_app;         // FIX application callbacks

// =============================================================================
// Main Entry Point
// =============================================================================
// Parses arguments and launches either an acceptor or initiator with an
// interactive shell for sending commands
// =============================================================================

fn main() -> Result<(), QuickFixError> {
    // =========================================================================
    // Step 1: Parse Command-Line Arguments
    // =========================================================================
    // Required args: [acceptor|initiator] <config_file>
    // =========================================================================
    
    let args: Vec<_> = env::args().collect();
    
    // Use pattern matching to destructure and validate arguments
    let (Some(connect_mode), Some(config_file)) = (args.get(1), args.get(2)) else {
        eprintln!(
            "Bad program usage: {} [acceptor|initiator] <config_file>",
            args[0]
        );
        exit(1);
    };

    // =========================================================================
    // Step 2: Initialize FIX Engine Components
    // =========================================================================
    // These components are shared between acceptor and initiator modes
    // =========================================================================
    
    println!(">> Creating resources");
    
    // Load configuration from file
    // Supports both acceptor and initiator configurations
    let settings = SessionSettings::try_from_path(config_file)?;
    
    // Use file-based message store for persistence
    // Critical for maintaining sequence numbers across restarts
    let store_factory = FileMessageStoreFactory::try_new(&settings)?;
    
    // Log to stdout for visibility during testing
    let log_factory = LogFactory::try_new(&StdLogger::Stdout)?;
    
    // Create our custom application with full callback logging
    let callbacks = MyApplication::new();
    
    // Wrap callbacks for the QuickFIX engine
    let app = Application::try_new(&callbacks)?;

    // =========================================================================
    // Step 3: Create Connection Handler Based on Mode
    // =========================================================================
    // Branch based on command-line argument to create acceptor or initiator
    // Both implement the ConnectionHandler trait, allowing generic handling
    // =========================================================================
    
    match connect_mode.as_str() {
        // ---------------------------------------------------------------------
        // Initiator Mode: Connect to a remote FIX acceptor
        // ---------------------------------------------------------------------
        // Use case: Trading client connecting to an exchange or broker
        // The initiator will attempt to connect to the configured host:port
        // and maintain the connection with automatic reconnection
        // ---------------------------------------------------------------------
        "initiator" => server_loop(Initiator::try_new(
            &settings,      // Contains SocketConnectHost and SocketConnectPort
            &app,           // Our callback handlers
            &store_factory, // Message persistence
            &log_factory,   // Logging
            FixSocketServerKind::SingleThreaded, // Threading model
        )?),
        
        // ---------------------------------------------------------------------
        // Acceptor Mode: Listen for incoming FIX connections
        // ---------------------------------------------------------------------
        // Use case: Exchange or broker accepting client connections
        // The acceptor will listen on the configured port for incoming
        // connections from multiple trading counterparties
        // ---------------------------------------------------------------------
        "acceptor" => server_loop(Acceptor::try_new(
            &settings,      // Contains SocketAcceptPort
            &app,           // Our callback handlers
            &store_factory, // Message persistence
            &log_factory,   // Logging
            FixSocketServerKind::SingleThreaded, // Threading model
        )?),
        
        // ---------------------------------------------------------------------
        // Invalid Mode
        // ---------------------------------------------------------------------
        _ => {
            eprintln!("Invalid connection mode");
            exit(1);
        }
    }?;

    println!(">> All cleared. Bye !");
    Ok(())
}

// =============================================================================
// Generic Server Loop
// =============================================================================
// This function works with both Acceptor and Initiator because they both
// implement the ConnectionHandler trait. This is a great example of generic
// programming in Rust - write once, use with multiple types.
// 
// The ConnectionHandler trait provides:
// - start() / stop() - Lifecycle management
// - is_logged_on() / is_stopped() - State queries
// - block() / poll() - Message processing control
// =============================================================================

fn server_loop<C: ConnectionHandler>(mut connection_handler: C) -> Result<(), QuickFixError> {
    // =========================================================================
    // Start the Connection Handler
    // =========================================================================
    // For Initiator: Begins attempting to connect to remote host
    // For Acceptor: Begins listening on configured port
    // =========================================================================
    
    println!(">> connection handler START");
    connection_handler.start()?;

    // =========================================================================
    // Launch Interactive Shell
    // =========================================================================
    // The REPL allows real-time interaction with the FIX connection:
    // - Send messages
    // - Check connection status
    // - Control the connection (start/stop/block/poll)
    // =========================================================================
    
    let mut shell = FixShell::new();
    shell.repl(&mut connection_handler);
    // The REPL blocks here until the user quits (types 'quit' or presses CTRL-D)

    // =========================================================================
    // Stop the Connection Handler
    // =========================================================================
    // Graceful shutdown:
    // - Sends logout messages to all connected sessions
    // - Flushes pending messages
    // - Closes sockets
    // - Saves sequence numbers
    // =========================================================================
    
    println!(">> connection handler STOP");
    connection_handler.stop()?;

    Ok(())
}

// =============================================================================
// Usage Examples
// =============================================================================
//
// Run as Acceptor (Server):
//   cargo run --example fix_repl -- acceptor acceptor.cfg
//
// Run as Initiator (Client):
//   cargo run --example fix_repl -- initiator initiator.cfg
//
// =============================================================================
// Example Acceptor Configuration (acceptor.cfg)
// =============================================================================
// [DEFAULT]
// ConnectionType=acceptor
// FileStorePath=./fix_acceptor_store
// FileLogPath=./fix_acceptor_log
// StartTime=00:00:00
// EndTime=00:00:00
//
// [SESSION]
// BeginString=FIX.4.4
// SenderCompID=EXCHANGE
// TargetCompID=CLIENT
// HeartBtInt=30
// SocketAcceptPort=5001
// DataDictionary=spec/FIX44.xml
//
// =============================================================================
// Example Initiator Configuration (initiator.cfg)
// =============================================================================
// [DEFAULT]
// ConnectionType=initiator
// ReconnectInterval=5
// FileStorePath=./fix_initiator_store
// FileLogPath=./fix_initiator_log
// StartTime=00:00:00
// EndTime=00:00:00
//
// [SESSION]
// BeginString=FIX.4.4
// SenderCompID=CLIENT
// TargetCompID=EXCHANGE
// HeartBtInt=30
// SocketConnectHost=127.0.0.1
// SocketConnectPort=5001
// DataDictionary=spec/FIX44.xml
//
// =============================================================================
// Interactive Commands
// =============================================================================
// Once running, you can use these commands:
//
// help      - Show available commands
// status    - Display connection status (logged on, stopped)
// start     - Start the connection handler
// stop      - Stop the connection handler
// block     - Block waiting for messages (for testing)
// poll      - Poll for messages without blocking
// send_to   - Send a custom FIX message
//             Format: send_to TAG=VALUE|TAG=VALUE sender target
//             Example: send_to 35=D|54=1|55=AAPL|38=100 CLIENT EXCHANGE
// quit      - Exit the program
//
// =============================================================================
