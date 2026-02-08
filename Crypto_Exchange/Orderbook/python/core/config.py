"""
Configuration for CLOB Aggregator
"""

# ============================================================================
# ORDERBOOK CONTROLS - Tune these parameters to control spread & liquidity
# ============================================================================
CONTROLS = {
    'min_notional': 10000,      # Min USDT per level
    'depth': 16,                # Number of price levels per side to display
    'display_fps': 10,          # Display refresh rate (frames per second)
    'aggregation_hz': 20,       # Aggregation update rate (Hz)
    'bid_markup_bps': 0.8,      # Push BID prices UP to tighten spread
    'ask_markup_bps': 0.1,      # Push ASK prices DOWN to tighten spread
    'spread_floor_bps': 0,      # Minimum spread enforcement (safety net)
    
    # Exchange Weighting System
    'weights': {
        # TIER 1: Price Discovery Leaders
        'binance': 1.5,
        'coinbase': 1.5,
        'kraken': 1.2,
        'bingx': 1.2,
        
        # TIER 2: Reliable Contributors
        'gate': 1.0,
        'kucoin': 1.0,
        'bitget': 1.0,
        
        # TIER 3: Supplementary
        'hyperliquid': 0.6,
        'htx': 0.5,
        
        # TIER 4: Wide Spreads
        'okx': 0.3,
        'bybit': 0.3,
        'dydx': 0.2,
    }
}

# Exchange configuration
EXCHANGES = {
    'binance': {'module': '1_orderbook_binance', 'class': 'BinanceOrderBook'},
    'okx': {'module': '2_orderbook_okx', 'class': 'OKXOrderBook'},
    'bybit': {'module': '3_orderbook_bybit', 'class': 'BybitOrderBook'},
    'gate': {'module': '4_orderbook_gate', 'class': 'GateOrderBook'},
    'kucoin': {'module': '14_orderbook_kucoin', 'class': 'KucoinOrderBook'},
    'bitget': {'module': '5_orderbook_bitget', 'class': 'BitgetOrderBook'},
    'bingx': {'module': '6_orderbook_bingx', 'class': 'BingxOrderBook'},
    'htx': {'module': '8_orderbook_HTX', 'class': 'HTXOrderBook'},
    'hyperliquid': {'module': '9_orderbook_hyperliquid', 'class': 'HyperliquidOrderBook'},
    'dydx': {'module': '10_orderbook_dydx', 'class': 'DydxOrderBook'},
    'coinbase': {'module': '11_orderbook_coinbase', 'class': 'CoinbaseOrderBook'},
    'kraken': {'module': '12_orderbook_kraken', 'class': 'KrakenOrderBook'},
}
