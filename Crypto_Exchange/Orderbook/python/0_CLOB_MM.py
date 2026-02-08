#!/usr/bin/env python3
"""
Aggregated CLOB - Multi-Exchange Order Book Aggregator

This system aggregates order books from 12 cryptocurrency exchanges into a single
unified Central Limit Order Book (CLOB). It provides:

1. WEIGHTED AGGREGATION: Each exchange has a configurable weight (0.5-1.5)
   - Higher weight = more influence in the aggregated book
   - Use to boost reliable exchanges, reduce unreliable ones

2. SPREAD TIGHTENING: Adjustable bid/ask markup in basis points
   - Positive bid_markup = push bids UP (tighter)
   - Negative ask_markup = push asks DOWN (tighter)
   - Can create sub-dollar spreads from natural $5-10 spreads

3. LIQUIDITY FILTERING: Minimum notional threshold per level
   - Filters out small/dust orders
   - Adjustable from $1K (very tight) to $10K+ (conservative)

4. LOCK-FREE UPDATES: Each exchange pushes to its own queue
   - No thread contention between 12 exchange feeds
   - Aggregator drains all queues at 10 Hz (100ms loop)

OUTPUT: vector<pair<price, volume>> for easy C++ conversion

Usage: Adjust CONTROLS dict to tune spread and liquidity filtering
"""

import time
from datetime import datetime, timezone, timedelta
from threading import Thread
from queue import Queue
from collections import defaultdict

# Import exchange classes
from importlib import import_module

# ============================================================================
# ORDERBOOK CONTROLS - Tune these parameters to control spread & liquidity
# ============================================================================
CONTROLS = {
    # Liquidity Filter
    'min_notional': 10,      # Min USDT per level (lower = more levels included)
                                # 1000 = very tight, includes tiny orders
                                # 5000 = balanced filtering
                                # 10000+ = only large liquidity
    
    # Output Depth
    'depth': 32,                 # Number of price levels per side to display
                                # 8 = compact view
                                # 16 = standard view
                                # 32+ = deep book view
    
    # Spread Tightening Controls (in basis points, 1 bp = 0.01%)
    'bid_markup_bps': -10,       # Push BID prices UP to tighten spread
                                # Positive = bids move UP (closer to asks)
                                # 0 = no adjustment
                                # 10-20 = aggressive tightening
                                # Example: 69000 * (1 + 20/10000) = 69138
    
    'ask_markup_bps': -10,      # Push ASK prices DOWN to tighten spread
                                # Negative = asks move DOWN (closer to bids)
                                # 0 = no adjustment
                                # -10 to -20 = aggressive tightening
                                # Example: 69150 * (1 - 20/10000) = 69011
    
    'spread_floor_bps': 0,      # Minimum spread enforcement (safety net)
                                # 0 = allow natural spread (can cross)
                                # 1-2 = tight but safe
                                # 5+ = wide safety margin
    
    # Exchange Weighting System - OPTIMIZED FOR MARKET MAKING
    'weights': {                # Professional weight strategy based on spread tightness
                                # Tier 1 (1.2-1.8x): Price discovery leaders (<$0.50 spread)
                                # Tier 2 (0.8-1.1x): Reliable contributors (<$1 spread)
                                # Tier 3 (0.4-0.7x): Supplementary ($1-5 spread)
                                # Tier 4 (0.2-0.3x): Wide spreads (>$10 spread)
        
        # TIER 1: Price Discovery Leaders (tight spreads, high volume)
        'binance': 1.5,         # Global leader, $0.01 spread, deepest book
        'coinbase': 1.5,        # US market leader, $0.01 spread, best US pricing
        'kraken': 1.2,          # EU leader, $0.70 spread, strong European flow
        'bingx': 1.2,           # Consistently $0.01 spread, fast updates
        
        # TIER 2: Reliable Contributors (tight spreads, good quality)
        'gate': 1.0,            # Asian market, $0.10 spread, consistent
        'kucoin': 1.0,          # Popular CEX, $0.10 spread, good depth
        'bitget': 1.0,          # Growing exchange, $0.01 spread, reliable
        
        # TIER 3: Supplementary (moderate spreads, use for depth)
        'hyperliquid': 0.6,     # DEX but tight $1.00 spread, on-chain pricing
        'htx': 0.5,             # $3-5 spread typical, variable quality
        
        # TIER 4: Wide Spreads (reduce influence, often $10+ spreads)
        'okx': 0.3,             # Often $10-20 spreads, less reliable pricing
        'bybit': 0.3,           # Wide spreads $10-20, derivatives-focused
        'dydx': 0.2,            # DEX with $10-20 spreads, minimal influence
    }
}

