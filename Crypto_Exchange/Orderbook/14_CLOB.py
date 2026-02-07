#!/usr/bin/env python3
"""Aggregated CLOB - Multi-Exchange Order Book Aggregator"""

import time
from datetime import datetime, timezone, timedelta
from threading import Thread
from queue import Queue
from collections import defaultdict

# Import exchange classes
from importlib import import_module

# Exchange configuration
EXCHANGES = {
    'binance': {'module': '0_orderbook_binance', 'class': 'BinanceOrderBook', 'weight': 1.0},
    'okx': {'module': '1_orderbook_okx', 'class': 'OKXOrderBook', 'weight': 0.9},
    'bybit': {'module': '2_orderbook_bybit', 'class': 'BybitOrderBook', 'weight': 0.85},
    'gate': {'module': '3_orderbook_gate', 'class': 'GateOrderBook', 'weight': 0.75},
    'kucoin': {'module': '4_orderbook_kucoin', 'class': 'KucoinOrderBook', 'weight': 0.7},
    'bitget': {'module': '5_orderbook_bitget', 'class': 'BitgetOrderBook', 'weight': 0.65},
    'bingx': {'module': '6_orderbook_bingx', 'class': 'BingxOrderBook', 'weight': 0.6},
    'htx': {'module': '8_orderbook_HTX', 'class': 'HTXOrderBook', 'weight': 0.5},
    'hyperliquid': {'module': '9_orderbook_hyperliquid', 'class': 'HyperliquidOrderBook', 'weight': 0.7},
    'dydx': {'module': '10_orderbook_dydx', 'class': 'DydxOrderBook', 'weight': 0.65},
    'coinbase': {'module': '11_orderbook_coinbase', 'class': 'CoinbaseOrderBook', 'weight': 0.95},
    'kraken': {'module': '12_orderbook_kraken', 'class': 'KrakenOrderBook', 'weight': 0.8},
}


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
    
    def __init__(self, depth=16):
        self.depth = depth
        self.queues = {ex: ExchangeQueue() for ex in EXCHANGES}
        self.merged_bids = []
        self.merged_asks = []
        self.running = False
        self.stats = defaultdict(int)
    
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
        """Main aggregation loop (10 Hz)"""
        latest = {}
        
        while self.running:
            # Drain all queues
            for ex, q in self.queues.items():
                update = q.pop()
                if update:
                    latest[ex] = update
            
            # Aggregate
            self._rebuild_clob(latest)
            time.sleep(0.1)
    
    def _rebuild_clob(self, updates):
        """Aggregate order books"""
        all_bids = []
        all_asks = []
        
        for ex, book in updates.items():
            weight = EXCHANGES[ex]['weight']
            # Apply weight, then filter on weighted notional >= 10K USDT
            for p, v in book['bids']:
                weighted_v = v * weight
                if p * weighted_v >= 10000:
                    all_bids.append((p, weighted_v, ex))
            
            for p, v in book['asks']:
                weighted_v = v * weight
                if p * weighted_v >= 10000:
                    all_asks.append((p, weighted_v, ex))
        
        # Sort
        all_bids = sorted(all_bids, key=lambda x: x[0], reverse=True)
        all_asks = sorted(all_asks, key=lambda x: x[0])
        
        # Sanitize crossed levels
        if all_bids and all_asks:
            all_bids = [(p, v, lp) for p, v, lp in all_bids if p < all_asks[0][0]]
            all_asks = [(p, v, lp) for p, v, lp in all_asks if p > (all_bids[0][0] if all_bids else 0)]
        
        # Take top 16
        self.merged_bids = all_bids[:self.depth]
        self.merged_asks = all_asks[:self.depth]
        
        # Pad to 16 levels
        self.merged_bids += [(0.0, 0.0, '')] * (self.depth - len(self.merged_bids))
        self.merged_asks += [(0.0, 0.0, '')] * (self.depth - len(self.merged_asks))
    
    def get_orderbook(self):
        return self.merged_bids, self.merged_asks


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
    print(f"{'Active: ' + str(active) + ' exchanges':^70}")
    print(f"{'GMT: ' + now_utc.strftime('%Y-%m-%d %H:%M:%S'):^70}")
    print(f"{'GMT+8: ' + now_gmt8.strftime('%Y-%m-%d %H:%M:%S'):^70}")
    print(f"{'Spread: $' + f'{spread:.2f}' + '  |  Mid: $' + f'{mid:,.2f}':^70}")
    print(f"{'=' * 70}")
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
    
    aggregator = CLOBAggregator(depth=16)
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
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        aggregator.stop()
        for feed in feeds:
            feed.stop() if hasattr(feed, 'stop') else None


if __name__ == "__main__":
    main()