package quickfix.examples.banzai;

import quickfix.SessionID;

public class LogonEvent {
    private final SessionID sessionID;
    private final boolean loggedOn;

    public LogonEvent(SessionID sessionID, boolean loggedOn) {
        this.sessionID = sessionID;
        this.loggedOn = loggedOn;
    }

    public SessionID getSessionID() {
        return sessionID;
    }

    public boolean isLoggedOn() {
        return loggedOn;
    }
}