# QUICK PRESETS - Copy/paste these to try different strategies:
# 
# ZERO-SPREAD (Risky, for market making):
#   'bid_markup_bps': 30, 'ask_markup_bps': -30, 'spread_floor_bps': 0
#
# TIGHT (2-5 dollar spread):
#   'bid_markup_bps': 10, 'ask_markup_bps': -10, 'spread_floor_bps': 1
#
# CONSERVATIVE (natural spreads):
#   'bid_markup_bps': 0, 'ask_markup_bps': 0, 'spread_floor_bps': 5
# ============================================================================

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


def calculate_confidence(age_seconds):
    """
    Calculate confidence score based on data age for fair price modeling.
    
    Optimized decay curve for real-time trading:
    - 0-3s:   100% confidence (excellent, real-time data)
    - 3-8s:   Linear decay 100% → 40% (good, minor staleness)
    - 8-15s:  Linear decay 40% → 10% (aging, significant penalty)
    - 15s+:   5% confidence (stale, minimal influence)
    
    Returns: float 0.05-1.0 representing data quality
    """
    if age_seconds < 3:
        return 1.0  # Full confidence - excellent data
    elif age_seconds < 8:
        # Faster decay: 1.0 → 0.4 over 5 seconds
        return 1.0 - (age_seconds - 3) * 0.12
    elif age_seconds < 15:
        # Aggressive decay: 0.4 → 0.1 over 7 seconds
        return 0.4 - (age_seconds - 8) * 0.0429
    else:
        # Very low confidence for stale data
        return 0.05


class ExchangeQueue:
    """Lock-free queue for exchange updates"""
    def __init__(self):
        self.queue = Queue(maxsize=100)
    
    def push(self, bids, asks):
        try:
            self.queue.put_nowait({'bids': bids, 'asks': asks, 'time': time.time()})
        except:
            pass
    
    def pop(self):
        try:
            return self.queue.get_nowait()
        except:
            return None


