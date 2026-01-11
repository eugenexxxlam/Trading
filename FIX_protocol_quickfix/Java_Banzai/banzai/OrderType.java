package quickfix.examples.banzai;

import java.util.HashMap;
import java.util.Map;

public class OrderType {
    static private final Map<String, OrderType> known = new HashMap<>();
    static public final OrderType MARKET = new OrderType("Market");
    static public final OrderType LIMIT = new OrderType("Limit");
    static public final OrderType STOP = new OrderType("Stop");
    static public final OrderType STOP_LIMIT = new OrderType("Stop Limit");
    private final String name;

    static private final OrderType[] array = { MARKET, LIMIT, STOP, STOP_LIMIT };

    private OrderType(String name) {
        this.name = name;
        synchronized (OrderType.class) {
            known.put(name, this);
        }
    }

    public String getName() {
        return name;
    }

    public String toString() {
        return name;
    }

    static public Object[] toArray() {
        return array;
    }

    public static OrderType parse(String type) throws IllegalArgumentException {
        OrderType result = known.get(type);
        if (result == null) {
            throw new IllegalArgumentException
            ("OrderType: " + type + " is unknown.");
        }
        return result;
    }
}
