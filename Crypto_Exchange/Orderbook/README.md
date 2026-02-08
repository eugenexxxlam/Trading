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

**Example Output:**
```
======================================================================
                   AGGREGATED CLOB - Multi-Exchange
                       GMT: 2026-02-08 16:46:46
                      GMT+8: 2026-02-09 00:46:46
           Spread: $1.33  |  Mid: $70,705.04  |  FPS: 139.9           
======================================================================

     EXCHANGE STATUS - Confidence Weighting (12/12 high quality)      
----------------------------------------------------------------------
Exchange      Confidence  Weight     Spread     Age      Quality
----------------------------------------------------------------------
binance            100%    1.50x $    0.01    0.0s    EXCELLENT
okx                100%    0.30x $   11.90    0.0s    EXCELLENT
bybit              100%    0.30x $   39.40    2.2s    EXCELLENT
gate               100%    1.00x $    0.10    0.1s    EXCELLENT
kucoin             100%    1.00x $    0.10    0.1s    EXCELLENT
bitget             100%    1.00x $    6.45    0.1s    EXCELLENT
bingx              100%    1.20x $    0.02    0.7s    EXCELLENT
htx                100%    0.50x $    2.99    0.7s    EXCELLENT
hyperliquid        100%    0.60x $    1.00    0.5s    EXCELLENT
dydx               100%    0.20x $   20.00    2.4s    EXCELLENT
coinbase           100%    1.50x $    0.01    0.3s    EXCELLENT
kraken             100%    1.20x $    0.10    0.5s    EXCELLENT
----------------------------------------------------------------------

                          ASKS (Sell Orders)
      Price         BTC       USDT     Cum BTC   Cum USDT  LP
----------------------------------------------------------------------
  70,721.75   2.3847900    168,657      2.3848    168,657  binance 
  70,717.82   0.9172110     64,863      3.3020    233,520  coinbase
  70,717.67   0.4242264     30,000      3.7262    263,520  coinbase
  70,715.22   0.1743000     12,326      3.9005    275,846  coinbase
  70,714.89   0.1909950     13,506      4.0915    289,352  coinbase
  70,713.71   0.6336300     44,806      4.7252    334,158  hyperliquid
  70,712.71   5.2222260    369,278      9.9474    703,436  hyperliquid
  70,712.04   0.4242602     30,000     10.3716    733,436  coinbase

                          BIDS (Buy Orders)
      Price         BTC       USDT     Cum BTC   Cum USDT  LP
----------------------------------------------------------------------
  70,704.38   0.2260125     15,980      0.2260     15,980  htx     
  70,702.44   0.1448485     10,241      0.3709     26,221  htx     
  70,702.43   0.3077625     21,760      0.6786     47,981  htx     
  70,702.42   0.5288315     37,390      1.2075     85,370  htx     
  70,702.37   5.6922204    402,453      6.8997    487,824  bingx   
  70,702.04   3.3027216    233,509     10.2024    721,333  bingx   
  70,701.50   0.2475860     17,505     10.4500    738,838  htx     
  70,696.66   0.4868280     34,417     10.9368    773,255  hyperliquid
======================================================================
```

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
