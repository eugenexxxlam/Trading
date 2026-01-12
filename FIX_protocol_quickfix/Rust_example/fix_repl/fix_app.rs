// =============================================================================
// FIX Application Callbacks with Full Logging
// =============================================================================
// This module implements a FIX application that logs all callback invocations.
// This is extremely useful for:
// - Understanding FIX message flow
// - Debugging connection issues
// - Monitoring system behavior
// - Learning how FIX sessions work
//
// In production, you would replace the logging with actual business logic.
// =============================================================================

use std::{
    io::{stdout, Write}, // For writing to console
    sync::atomic::{AtomicU32, Ordering}, // Thread-safe counter
};

use quickfix::*; // Import all QuickFIX types

// =============================================================================
// MyApplication: FIX Callback Handler with Message Tracking
// =============================================================================
// This application tracks and displays all FIX session events and messages
// =============================================================================

#[derive(Default)]
pub struct MyApplication {
    // Thread-safe counter for numbering messages
    // AtomicU32 allows multiple threads to safely increment the counter
    // This is important if using MultiThreaded socket server
    message_index: AtomicU32,
}

impl MyApplication {
    /// Create a new application instance
    pub fn new() -> Self {
        Self::default()
    }

    /// Increment and return the message counter
    /// Uses Relaxed ordering since we only need atomicity, not ordering guarantees
    fn inc_message_index(&self) {
        self.message_index.fetch_add(1, Ordering::Relaxed);
    }

    /// Helper function to print callback information in a consistent format
    /// 
    /// # Arguments
    /// * `callback_name` - Name of the callback being invoked
    /// * `session` - Session ID where the event occurred
    /// * `msg` - Optional FIX message (if applicable)
    fn print_callback(&self, callback_name: &str, session: &SessionId, msg: Option<&Message>) {
        let msg_count = self.message_index.load(Ordering::Relaxed);

        // Lock stdout for atomic write (prevents interleaved output in multithreaded scenarios)
        let mut stdout = stdout().lock();
        
        // Print callback information
        let _ = write!(stdout, "{callback_name}");
        let _ = write!(stdout, "(id={msg_count}) ");
        let _ = write!(stdout, "session={session:?}");
        
        // If there's a message, print it
        if let Some(msg) = msg {
            let _ = write!(stdout, " msg={msg:?}");
        }
        
        let _ = writeln!(stdout);
    }
}

// =============================================================================
// ApplicationCallback Implementation
// =============================================================================
// These are the core hooks into the FIX engine lifecycle. Every FIX application
// must implement this trait to handle session events and messages.
// =============================================================================

impl ApplicationCallback for MyApplication {
    // =========================================================================
    // on_create: Session Creation
    // =========================================================================
    // Called when a FIX session is initially created, before any connection
    // is established or logon is attempted.
    // 
    // Use cases:
    // - Allocate resources for this session
    // - Initialize order books or position trackers
    // - Load counterparty-specific configuration
    // - Set up monitoring/alerting
    // 
    // This is called once per session during application startup.
    // =========================================================================
    fn on_create(&self, session: &SessionId) {
        self.print_callback("on_create", session, None);
        
        // In production, you might do:
        // - Initialize a HashMap for this session's orders
        // - Load risk limits from database
        // - Set up metrics collection
        // - Allocate memory pools
    }

    // =========================================================================
    // on_logon: Successful Logon
    // =========================================================================
    // Called after both sides have successfully exchanged logon messages and
    // the session is now active and ready to send/receive application messages.
    // 
    // Use cases:
    // - Send initial market data subscriptions
    // - Request position reconciliation
    // - Enable order routing for this counterparty
    // - Send queued messages that were waiting for logon
    // - Update monitoring systems (session is UP)
    // 
    // This is called each time a session logs on (could happen multiple times
    // due to disconnections and reconnections).
    // =========================================================================
    fn on_logon(&self, session: &SessionId) {
        self.print_callback("on_logon", session, None);
        
        // In production, you might do:
        // - Send NewOrderSingle messages
        // - Subscribe to market data
        // - Request security definitions
        // - Enable trading
    }

    // =========================================================================
    // on_logout: Session Logout
    // =========================================================================
    // Called when a session logs out, either gracefully (via Logout message)
    // or due to connection failure, heartbeat timeout, or session time ending.
    // 
    // Use cases:
    // - Cancel all pending orders for this counterparty
    // - Stop sending market data
    // - Update position tracking
    // - Trigger alerts
    // - Log final state for reconciliation
    // 
    // After this callback, no more application messages can be sent until
    // the next on_logon.
    // =========================================================================
    fn on_logout(&self, session: &SessionId) {
        self.print_callback("on_logout", session, None);
        
        // In production, you might do:
        // - Cancel working orders
        // - Close positions if required
        // - Save state to database
        // - Alert operations team
    }

    // =========================================================================
    // on_msg_to_admin: Outgoing Administrative Messages
    // =========================================================================
    // Called just before sending an administrative message (Logon, Logout,
    // Heartbeat, TestRequest, ResendRequest, SequenceReset, Reject).
    // 
    // This is your last chance to modify the message before it's sent.
    // 
    // Use cases:
    // - Add custom fields to Logon message (username, password, etc.)
    // - Modify heartbeat content
    // - Add session-level flags
    // - Implement custom authentication
    // 
    // Note: The message parameter is mutable, so you can modify it.
    // =========================================================================
    fn on_msg_to_admin(&self, msg: &mut Message, session: &SessionId) {
        self.inc_message_index();
        self.print_callback("to_admin", session, Some(msg));
        
        // In production, you might do:
        // if msg.msg_type() == "A" {  // Logon message
        //     msg.set_field(553, "username");  // Username
        //     msg.set_field(554, "password");  // Password
        // }
    }

