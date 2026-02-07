# CLOB Orderbook Aggregator

## Purpose

Python prototype for aggregating orderbook data from 12 cryptocurrency exchanges. This serves as the **observation layer** for AI trading agents - providing a unified view of market liquidity before implementing the final version in C++.

## What It Does

Connects to 12 exchanges via WebSocket and combines their orderbooks into one aggregated view. Shows you the best bid/ask prices and liquidity across all exchanges in real-time.

## Files

**Exchange Connectors (0-13):**
- Each file connects to one exchange (Binance, OKX, Bybit, Gate, KuCoin, Bitget, BingX, Bitfinex, HTX, Hyperliquid, dYdX, Coinbase, Kraken, Crypto.com)
- Gets real-time orderbook data via WebSocket
- Can run standalone to test individual exchange feeds

**Aggregators:**
- `14_CLOB.py` - Basic version: aggregates orderbooks with simple weighted averaging
- `15_CLOB_MM.py` - Market making version: adds spread control and liquidity management

## How to Use

```bash
# Install dependency
pip install websocket-client

# Test a single exchange
python3 0_orderbook_binance.py

# Run basic aggregator
python3 14_CLOB.py

# Run market making version (with spread controls)
python3 15_CLOB_MM.py
```

## 14_CLOB.py vs 15_CLOB_MM.py

**14_CLOB.py** - Passive mode:
- Combines all exchange orderbooks using weighted volumes
- Filters out small orders (< $10K notional)
- Shows natural market spreads

**15_CLOB_MM.py** - Market making mode:
- Same as above, plus:
- Adjustable bid/ask markup to tighten spreads
- Per-exchange weight control (boost/reduce specific exchanges)
- Minimum spread enforcement

Edit the `CONTROLS` dict in 15_CLOB_MM.py to adjust:
```python
CONTROLS = {
    'bid_markup_bps': 20,      # Push bids up (tighten spread)
    'ask_markup_bps': -20,     # Push asks down (tighten spread)
    'min_notional': 10000,     # Filter orders < $10K
    'weights': {               # Boost/reduce specific exchanges
        'binance': 1.5,
        'coinbase': 1.3,
        # ...
    }
}
```

## Output Format

Both scripts display:
- Aggregated bids (buy orders) and asks (sell orders)
- Price, volume in BTC, notional in USDT
- Cumulative totals
- Liquidity provider (which exchange)

## Next Steps

This is a **prototype**. The plan:
1. Use this to understand market data structure
2. Reimplement in C++ for production speed
3. Feed the aggregated orderbook to AI trading agents as the "observation" of market state

## For AI Trading Agents

The aggregated orderbook provides:
- Current best bid/ask across all venues
- Available liquidity at each price level
- Market depth for impact estimation
- Real-time spread and mid-price

This is what the agent "sees" before deciding to buy/sell.
