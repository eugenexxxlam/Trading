
package quickfix.examples.banzai.ui;

import java.awt.BorderLayout;

import javax.swing.JCheckBoxMenuItem;
import javax.swing.JFrame;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;

import quickfix.examples.banzai.Banzai;
import quickfix.examples.banzai.BanzaiApplication;
import quickfix.examples.banzai.ExecutionTableModel;
import quickfix.examples.banzai.OrderTableModel;

/**
 * Main application window
 */
public class BanzaiFrame extends JFrame {

    public BanzaiFrame(OrderTableModel orderTableModel, ExecutionTableModel executionTableModel,
            final BanzaiApplication application) {
        super();
        setTitle("Banzai!");
        setSize(600, 400);

        if (System.getProperties().containsKey("openfix")) {
            createMenuBar(application);
        }
        getContentPane().add(new BanzaiPanel(orderTableModel, executionTableModel, application),
                BorderLayout.CENTER);
        setVisible(true);
    }

    private void createMenuBar(final BanzaiApplication application) {
        JMenuBar menubar = new JMenuBar();

        JMenu sessionMenu = new JMenu("Session");
        menubar.add(sessionMenu);

        JMenuItem logonItem = new JMenuItem("Logon");
        logonItem.addActionListener(e -> Banzai.get().logon());
        sessionMenu.add(logonItem);

        JMenuItem logoffItem = new JMenuItem("Logoff");
        logoffItem.addActionListener(e -> Banzai.get().logout());
        sessionMenu.add(logoffItem);

        JMenu appMenu = new JMenu("Application");
        menubar.add(appMenu);

        JMenuItem appAvailableItem = new JCheckBoxMenuItem("Available");
        appAvailableItem.setSelected(application.isAvailable());
        appAvailableItem.addActionListener(e -> application.setAvailable(((JCheckBoxMenuItem) e.getSource()).isSelected()));
        appMenu.add(appAvailableItem);

        JMenuItem sendMissingFieldRejectItem = new JCheckBoxMenuItem("Send Missing Field Reject");
        sendMissingFieldRejectItem.setSelected(application.isMissingField());
        sendMissingFieldRejectItem.addActionListener(e -> application.setMissingField(((JCheckBoxMenuItem) e.getSource()).isSelected()));
        appMenu.add(sendMissingFieldRejectItem);

        setJMenuBar(menubar);
    }
}