    // =========================================================================
    // on_msg_to_app: Outgoing Application Messages
    // =========================================================================
    // Called just before sending an application message (orders, executions,
    // market data, etc.). This is your last chance to validate or modify
    // the message before it goes out.
    // 
    // Use cases:
    // - Add custom fields
    // - Validate message content
    // - Implement risk checks (reject risky orders)
    // - Log outgoing messages for audit
    // - Update internal state
    // 
    // Return Err to prevent the message from being sent.
    // =========================================================================
    fn on_msg_to_app(&self, msg: &mut Message, session: &SessionId) -> Result<(), MsgToAppError> {
        self.inc_message_index();
        self.print_callback("to_app", session, Some(msg));
        
        // In production, you might do:
        // if msg.msg_type() == "D" {  // NewOrderSingle
        //     // Perform pre-send validation
        //     if !validate_order(msg) {
        //         return Err(MsgToAppError::ValidationFailed);
        //     }
        //     // Add firm identifier
        //     msg.set_field(1, "firm_account");
        // }
        
        Ok(())
    }

    // =========================================================================
    // on_msg_from_admin: Incoming Administrative Messages
    // =========================================================================
    // Called when an administrative message is received from the counterparty.
    // These are protocol-level messages, not business messages.
    // 
    // Use cases:
    // - Validate logon credentials
    // - Process sequence reset requests
    // - Handle resend requests
    // - Monitor heartbeat health
    // - Detect and respond to reject messages
    // 
    // Return Err to trigger a logout (session-level error).
    // =========================================================================
    fn on_msg_from_admin(
        &self,
        msg: &Message,
        session: &SessionId,
    ) -> Result<(), MsgFromAdminError> {
        self.inc_message_index();
        self.print_callback("from_admin", session, Some(msg));
        
        // In production, you might do:
        // if msg.msg_type() == "A" {  // Logon
        //     // Validate credentials
        //     let username = msg.get_field(553)?;
        //     let password = msg.get_field(554)?;
        //     if !authenticate(username, password) {
        //         return Err(MsgFromAdminError::AuthenticationFailed);
        //     }
        // }
        
        Ok(())
    }

    // =========================================================================
    // on_msg_from_app: Incoming Application Messages
    // =========================================================================
    // This is the most important callback - it processes all business messages
    // received from counterparties. This is where your core trading logic lives.
    // 
    // Message types you might receive:
    // - NewOrderSingle (D) - New order request
    // - OrderCancelRequest (F) - Cancel order request
    // - OrderCancelReplaceRequest (G) - Modify order request
    // - ExecutionReport (8) - Order acknowledgment or fill
    // - MarketDataRequest (V) - Market data subscription
    // - MarketDataSnapshotFullRefresh (W) - Market data snapshot
    // - Quote (S) - Price quote
    // - TradingSessionStatus (h) - Market status
    // ... and many more depending on FIX version and use case
    // 
    // Use cases:
    // - Match orders in an exchange
    // - Route orders to execution venues
    // - Update order books
    // - Calculate positions and P&L
    // - Apply risk checks
    // - Send execution reports
    // 
    // Return Err to trigger a business reject message.
    // =========================================================================
    fn on_msg_from_app(&self, msg: &Message, session: &SessionId) -> Result<(), MsgFromAppError> {
        self.inc_message_index();
        self.print_callback("from_app", session, Some(msg));
        
        // In production, you might do:
        //
        // match msg.msg_type().as_str() {
        //     "D" => {  // NewOrderSingle
        //         let symbol = msg.get_field(55)?;  // Symbol
        //         let side = msg.get_field(54)?;     // Side (1=Buy, 2=Sell)
        //         let quantity = msg.get_field(38)?; // OrderQty
        //         let price = msg.get_field(44)?;    // Price
        //         
        //         // Process the order
        //         process_new_order(symbol, side, quantity, price, session)?;
        //         
        //         // Send execution report back
        //         let exec_report = create_execution_report(...);
        //         send_to_target(exec_report, session)?;
        //     }
        //     "F" => {  // OrderCancelRequest
        //         // Cancel order logic
        //     }
        //     "V" => {  // MarketDataRequest
        //         // Subscribe to market data
        //     }
        //     _ => {
        //         // Unknown message type
        //         return Err(MsgFromAppError::UnsupportedMessageType);
        //     }
        // }
        
        Ok(())
    }
}

// =============================================================================
// Message Flow Summary
// =============================================================================
//
// Outgoing Message Flow:
// 1. Your code calls send_to_target(msg, session)
// 2. on_msg_to_app() or on_msg_to_admin() is called (your last chance to modify)
// 3. Message is sent over the network
// 4. Sequence number is incremented and persisted
//
// Incoming Message Flow:
// 1. Message arrives from network
// 2. Sequence number is validated
// 3. Message is parsed and validated against data dictionary
// 4. on_msg_from_app() or on_msg_from_admin() is called
// 5. Your application processes the message
// 6. If you return Ok(()), sequence number is incremented
// 7. If you return Err(()), appropriate reject/logout is sent
//
// Session Lifecycle:
// 1. on_create() - Session object created
// 2. Connection established (TCP)
// 3. Logon messages exchanged
// 4. on_logon() - Session active
// 5. Heartbeats and application messages flow
// 6. Logout (graceful or due to error)
// 7. on_logout() - Session inactive
// 8. Steps 2-7 repeat on reconnection
//
// =============================================================================
