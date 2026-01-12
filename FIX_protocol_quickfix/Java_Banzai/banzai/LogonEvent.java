// ============================================================================
// LOGON EVENT - Session Status Notification
// ============================================================================
// Simple event object that carries information about FIX session logon/logout.
// Used in Observer pattern to notify GUI components of session state changes.
//
// PURPOSE:
// - Encapsulate logon/logout event data
// - Identify which session changed state
// - Indicate whether session is now logged on or logged out
// - Enable decoupled communication between FIX engine and GUI
//
// USAGE IN BANZAI:
// When FIX session logs on or logs out:
// 1. BanzaiApplication creates LogonEvent
// 2. BanzaiApplication notifies observers (GUI components)
// 3. Observers update UI based on logon state
//    - Enable/disable order entry controls
//    - Update status indicators
//    - Display connection messages
//
// OBSERVER PATTERN:
// BanzaiApplication extends Observable
// GUI components observe BanzaiApplication
// When logon state changes:
// - setChanged() called
// - notifyObservers(LogonEvent) called
// - All observers receive update
//
// EXAMPLE:
// // Session logs on
// LogonEvent event = new LogonEvent(sessionID, true);
// setChanged();
// notifyObservers(event);
// 
// // In GUI observer:
// public void update(Observable o, Object arg) {
//     if (arg instanceof LogonEvent) {
//         LogonEvent logonEvent = (LogonEvent) arg;
//         if (logonEvent.isLoggedOn()) {
//             enableOrderEntry();
//         } else {
//             disableOrderEntry();
//         }
//     }
// }
// ============================================================================

package quickfix.examples.banzai;

import quickfix.SessionID;

public class LogonEvent {
    // ========================================================================
    // EVENT DATA
    // ========================================================================
    private final SessionID sessionID;  // Which FIX session changed state
    private final boolean loggedOn;     // true=logged on, false=logged out

    // ========================================================================
    // CONSTRUCTOR
    // ========================================================================
    /**
     * Creates logon/logout event.
     * 
     * @param sessionID FIX session that changed state
     * @param loggedOn true if session logged on, false if logged out
     */
    public LogonEvent(SessionID sessionID, boolean loggedOn) {
        this.sessionID = sessionID;
        this.loggedOn = loggedOn;
    }

    // ========================================================================
    // ACCESSORS
    // ========================================================================
    
    /**
     * Gets the session ID that changed state.
     * 
     * SESSION ID FORMAT:
     * Identifies FIX session by BeginString:SenderCompID->TargetCompID
     * Example: "FIX.4.2:BANZAI->EXECUTOR"
     * 
     * USAGE:
     * Allows GUI to identify which session state changed,
     * important when multiple sessions are active.
     * 
     * @return SessionID of affected session
     */
    public SessionID getSessionID() {
        return sessionID;
    }

    /**
     * Checks if session is now logged on.
     * 
     * LOGON STATE:
     * - true: Session successfully logged on, can send orders
     * - false: Session logged out, cannot send orders
     * 
     * USAGE:
     * GUI uses this to:
     * - Enable/disable order entry buttons
     * - Update status bar
     * - Show connection indicator (green/red)
     * 
     * @return true if logged on, false if logged out
     */
    public boolean isLoggedOn() {
        return loggedOn;
    }
}
