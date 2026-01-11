package quickfix.examples.banzai;

import javax.swing.*;
import java.awt.event.KeyEvent;

public class IntegerNumberTextField extends JTextField {

    public void processKeyEvent(KeyEvent e) {
        char keyChar = e.getKeyChar();
        if (((keyChar >= '0') && (keyChar <= '9')) ||
                (keyChar == 8) || (keyChar == 127)) {
            super.processKeyEvent(e);
        }
    }
}
