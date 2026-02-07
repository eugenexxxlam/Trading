#!/usr/bin/env python3
"""Kraken Order Book Feed for CLOB Aggregator"""

import json
import time
from datetime import datetime, timezone, timedelta
from threading import Thread
from typing import List, Tuple
from websocket import create_connection


class KrakenOrderBook:
    """Kraken WebSocket order book feed"""
    
    def __init__(self, aggregator, symbol="XBT/USD", depth=20):
        self.aggregator = aggregator
        self.symbol = symbol
        self.depth = depth
        self.running = False
        self.url = "wss://ws.kraken.com"
        self.order_book = {"bids": {}, "asks": {}}
    
    def start(self):
        """Start WebSocket feed in background thread"""
        self.running = True
        Thread(target=self._run, daemon=True, name="kraken").start()
    
    def stop(self):
        """Stop WebSocket feed"""
        self.running = False
    
    def _run(self):
        """Main WebSocket loop with auto-reconnect"""
        while self.running:
            try:
                ws = create_connection(self.url)
                subscribe_msg = {
                    "event": "subscribe",
                    "pair": [self.symbol],
                    "subscription": {"name": "book", "depth": 10}
                }
                ws.send(json.dumps(subscribe_msg))
                print(f"✓ Kraken connected")
                
                snapshot_received = False
                
                while self.running:
                    msg = ws.recv()
                    data = json.loads(msg)
                    
                    if isinstance(data, dict):
                        continue
                    
                    if len(data) < 4:
                        continue
                    
                    book_data = data[1]
                    
                    if "as" in book_data and "bs" in book_data:
                        self.order_book = {"bids": {}, "asks": {}}
                        for price_str, vol_str, _ in book_data["bs"]:
                            self.order_book["bids"][float(price_str)] = float(vol_str)
                        for price_str, vol_str, _ in book_data["as"]:
                            self.order_book["asks"][float(price_str)] = float(vol_str)
                        snapshot_received = True
                    else:
                        if not snapshot_received:
                            continue
                        for side, key in [("asks", "a"), ("bids", "b")]:
                            if key in book_data:
                                for item in book_data[key]:
                                    price, vol = float(item[0]), float(item[1])
                                    if vol == 0:
                                        self.order_book[side].pop(price, None)
                                    else:
                                        self.order_book[side][price] = vol
                    
                    if self.order_book["bids"] and self.order_book["asks"]:
                        bids = sorted(self.order_book["bids"].items(), key=lambda x: x[0], reverse=True)
                        asks = sorted(self.order_book["asks"].items(), key=lambda x: x[0])
                        
                        # Sanitize crossed levels
                        bids = [(p, v) for p, v in bids if p < asks[0][0]]
                        asks = [(p, v) for p, v in asks if p > (bids[0][0] if bids else 0)]
                        
                        if bids and asks:
                            self.aggregator.update_exchange('kraken', bids, asks)
                    
            except Exception as e:
                if self.running:
                    print(f"Kraken error: {e}, reconnecting...")
                    time.sleep(2)


if __name__ == "__main__":
    # Standalone test mode
    from itertools import accumulate
    
    class MockAggregator:
        def update_exchange(self, name, bids, asks):
            self.bids, self.asks = bids, asks
    
    agg = MockAggregator()
    feed = KrakenOrderBook(agg)
    feed.start()
    
    DEPTH = 16
    
    try:
        while True:
            time.sleep(0.5)
            if hasattr(agg, 'bids') and agg.bids and hasattr(agg, 'asks') and agg.asks:
                now_utc = datetime.now(timezone.utc)
                now_gmt8 = now_utc.astimezone(timezone(timedelta(hours=8)))
                
                bids = agg.bids[:DEPTH] + [(0.0, 0.0)] * (DEPTH - len(agg.bids[:DEPTH]))
                asks = agg.asks[:DEPTH] + [(0.0, 0.0)] * (DEPTH - len(agg.asks[:DEPTH]))
                
                spread = asks[0][0] - bids[0][0]
                mid = (bids[0][0] + asks[0][0]) / 2
                
                print(f"\033[2J\033[H")
                print()
                print(f"{'=' * 80}")
                print(f"{'Exchange: Kraken':^80}")
                print(f"{'GMT: ' + now_utc.strftime('%Y-%m-%d %H:%M:%S'):^80}")
                print(f"{'GMT+8: ' + now_gmt8.strftime('%Y-%m-%d %H:%M:%S'):^80}")
                print(f"{'Spread: $' + f'{spread:.2f}' + '  |  Mid: $' + f'{mid:,.2f}':^80}")
                print(f"{'=' * 80}")
                print()
                
                print(f"{'ASKS (Sell Orders)':^100}")
                print(f"{'Price':>14}  {'BTC Vol':>12}  {'USDT':>14}  {'Cum BTC':>12}  {'Cum USDT':>14}")
                print(f"{'-' * 100}")
                
                cum_btc_ask = 0.0
                cum_usdt_ask = 0.0
                for p, v in reversed(asks):
                    if p > 0:
                        usdt = p * v
                        cum_btc_ask += v
                        cum_usdt_ask += usdt
                        print(f"\033[91m{p:>14,.2f}\033[0m  {v:>12,.8f}  {usdt:>14,.2f}  {cum_btc_ask:>12,.4f}  {cum_usdt_ask:>14,.2f}")
                    else:
                        print(f"{'---':>14}  {'---':>12}  {'---':>14}  {cum_btc_ask:>12,.4f}  {cum_usdt_ask:>14,.2f}")
                
                print()
                print(f"{'BIDS (Buy Orders)':^100}")
                print(f"{'Price':>14}  {'BTC Vol':>12}  {'USDT':>14}  {'Cum BTC':>12}  {'Cum USDT':>14}")
                print(f"{'-' * 100}")
                
                cum_btc_bid = 0.0
                cum_usdt_bid = 0.0
                for p, v in bids:
                    if p > 0:
                        usdt = p * v
                        cum_btc_bid += v
                        cum_usdt_bid += usdt
                        print(f"\033[92m{p:>14,.2f}\033[0m  {v:>12,.8f}  {usdt:>14,.2f}  {cum_btc_bid:>12,.4f}  {cum_usdt_bid:>14,.2f}")
                    else:
                        print(f"{'---':>14}  {'---':>12}  {'---':>14}  {cum_btc_bid:>12,.4f}  {cum_usdt_bid:>14,.2f}")
                
                print(f"{'=' * 100}")
                
    except KeyboardInterrupt:
        feed.stop()
        print("\n✓ Stopped")
