// ============================================================================
// DOUBLE NUMBER TEXT FIELD - Decimal Number Input Field
// ============================================================================
// Custom Swing text field that accepts numeric input with decimals.
// Restricts user input to digits, one decimal point, and control characters.
//
// PURPOSE:
// - Accept decimal numbers for price input
// - Prevent invalid input at keystroke level
// - Allow only one decimal point
// - Ensure price fields contain valid numbers
// - Provide better UX than post-validation
//
// INPUT RESTRICTIONS:
// - Allows: digits 0-9
// - Allows: one decimal point (.)
// - Allows: backspace (ASCII 8) and delete (ASCII 127)
// - Blocks: letters, symbols, multiple decimal points, negative signs
//
// USAGE IN BANZAI:
// Used for price input fields:
// - Limit price: "150.50", "99.99"
// - Stop price: "149.75", "151.25"
// - Execution price display
//
// EXAMPLE:
// DoubleNumberTextField priceField = new DoubleNumberTextField();
// // User can type: "150.50", "99", "0.01"
// // User cannot type: "12..5", "-10", "ABC"
//
// DECIMAL POINT LOGIC:
// - First decimal point: allowed
// - Second decimal point: blocked
// - Implementation checks if text already contains "."
//
// COMPARISON TO IntegerNumberTextField:
// - DoubleNumberTextField: Allows one decimal point (price)
// - IntegerNumberTextField: No decimal point (quantity)
//
// LIMITATIONS:
// - Does not prevent:
//   - Leading zeros ("00150.50")
//   - Multiple decimals via paste
//   - Empty field or just "."
//   - Scientific notation
// - Does not enforce:
//   - Minimum tick size (e.g., $0.01 increments)
//   - Price range limits
// - Consider adding validation on focus lost or submit
//
// PRODUCTION CONSIDERATIONS:
// Real price input would add:
// - Tick size validation (e.g., $0.01, $0.05)
// - Range validation (min/max price)
// - Decimal place limits (e.g., 2 decimals for stocks)
// - Formatter for display
// - Visual feedback for invalid state
// - Paste validation
// ============================================================================

package quickfix.examples.banzai;

import javax.swing.*;
import java.awt.event.KeyEvent;

/**
 * Text field that accepts numeric input with decimal point.
 * 
 * EXTENDS:
 * JTextField - Standard Swing text input component
 * 
 * OVERRIDE:
 * processKeyEvent() - Intercepts keystrokes before display
 */
public class DoubleNumberTextField extends JTextField {

    /**
     * Processes keyboard events, filtering out non-numeric characters.
     * 
     * KEY EVENT PROCESSING:
     * This method is called for every keystroke before the character
     * is displayed in the text field. We validate and either:
     * - Allow the keystroke (call super.processKeyEvent())
     * - Block the keystroke (don't call super)
     * 
     * ALLOWED CHARACTERS:
     * 1. Digits '0' through '9' - always allowed
     * 2. Backspace (keyChar == 8) - delete character to left
     * 3. Delete (keyChar == 127) - delete character to right
     * 4. Decimal point '.' - only if not already present
     * 
     * BLOCKED CHARACTERS:
     * Everything else:
     * - Letters (a-z, A-Z)
     * - Symbols (+, -, *, /, etc.)
     * - Space, comma
     * - Second decimal point
     * 
     * DECIMAL POINT LOGIC:
     * When user types '.':
     * 1. Get current text content
     * 2. Check if text already contains "."
     * 3. If yes: block (don't allow second decimal)
     * 4. If no: allow (add first decimal)
     * 
     * IMPLEMENTATION:
     * Three conditions checked in order:
     * 1. Digit or control character? → allow
     * 2. Decimal point AND no decimal present? → allow
     * 3. Else → block
     * 
     * EXAMPLE BEHAVIOR:
     * Text="150", User types ".", Contains "."? No → "150."
     * Text="150.", User types "5", Digit? Yes → "150.5"
     * Text="150.5", User types ".", Contains "."? Yes → blocked
     * Text="150.50", User types backspace → "150.5"
     * 
     * ASCII VALUES:
     * - 8: Backspace
     * - 127: Delete
     * - 46: Decimal point '.'
     * - 48-57: Digits '0'-'9'
     * 
     * @param e KeyEvent containing keystroke information
     */
    public void processKeyEvent(KeyEvent e) {
        char keyChar = e.getKeyChar();
        
        // ====================================================================
        // CHECK 1: Digit or control character?
        // ====================================================================
        if (((keyChar >= '0') && (keyChar <= '9')) ||
                (keyChar == 8) || (keyChar == 127)) {
            // Valid digit or control - always allowed
            super.processKeyEvent(e);
        } 
        // ====================================================================
        // CHECK 2: Decimal point (only if not already present)?
        // ====================================================================
        else if (keyChar == '.') {
            String text = getText();
            if (!text.contains(".")) {
                // First decimal point - allowed
                super.processKeyEvent(e);
            }
            // Second decimal point - blocked (do nothing)
        }
        // All other characters blocked (do nothing)
    }
}
