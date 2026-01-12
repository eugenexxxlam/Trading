// =============================================================================
// Command Parser for FIX REPL
// =============================================================================
// This module handles parsing user input into executable commands.
// It demonstrates:
// - Custom error types with the Error trait
// - FromStr trait implementation for string parsing
// - FIX message construction from text format
// - SessionId creation
//
// The parser supports a simple command syntax for interacting with FIX sessions.
// =============================================================================

use std::{error::Error, fmt, str::FromStr};

use quickfix::{FieldMap, Message, SessionId};

// =============================================================================
// Error Types
// =============================================================================
// Custom error type for command parsing failures
// Implementing Error and Display traits makes this a proper Rust error type
// =============================================================================

#[derive(Debug)]
#[non_exhaustive]  // Allows adding variants later without breaking compatibility
pub enum BadCommand {
    /// User entered an unrecognized command
    Unknown(String),
    
    /// Wrong number of arguments provided
    InvalidArgumentCount { 
        current: usize,   // How many were provided
        expected: usize   // How many were expected
    },
    
    /// Argument value is invalid or malformed
    InvalidArgument(&'static str),
}

// Implement Display to provide human-readable error messages
impl fmt::Display for BadCommand {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BadCommand::Unknown(cmd) => 
                write!(f, "unknown command: {cmd}"),
            
            BadCommand::InvalidArgumentCount { current, expected } => 
                write!(
                    f,
                    "invalid argument count: current={current}, expected={expected}"
                ),
            
            BadCommand::InvalidArgument(msg) => 
                write!(f, "invalid argument: {msg}"),
        }
    }
}

// Implement Error trait to make this a standard Rust error
impl Error for BadCommand {}

// =============================================================================
// Command Enumeration
// =============================================================================
// Represents all possible commands the REPL supports
// Each variant corresponds to a user action
// =============================================================================

#[derive(Debug)]
pub enum ShellCommand {
    /// Exit the REPL
    Quit,
    
    /// Display help information
    Help,
    
    /// Start the connection handler (begin accepting/initiating connections)
    Start,
    
    /// Stop the connection handler (close all connections)
    Stop,
    
    /// Display current connection status
    Status,
    
    /// Block waiting for incoming messages (for testing)
    Block,
    
    /// Poll for messages without blocking (for testing)
    Poll,
    
    /// Send a FIX message to a specific session
    /// Parameters: (message, session_id)
    SendMessage(Message, SessionId),
    
    /// Empty command (user just pressed Enter)
    NoOperation,
}

// =============================================================================
// Command Parser Implementation
// =============================================================================
// Implements FromStr trait to parse strings into commands
// This is idiomatic Rust - using traits for conversions
// =============================================================================

impl FromStr for ShellCommand {
    type Err = BadCommand;

    /// Parse a string into a ShellCommand
    /// 
    /// # Supported Commands
    /// - `quit` or `q` - Exit
    /// - `help` or `?` - Show help
    /// - `start` - Start connection handler
    /// - `stop` - Stop connection handler
    /// - `status` - Show connection status
    /// - `block` - Block for messages
    /// - `poll` - Poll for messages
    /// - `send_to MSG SENDER TARGET` - Send FIX message
    /// - (empty) - No operation
    fn from_str(source: &str) -> Result<Self, Self::Err> {
        // Trim whitespace and match against known commands
        match source.trim() {
            // Exit commands
            "quit" | "q" => Ok(Self::Quit),
            
            // Help commands
            "help" | "?" => Ok(Self::Help),
            
            // Connection management
            "start" => Ok(Self::Start),
            "stop" => Ok(Self::Stop),
            "status" => Ok(Self::Status),
            
            // Message processing modes
            "block" => Ok(Self::Block),
            "poll" => Ok(Self::Poll),
            
            // Empty input
            "" => Ok(Self::NoOperation),
            
            // Send message command - more complex parsing required
            cmd if cmd.starts_with("send_to ") => {
                parse_send_to(cmd).map(|x| Self::SendMessage(x.0, x.1))
            }
            
            // Unknown command
            cmd => Err(BadCommand::Unknown(cmd.to_string())),
        }
    }
}

// =============================================================================
// Send Message Parser
// =============================================================================
// Parses the "send_to" command which has a complex syntax:
//   send_to TAG=VALUE|TAG=VALUE|... sender_id target_id
//
// Example:
//   send_to 35=D|55=AAPL|54=1|38=100|40=2|44=150.50 CLIENT EXCHANGE
//
// This creates a NewOrderSingle (35=D) for Apple stock:
// - 55=AAPL (Symbol)
// - 54=1 (Side: Buy)
// - 38=100 (Quantity)
// - 40=2 (Order Type: Limit)
// - 44=150.50 (Price)
// =============================================================================

