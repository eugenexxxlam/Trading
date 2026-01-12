// =============================================================================
// Command Execution Engine for FIX REPL
// =============================================================================
// This module implements the interactive shell (REPL - Read-Eval-Print Loop)
// that allows users to interact with a FIX connection in real-time.
//
// Key concepts:
// - Interactive user input handling
// - Generic programming with ConnectionHandler trait
// - Command execution and result display
// - Proper I/O buffering for responsive terminal interaction
// =============================================================================

use std::io::{self, stdin, stdout, BufRead, StdinLock, Write};

use quickfix::{send_to_target, ConnectionHandler};

use crate::command_parser::ShellCommand;

// =============================================================================
// FixShell: Interactive FIX Command Shell
// =============================================================================
// This struct manages the REPL state and provides an interactive interface
// for sending commands to the FIX engine.
// =============================================================================

pub struct FixShell<'a> {
    /// Locked stdin handle for efficient reading
    /// Locking prevents multiple locks per read, improving performance
    stdin: StdinLock<'a>,
    
    /// Buffer to store the last command entered by the user
    /// Pre-allocated with reasonable capacity to avoid frequent reallocations
    last_command: String,
}

impl FixShell<'_> {
    /// Create a new interactive shell instance
    /// 
    /// # Returns
    /// A new FixShell ready to accept user input
    pub fn new() -> Self {
        Self {
            // Lock stdin once for the lifetime of the shell
            stdin: stdin().lock(),
            
            // Pre-allocate 1KB for command buffer
            // This is more than enough for typical commands
            last_command: String::with_capacity(1024),
        }
    }

    // =========================================================================
    // User Input Handling
    // =========================================================================
    
    /// Read one line of input from the user
    /// 
    /// This function:
    /// 1. Displays the prompt "FIX> "
    /// 2. Waits for user input (blocking)
    /// 3. Stores the input in last_command buffer
    /// 
    /// # Returns
    /// Ok(()) on success, Err on I/O error
    fn read_user_input(&mut self) -> io::Result<()> {
        // Print prompt
        let mut stdout = stdout().lock();
        write!(stdout, "FIX> ")?;
        
        // Flush immediately so prompt appears before waiting for input
        // Without flush, the prompt might not appear until after user types
        stdout.flush()?;
        
        // Drop stdout lock to allow command output
        drop(stdout);

        // Clear previous command from buffer (reuse allocation)
        self.last_command.clear();
        
        // Read one line from stdin (blocks until Enter is pressed)
        self.stdin.read_line(&mut self.last_command)?;

        Ok(())
    }

    // =========================================================================
    // Command Execution
    // =========================================================================
    
    /// Execute a parsed command
    /// 
    /// This function takes a ShellCommand and executes it against the
    /// provided ConnectionHandler. The ConnectionHandler trait is implemented
    /// by both Acceptor and Initiator, allowing this code to work with both.
    /// 
    /// # Arguments
    /// * `command` - The parsed command to execute
    /// * `connection_handler` - The FIX connection handler (Acceptor or Initiator)
    fn exec_command<C: ConnectionHandler>(
        &mut self,
        command: ShellCommand,
        connection_handler: &mut C,
    ) {
        match command {
            // -----------------------------------------------------------------
            // Help Command
            // -----------------------------------------------------------------
            // Display all available commands and their descriptions
            // -----------------------------------------------------------------
            ShellCommand::Help => {
                println!("Available commands:");
                println!("- status : Print connection handler status");
                println!("- start  : Start connection handler");
                println!("- block  : Block connection handler");
                println!("- poll   : Poll connection handler");
                println!("- stop   : Stop connection handler");
                println!("- send_to K1=V1|K2=V2|… sender target : Create new FIX message");
                println!();
                println!("Examples:");
                println!("  send_to 35=D|55=AAPL|54=1|38=100 CLIENT EXCHANGE");
                println!("    (Send buy order for 100 shares of AAPL)");
                println!();
                println!("  send_to 35=V|262=REQ1|263=1|55=MSFT CLIENT EXCHANGE");
                println!("    (Subscribe to market data for MSFT)");
            }
            
            // -----------------------------------------------------------------
            // Start Command
            // -----------------------------------------------------------------
            // Start the connection handler
            // - For Acceptor: Begin listening on configured port
            // - For Initiator: Begin attempting to connect to configured host
            // -----------------------------------------------------------------
            ShellCommand::Start => {
                println!("RESULT: {:?}", connection_handler.start());
                // Possible results:
                // - Ok(()) - Successfully started
                // - Err(AlreadyRunning) - Already started
                // - Err(ConfigurationError) - Invalid configuration
            }
            
            // -----------------------------------------------------------------
            // Stop Command
            // -----------------------------------------------------------------
            // Stop the connection handler
            // - Sends logout messages to all connected sessions
            // - Closes all sockets
            // - Flushes message stores
            // -----------------------------------------------------------------
            ShellCommand::Stop => {
                println!("RESULT: {:?}", connection_handler.stop());
                // Possible results:
                // - Ok(()) - Successfully stopped
                // - Err(NotRunning) - Already stopped
            }
            
            // -----------------------------------------------------------------
            // Status Command
            // -----------------------------------------------------------------
            // Display current connection state
            // Useful for debugging connectivity issues
            // -----------------------------------------------------------------
            ShellCommand::Status => {
                println!(
                    "Connection handler status: logged_on={:?}, stopped={:?}",
                    connection_handler.is_logged_on(),
                    connection_handler.is_stopped(),
                );
                // logged_on=true means at least one session is active
                // stopped=true means the handler is not running
            }
            
            // -----------------------------------------------------------------
            // Block Command
            // -----------------------------------------------------------------
            // Block waiting for incoming messages
            // This is primarily for testing - blocks the thread until a
            // message arrives or timeout occurs
            // -----------------------------------------------------------------
            ShellCommand::Block => {
                println!("RESULT: {:?}", connection_handler.block());
                println!("(Blocked until message received)");
                // Use this to test message receiving without polling
            }
            
            // -----------------------------------------------------------------
            // Poll Command
            // -----------------------------------------------------------------
            // Poll for messages without blocking
            // Checks for pending messages and processes them immediately
            // Returns immediately whether or not messages were found
            // -----------------------------------------------------------------
            ShellCommand::Poll => {
                println!("RESULT: {:?}", connection_handler.poll());
                // Ok(true) - Messages were processed
                // Ok(false) - No messages pending
                // Err(...) - Error occurred
            }
            
            // -----------------------------------------------------------------
            // Send Message Command
            // -----------------------------------------------------------------
            // Send a FIX message to a specific session
            // This is the most powerful command - allows sending any FIX message
            // -----------------------------------------------------------------
            ShellCommand::SendMessage(msg, session_id) => {
                println!("Sending {msg:?} to {session_id:?}");
                
                // send_to_target is the main function for sending FIX messages
                // It will:
                // 1. Call on_msg_to_app() callback (your last chance to modify)
                // 2. Add standard header fields (sequence number, timestamp, etc.)
                // 3. Calculate checksum
                // 4. Send over the network
                // 5. Store in message log
                println!("SEND_RESULT: {:?}", send_to_target(msg, &session_id));
                
                // Possible results:
                // - Ok(()) - Message queued for sending
                // - Err(SessionNotFound) - No session with that ID
                // - Err(NotLoggedOn) - Session exists but not logged on
                // - Err(ValidationError) - Message failed validation
            }
            
            // -----------------------------------------------------------------
            // No Operation / Quit
            // -----------------------------------------------------------------
            // Do nothing - user pressed Enter or typed quit
            // -----------------------------------------------------------------
            ShellCommand::NoOperation | ShellCommand::Quit => {}
        }
    }

    // =========================================================================
    // Main REPL Loop
    // =========================================================================
    
    /// Run the interactive Read-Eval-Print Loop
    /// 
    /// This is the main entry point for the interactive shell. It:
    /// 1. Displays a welcome message
    /// 2. Prompts for user input
    /// 3. Parses the input into a command
    /// 4. Executes the command
    /// 5. Repeats until user quits
    /// 
    /// # Arguments
    /// * `connection_handler` - The FIX connection handler to control
    /// 
    /// # Termination
    /// The loop terminates when:
    /// - User types "quit" or "q"
    /// - User presses CTRL-D (EOF)
    pub fn repl<C: ConnectionHandler>(&mut self, connection_handler: &mut C) {
        // Display welcome message
        println!(">> Type 'help' or '?' for more information, 'quit' or 'q' to exit.");

        // Main loop - runs until user quits
        loop {
            // ================================================================
            // Step 1: Read user input
            // ================================================================
            // This blocks waiting for the user to type something and press Enter
            // ================================================================
            
            self.read_user_input().expect("I/O error");

            // ================================================================
            // Step 2: Handle EOF (CTRL-D)
            // ================================================================
            // If stdin reaches EOF (user pressed CTRL-D), read_line returns
            // an empty string. This is a common way to exit interactive programs.
            // ================================================================
            
            if self.last_command.is_empty() {
                println!("CTRL-D");
                break;
            }
            
            // ================================================================
            // Step 3: Parse and execute the command
            // ================================================================
            // Try to parse the input string into a ShellCommand
            // If successful, execute it. If parsing fails, show error.
            // ================================================================
            
            match self.last_command.parse::<ShellCommand>() {
                // User wants to quit
                Ok(ShellCommand::Quit) => break,
                
                // Execute the parsed command
                Ok(cmd) => self.exec_command(cmd, connection_handler),
                
                // Parsing failed - show error message
                Err(err) => eprintln!("Error when running command: {err}"),
            }
        }
    }
}