class CLOBAggregator:
    """Central Limit Order Book Aggregator"""
    
    def __init__(self, config=CONTROLS):
        self.cfg = config
        self.depth = config['depth']
        self.queues = {ex: ExchangeQueue() for ex in EXCHANGES}
        self.merged_bids = []
        self.merged_asks = []
        self.running = False
        self.stats = defaultdict(int)
        self.exchange_status = {}  # Track per-exchange connection and spread
        self.last_warning = {}  # Track when we last warned about stale exchanges
    
    def update_exchange(self, exchange, bids, asks):
        """Thread-safe update via lock-free queue"""
        if exchange in self.queues:
            self.queues[exchange].push(bids, asks)
            self.stats[f'{exchange}_updates'] += 1
    
    def start(self):
        self.running = True
        Thread(target=self._aggregation_loop, daemon=True).start()
    
    def stop(self):
        self.running = False
    
    def _aggregation_loop(self):
        """Main aggregation loop (optimized for 20 Hz)"""
        latest = {}
        stale_threshold = 20.0  # Drop data older than 20 seconds
        
        while self.running:
            now = time.time()
            
            # Drain all queues
            for ex, q in self.queues.items():
                update = q.pop()
                if update:
                    latest[ex] = update
            
            # Filter out very stale data (optional: helps reduce noise)
            active_updates = {}
            for ex, book in latest.items():
                age = now - book['time']
                if age < stale_threshold:
                    active_updates[ex] = book
            
            # Aggregate with active data only
            self._rebuild_clob(active_updates)
            time.sleep(0.05)  # 20 Hz update rate (faster responsiveness)
    
    def _rebuild_clob(self, updates):
        """Aggregate order books with markup and filtering"""
        all_bids = []
        all_asks = []
        
        # Calculate price adjustment multipliers from basis points
        # Example: bid_markup_bps=20 → bid_mult=1.002 (0.2% increase)
        bid_mult = 1 + self.cfg['bid_markup_bps'] / 10000
        ask_mult = 1 + self.cfg['ask_markup_bps'] / 10000
        min_notional = self.cfg['min_notional']
        weights = self.cfg['weights']
        
        # Step 0: Track per-exchange status with confidence scores
        now = time.time()
        for ex, book in updates.items():
            if book['bids'] and book['asks']:
                age = now - book['time']
                confidence = calculate_confidence(age)
                
                # Warn about stale exchanges (but not too frequently)
                if age > 10.0 and (ex not in self.last_warning or 
                                   now - self.last_warning.get(ex, 0) > 60):
                    # Only warn once per minute per exchange
                    self.last_warning[ex] = now
                    # Warning will be visible in console but won't spam
                
                best_bid = book['bids'][0][0]
                best_ask = book['asks'][0][0]
                self.exchange_status[ex] = {
                    'connected': True,
                    'last_update': book['time'],
                    'age': age,
                    'confidence': confidence,
                    'best_bid': best_bid,
                    'best_ask': best_ask,
                    'spread': best_ask - best_bid,
                }
        
        # Step 1: Collect all levels from all exchanges with confidence weighting
        for ex, book in updates.items():
            base_weight = weights.get(ex, 1.0)
            confidence = self.exchange_status[ex].get('confidence', 0.1)
            
            # Effective weight = base weight * confidence
            # Example: Binance (1.5) with 10s old data (0.75 confidence) = 1.125 effective
            effective_weight = base_weight * confidence
            
            # Process BID side (buy orders)
            for p, v in book['bids']:
                p_adj = p * bid_mult          # Adjust price (push UP if positive)
                v_weighted = v * effective_weight  # Apply effective weight (base * confidence)
                # Filter: Only include if weighted notional >= min threshold
                if p_adj * v_weighted >= min_notional:
                    all_bids.append((p_adj, v_weighted, ex))
            
            # Process ASK side (sell orders)
            for p, v in book['asks']:
                p_adj = p * ask_mult          # Adjust price (push DOWN if negative)
                v_weighted = v * effective_weight  # Apply effective weight (base * confidence)
                if p_adj * v_weighted >= min_notional:
                    all_asks.append((p_adj, v_weighted, ex))
        
        # Step 2: Sort by price
        all_bids = sorted(all_bids, key=lambda x: x[0], reverse=True)  # Highest bid first
        all_asks = sorted(all_asks, key=lambda x: x[0])                # Lowest ask first
        
        # Step 3: Enforce spread floor & remove crossed levels
        if all_bids and all_asks:
            best_bid, best_ask = all_bids[0][0], all_asks[0][0]
            spread_bps = (best_ask / best_bid - 1) * 10000
            
            # If spread is too tight (below floor), widen it artificially
            if spread_bps < self.cfg['spread_floor_bps']:
                mid = (best_bid + best_ask) / 2
                half_spread = mid * self.cfg['spread_floor_bps'] / 10000 / 2
                # Remove levels that violate minimum spread
                all_bids = [(p, v, lp) for p, v, lp in all_bids if p <= mid - half_spread]
                all_asks = [(p, v, lp) for p, v, lp in all_asks if p >= mid + half_spread]
            else:
                # Remove crossed levels (bids >= best ask, asks <= best bid)
                all_bids = [(p, v, lp) for p, v, lp in all_bids if p < best_ask]
                all_asks = [(p, v, lp) for p, v, lp in all_asks if p > best_bid]
        
        # Step 4: Take top N levels (as specified by depth)
        self.merged_bids = all_bids[:self.depth]
        self.merged_asks = all_asks[:self.depth]
        
        # Step 5: Pad with empty levels for consistent output format
        self.merged_bids += [(0.0, 0.0, '')] * (self.depth - len(self.merged_bids))
        self.merged_asks += [(0.0, 0.0, '')] * (self.depth - len(self.merged_asks))
    
    def get_orderbook(self):
        """Get current aggregated orderbook
        Returns: (bids, asks) where each is [(price, volume, exchange), ...]
        """
        return self.merged_bids, self.merged_asks
    
    def get_exchange_status(self):
        """Get per-exchange connection and spread status
        Returns: dict of {exchange: {connected, last_update, spread, ...}}
        """
        return self.exchange_status


def display_exchange_status(aggregator):
    """Display per-exchange confidence scores for fair price modeling"""
    status = aggregator.get_exchange_status()
    now = time.time()
    
    high_quality_count = 0  # Confidence >= 70%
    total_count = len(EXCHANGES)
    
    # Count high-quality exchanges (confidence >= 0.7)
    for ex in EXCHANGES:
        if ex in status:
            confidence = status[ex].get('confidence', 0)
            if confidence >= 0.7:
                high_quality_count += 1
    
    print(f"{'EXCHANGE STATUS - Confidence Weighting (' + str(high_quality_count) + '/' + str(total_count) + ' high quality)':^70}")
    print(f"{'-' * 70}")
    print(f"{'Exchange':<12} {'Confidence':>11} {'Weight':>7} {'Spread':>10} {'Age':>7} {'Quality':>12}")
    print(f"{'-' * 70}")
    
    for ex in EXCHANGES:
        if ex in status:
            ex_status = status[ex]
            age = ex_status.get('age', 0)
            confidence = ex_status.get('confidence', 0)
            spread = ex_status.get('spread', 0)
            base_weight = aggregator.cfg['weights'].get(ex, 1.0)
            effective_weight = base_weight * confidence
            
            # Color coding based on confidence (updated thresholds)
            if confidence >= 0.8:
                color = '\033[92m'  # Green (excellent: 0-3s)
                quality = 'EXCELLENT'
            elif confidence >= 0.4:
                color = '\033[93m'  # Yellow (good: 3-8s)
                quality = 'GOOD'
            elif confidence >= 0.15:
                color = '\033[93m'  # Yellow (aging: 8-13s)
                quality = 'AGING'
            else:
                color = '\033[91m'  # Red (stale: 13s+)
                quality = 'STALE'
            
            print(f"{ex:<12} {color}{confidence*100:>9,.0f}%\033[0m  "
                  f"{effective_weight:>6,.2f}x ${spread:>8,.2f} {age:>6,.1f}s {color}{quality:>12}\033[0m")
        else:
            # Never received data from this exchange
            print(f"{ex:<12} \033[91m{'0%':>11}\033[0m  "
                  f"{'0.00x':>7} {'---':>10} {'---':>7} \033[91m{'DISCONNECTED':>12}\033[0m")
    
    print(f"{'-' * 70}")
    print()


