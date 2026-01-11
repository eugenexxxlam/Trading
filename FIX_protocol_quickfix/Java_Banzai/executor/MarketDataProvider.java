package quickfix.examples.executor;

/**
 * Trivial market data provider interface to allow plugins for
 * alternative market data sources.
 */
public interface MarketDataProvider {

    double getBid(String symbol);

    double getAsk(String symbol);
}