// =============================================================================
// Usage Pattern
// =============================================================================
// This shell demonstrates several important Rust patterns:
//
// 1. Generic Programming:
//    - Works with any type implementing ConnectionHandler
//    - No code duplication for Acceptor vs Initiator
//
// 2. Resource Management:
//    - StdinLock held for shell lifetime (efficient)
//    - String buffer reused across iterations (no allocation overhead)
//    - Explicit stdout flushing for responsive UI
//
// 3. Error Handling:
//    - Uses Result types throughout
//    - Graceful error messages for users
//    - Panics only on truly unexpected errors (I/O failure)
//
// 4. Interactive UX:
//    - Clear prompts and help text
//    - Immediate feedback on command execution
//    - Support for CTRL-D (standard Unix convention)
//
// =============================================================================
// Testing Workflow Example
// =============================================================================
//
// 1. Start two terminals
//
// 2. Terminal 1 - Start acceptor:
//    $ cargo run --example fix_repl -- acceptor acceptor.cfg
//    >> Creating resources
//    >> connection handler START
//    >> Type 'help' or '?' for more information
//    FIX> status
//    Connection handler status: logged_on=false, stopped=false
//
// 3. Terminal 2 - Start initiator:
//    $ cargo run --example fix_repl -- initiator initiator.cfg
//    >> Creating resources
//    >> connection handler START
//    >> Type 'help' or '?' for more information
//    FIX> status
//    Connection handler status: logged_on=true, stopped=false
//
// 4. Terminal 2 - Send a test order:
//    FIX> send_to 35=D|55=AAPL|54=1|38=100|40=2|44=150.00 CLIENT EXCHANGE
//    Sending Message(...) to SessionId(...)
//    SEND_RESULT: Ok(())
//
// 5. Terminal 1 - Should receive and log the order in on_msg_from_app callback
//
// 6. Both terminals - Clean shutdown:
//    FIX> quit
//    >> connection handler STOP
//    >> All cleared. Bye !
//
// =============================================================================
