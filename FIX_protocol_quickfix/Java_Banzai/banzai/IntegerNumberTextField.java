// ============================================================================
// INTEGER NUMBER TEXT FIELD - Integer-Only Input Field
// ============================================================================
// Custom Swing text field that accepts only integer input.
// Restricts user input to digits and control characters (backspace, delete).
//
// PURPOSE:
// - Prevent invalid input at keystroke level
// - Provide better UX than post-validation
// - Ensure quantity fields contain valid integers
// - Avoid parse errors and exceptions
//
// INPUT RESTRICTIONS:
// - Allows: digits 0-9
// - Allows: backspace (ASCII 8) and delete (ASCII 127)
// - Blocks: letters, symbols, decimal points, negative signs
//
// USAGE IN BANZAI:
// Used for order quantity input field:
// - Quantity must be positive integer
// - No decimals allowed (shares are whole units)
// - Prevents typing invalid characters
//
// EXAMPLE:
// IntegerNumberTextField qtyField = new IntegerNumberTextField();
// // User can type: "100", "1500", "50"
// // User cannot type: "12.5", "-10", "ABC"
//
// COMPARISON TO DoubleNumberTextField:
// - IntegerNumberTextField: No decimal point (quantity)
// - DoubleNumberTextField: Allows decimal point (price)
//
// LIMITATIONS:
// - Does not prevent:
//   - Leading zeros ("00123")
//   - Empty field
//   - Values exceeding integer range
// - Consider adding validation on focus lost or submit
//
// PRODUCTION CONSIDERATIONS:
// Real input validation would add:
// - Range validation (min/max values)
// - Formatter for display (thousands separator)
// - Visual feedback for invalid state
// - Paste validation
// - Input masks
// ============================================================================

package quickfix.examples.banzai;

import javax.swing.*;
import java.awt.event.KeyEvent;

/**
 * Text field that accepts only integer input.
 * 
 * EXTENDS:
 * JTextField - Standard Swing text input component
 * 
 * OVERRIDE:
 * processKeyEvent() - Intercepts keystrokes before display
 */
public class IntegerNumberTextField extends JTextField {

    /**
     * Processes keyboard events, filtering out non-integer characters.
     * 
     * KEY EVENT PROCESSING:
     * This method is called for every keystroke before the character
     * is displayed in the text field. We can:
     * - Allow the keystroke (call super.processKeyEvent())
     * - Block the keystroke (don't call super)
     * 
     * ALLOWED CHARACTERS:
     * 1. Digits '0' through '9'
     * 2. Backspace (keyChar == 8) - delete character to left
     * 3. Delete (keyChar == 127) - delete character to right
     * 
     * BLOCKED CHARACTERS:
     * Everything else:
     * - Letters (a-z, A-Z)
     * - Symbols (+, -, *, /, etc.)
     * - Decimal point (.)
     * - Space, comma, etc.
     * 
     * IMPLEMENTATION:
     * If character is valid → call super.processKeyEvent() (display it)
     * If character is invalid → do nothing (ignore keystroke)
     * 
     * EXAMPLE BEHAVIOR:
     * User types "1": '1' is digit → allowed → displays "1"
     * User types "a": 'a' is letter → blocked → nothing happens
     * User types ".": '.' is symbol → blocked → nothing happens
     * User types backspace: 8 is control → allowed → deletes character
     * 
     * ASCII VALUES:
     * - 8: Backspace
     * - 127: Delete
     * - 48-57: Digits '0'-'9'
     * 
     * @param e KeyEvent containing keystroke information
     */
    public void processKeyEvent(KeyEvent e) {
        char keyChar = e.getKeyChar();
        
        // Check if character is digit (0-9), backspace (8), or delete (127)
        if (((keyChar >= '0') && (keyChar <= '9')) ||
                (keyChar == 8) || (keyChar == 127)) {
            // Valid input - pass to parent class to display
            super.processKeyEvent(e);
        }
        // Invalid input - do nothing (keystroke ignored)
    }
}