def display_orderbook(aggregator):
    """Display aggregated CLOB"""
    bids, asks = aggregator.get_orderbook()
    
    valid_bids = [(p, v, s) for p, v, s in bids if v > 0]
    valid_asks = [(p, v, s) for p, v, s in asks if v > 0]
    
    if not valid_bids or not valid_asks:
        return
    
    spread = valid_asks[0][0] - valid_bids[0][0]
    mid = (valid_bids[0][0] + valid_asks[0][0]) / 2
    
    now_utc = datetime.now(timezone.utc)
    now_gmt8 = now_utc.astimezone(timezone(timedelta(hours=8)))
    
    active = sum(1 for ex in EXCHANGES if f'{ex}_updates' in aggregator.stats)
    
    print(f"\033[2J\033[H")
    print()
    print(f"{'=' * 70}")
    print(f"{'AGGREGATED CLOB - Multi-Exchange':^70}")
    print(f"{'GMT: ' + now_utc.strftime('%Y-%m-%d %H:%M:%S'):^70}")
    print(f"{'GMT+8: ' + now_gmt8.strftime('%Y-%m-%d %H:%M:%S'):^70}")
    print(f"{'Spread: $' + f'{spread:.2f}' + '  |  Mid: $' + f'{mid:,.2f}':^70}")
    print(f"{'=' * 70}")
    print()
    
    # Display per-exchange status
    display_exchange_status(aggregator)
    print()
    
    print(f"{'ASKS (Sell Orders)':^70}")
    print(f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}  {'LP':<8}")
    print(f"{'-' * 70}")
    
    cum_btc = 0.0
    cum_usdt = 0.0
    for p, v, lp in reversed(asks):
        if p > 0:
            usdt = p * v
            cum_btc += v
            cum_usdt += usdt
            print(f"\033[91m{p:>11,.2f}\033[0m  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {lp:<8}")
        else:
            print(f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {'':8}")
    
    print()
    print(f"{'BIDS (Buy Orders)':^70}")
    print(f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}  {'LP':<8}")
    print(f"{'-' * 70}")
    
    cum_btc = 0.0
    cum_usdt = 0.0
    for p, v, lp in bids:
        if p > 0:
            usdt = p * v
            cum_btc += v
            cum_usdt += usdt
            print(f"\033[92m{p:>11,.2f}\033[0m  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {lp:<8}")
        else:
            print(f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {'':8}")
    
    print(f"{'=' * 70}")


def main():
    """Main aggregation engine"""
    print("=" * 60)
    print("CLOB Aggregator - Multi-Exchange Order Book")
    print("=" * 60)
    print(f"Config: Markup {CONTROLS['bid_markup_bps']}/{CONTROLS['ask_markup_bps']}bps | "
          f"Spread Floor {CONTROLS['spread_floor_bps']}bps | Min ${CONTROLS['min_notional']:,}")
    print("=" * 60)
    
    aggregator = CLOBAggregator(CONTROLS)
    aggregator.start()
    print("✓ Aggregation engine started\n")
    
    feeds = []
    for ex_name, config in EXCHANGES.items():
        try:
            module = import_module(config['module'])
            ExchangeClass = getattr(module, config['class'])
            feed = ExchangeClass(aggregator)
            feed.start()
            feeds.append(feed)
            print(f"  → Started {ex_name}")
        except Exception as e:
            print(f"  ✗ {ex_name}: {e}")
    
    print(f"\n✓ Started {len(feeds)} feeds\n")
    time.sleep(2)
    
    try:
        while True:
            display_orderbook(aggregator)
            time.sleep(0.3)  # Faster display updates (3.3 Hz)
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        aggregator.stop()
        for feed in feeds:
            feed.stop() if hasattr(feed, 'stop') else None


if __name__ == "__main__":
    main()