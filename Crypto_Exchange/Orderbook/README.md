# Multi-Exchange CLOB Aggregator

Real-time orderbook aggregation from 12 cryptocurrency exchanges (Binance, Coinbase, OKX, Bybit, Gate, KuCoin, Bitget, BingX, Kraken, HTX, Hyperliquid, dYdX). Provides unified market depth view for algorithmic trading systems.

## Architecture

**Python Implementation** (Production)
- Modular design with `core/` package (config, aggregator, display)
- Lock-free queues for multi-exchange feed handling
- Confidence-weighted aggregation with age-based decay
- Configurable spread controls and liquidity filtering

**C++ Implementation** (High-Performance)
- Ultra-low latency aggregation engine
- Lock-free data structures for concurrent updates
- Optimized for sub-millisecond processing

## Observe CLOB

### Python (Live Display)
```bash
python3 python/0_CLOB_MM_display.py
```
Real-time terminal UI showing:
- Aggregated bids/asks with exchange attribution
- Per-exchange confidence scores and spread metrics
- Cumulative liquidity depth and FPS counter

### C++ (Production Engine)
```bash
cd cpp/build
./clob_aggregator_12x
```
High-performance aggregation with JSON output for integration.

## Configuration

Edit `python/core/config.py`:
```python
CONTROLS = {
    'min_notional': 10000,      # Minimum order size (USDT)
    'depth': 16,                # Price levels per side
    'bid_markup_bps': 0.8,      # Spread tightening (basis points)
    'ask_markup_bps': 0.1,
    'weights': {                # Exchange priority weighting
        'binance': 1.5,         # Tier 1: Price discovery
        'coinbase': 1.5,
        'gate': 1.0,            # Tier 2: Reliable
        'okx': 0.3,             # Tier 4: Wide spreads
    }
}
```

## Key Features

- **Confidence Weighting**: Age-based decay (100% @ 0-3s → 5% @ 15s+)
- **Spread Management**: Configurable bid/ask markup in basis points
- **Liquidity Filtering**: Minimum notional threshold per level
- **Exchange Tiering**: 4-tier weight system based on spread quality
- **Lock-Free Design**: No thread contention across 12 concurrent feeds

## Dependencies

```bash
pip install websocket-client
```

## Output

Aggregated orderbook provides:
- Best bid/ask across all venues
- Price, volume (BTC), notional (USDT)
- Cumulative depth and liquidity provider attribution
- Real-time spread, mid-price, and market quality metrics

Built for AI trading agents requiring unified market observation.