fn parse_send_to(source: &str) -> Result<(Message, SessionId), BadCommand> {
    // =========================================================================
    // Step 1: Tokenize the command
    // =========================================================================
    // Split on whitespace: ["send_to", "TAG=VALUE|...", "SENDER", "TARGET"]
    // =========================================================================
    
    let mut tokens = source.split_whitespace();
    
    // Skip the "send_to" part (we already validated this)
    debug_assert_eq!(tokens.next().as_deref(), Some("send_to"));

    // Extract the three required arguments
    let text_msg = tokens.next().ok_or(BadCommand::InvalidArgumentCount {
        current: 0,
        expected: 3,
    })?;
    
    let text_sender = tokens.next().ok_or(BadCommand::InvalidArgumentCount {
        current: 1,
        expected: 3,
    })?;
    
    let text_target = tokens.next().ok_or(BadCommand::InvalidArgumentCount {
        current: 2,
        expected: 3,
    })?;

    // =========================================================================
    // Step 2: Parse the FIX message from TAG=VALUE format
    // =========================================================================
    // FIX messages are represented as tag-value pairs
    // Tag 35 is MsgType (defines what kind of message this is)
    // =========================================================================
    
    let mut msg = Message::new();
    
    // Split on pipe character to get individual fields
    for field in text_msg.split('|') {
        // Split each field into tag and value
        let mut field_tokens = field.splitn(2, '=');

        // Parse the tag number (integer)
        let tag = field_tokens
            .next()
            .ok_or(BadCommand::InvalidArgument("Invalid tag"))?
            .parse()
            .ok()
            .ok_or(BadCommand::InvalidArgument("Invalid tag number"))?;

        // Get the value (string)
        let value = field_tokens
            .next()
            .ok_or(BadCommand::InvalidArgument("Invalid value"))?;

        // Set the field in the message
        // Tags are integers defined in the FIX specification
        msg.set_field(tag, value)
            .ok()
            .expect("Fail to set msg tag=value");
    }

    // =========================================================================
    // Step 3: Create SessionId
    // =========================================================================
    // A FIX session is uniquely identified by:
    // - BeginString (FIX version, e.g., "FIX.4.4")
    // - SenderCompID (who is sending)
    // - TargetCompID (who is receiving)
    // - Optional qualifier (for multiple sessions between same parties)
    // =========================================================================
    
    let session_id = SessionId::try_new(
        "FIX.4.4",    // FIX version - hardcoded for this example
        text_sender,   // Our identifier
        text_target,   // Counterparty identifier
        ""            // Empty qualifier
    )
    .expect("Fail to allocate new session ID");

    Ok((msg, session_id))
}

// =============================================================================
// FIX Message Tag Reference
// =============================================================================
// Here are some common FIX tags you might use in send_to commands:
//
// Message Type (Tag 35) - Required in every message:
//   D = NewOrderSingle
//   F = OrderCancelRequest
//   G = OrderCancelReplaceRequest
//   8 = ExecutionReport
//   V = MarketDataRequest
//
// Order Fields:
//   11 = ClOrdID (Client Order ID)
//   55 = Symbol (e.g., "AAPL", "MSFT")
//   54 = Side (1=Buy, 2=Sell)
//   38 = OrderQty (quantity)
//   40 = OrdType (1=Market, 2=Limit, 3=Stop, 4=StopLimit)
//   44 = Price (for limit orders)
//   59 = TimeInForce (0=Day, 1=GTC, 3=IOC, 4=FOK)
//   
// Execution Fields:
//   37 = OrderID (exchange-assigned)
//   17 = ExecID (execution ID)
//   150 = ExecType (0=New, 1=PartialFill, 2=Fill, 4=Canceled)
//   39 = OrdStatus (0=New, 1=PartiallyFilled, 2=Filled, 4=Canceled)
//   32 = LastQty (quantity filled in this execution)
//   31 = LastPx (price of this fill)
//
// Market Data Fields:
//   262 = MDReqID (Market Data Request ID)
//   263 = SubscriptionRequestType (0=Snapshot, 1=Subscribe, 2=Unsubscribe)
//   264 = MarketDepth (0=Full, 1=Top)
//   268 = NoMDEntries (number of price levels)
//   269 = MDEntryType (0=Bid, 1=Offer, 2=Trade)
//   270 = MDEntryPx (price)
//   271 = MDEntrySize (size)
//
// =============================================================================
// Example Commands
// =============================================================================
//
// Send a market buy order for 100 shares of AAPL:
//   send_to 35=D|55=AAPL|54=1|38=100|40=1 CLIENT EXCHANGE
//
// Send a limit sell order for 50 shares of MSFT at $350:
//   send_to 35=D|55=MSFT|54=2|38=50|40=2|44=350.00 CLIENT EXCHANGE
//
// Cancel an order:
//   send_to 35=F|41=ORDER123|11=CANCEL456 CLIENT EXCHANGE
//
// Request market data for TSLA:
//   send_to 35=V|262=MD001|263=1|55=TSLA CLIENT EXCHANGE
//
// =============================================================================
