#!/usr/bin/env python3
"""KuCoin Order Book Feed for CLOB Aggregator"""

import json
import requests
import time
from datetime import datetime, timezone, timedelta
from threading import Thread
from typing import List, Tuple
from websocket import create_connection


class KucoinOrderBook:
    """KuCoin WebSocket order book feed"""
    
    def __init__(self, aggregator, symbol="BTC-USDT", depth=20):
        self.aggregator = aggregator
        self.symbol = symbol
        self.depth = depth
        self.running = False
    
    def start(self):
        """Start WebSocket feed in background thread"""
        self.running = True
        Thread(target=self._run, daemon=True, name="kucoin").start()
    
    def stop(self):
        """Stop WebSocket feed"""
        self.running = False
    
    def _run(self):
        """Main WebSocket loop with auto-reconnect"""
        while self.running:
            try:
                # Get WebSocket token from REST API
                response = requests.post("https://api.kucoin.com/api/v1/bullet-public")
                token_data = response.json()["data"]
                url = f"{token_data['instanceServers'][0]['endpoint']}?token={token_data['token']}"
                
                ws = create_connection(url)
                subscribe_msg = {
                    "id": int(datetime.now().timestamp() * 1000),
                    "type": "subscribe",
                    "topic": f"/spotMarket/level2Depth5:{self.symbol}",
                    "response": True
                }
                ws.send(json.dumps(subscribe_msg))
                print(f"✓ KuCoin connected")
                
                while self.running:
                    msg = ws.recv()
                    if not msg or not msg.strip():
                        continue
                    
                    try:
                        data = json.loads(msg)
                    except json.JSONDecodeError:
                        continue
                    
                    if data.get("type") == "ping":
                        ws.send(json.dumps({"id": str(int(datetime.now().timestamp() * 1000)), "type": "pong"}))
                        continue
                    
                    if data.get("type") != "message" or "data" not in data:
                        continue
                    
                    book_data = data["data"]
                    if "asks" not in book_data or "bids" not in book_data:
                        continue
                    
                    bids = sorted(((float(p), float(v)) for p, v in book_data["bids"]), 
                                  key=lambda x: x[0], reverse=True)
                    asks = sorted(((float(p), float(v)) for p, v in book_data["asks"]), 
                                  key=lambda x: x[0])
                    
                    # Sanitize crossed levels
                    if bids and asks:
                        bids = [(p, v) for p, v in bids if p < asks[0][0]]
                        asks = [(p, v) for p, v in asks if p > (bids[0][0] if bids else 0)]
                    
                    if bids and asks:
                        self.aggregator.update_exchange('kucoin', bids, asks)
                    
            except Exception as e:
                if self.running:
                    print(f"KuCoin error: {e}, reconnecting...")
                    time.sleep(2)


if __name__ == "__main__":
    # Standalone test mode
    from itertools import accumulate
    
    class MockAggregator:
        def update_exchange(self, name, bids, asks):
            self.bids, self.asks = bids, asks
    
    agg = MockAggregator()
    feed = KucoinOrderBook(agg)
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
                print(f"{'=' * 70}")
                print(f"{'Exchange: KuCoin':^70}")
                print(f"{'GMT: ' + now_utc.strftime('%Y-%m-%d %H:%M:%S'):^70}")
                print(f"{'GMT+8: ' + now_gmt8.strftime('%Y-%m-%d %H:%M:%S'):^70}")
                print(f"{'Spread: $' + f'{spread:.2f}' + '  |  Mid: $' + f'{mid:,.2f}':^70}")
                print(f"{'=' * 70}")
                print()
                
                print(f"{'ASKS (Sell Orders)':^70}")
                print(f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}")
                print(f"{'-' * 70}")
                
                cum_btc_ask = 0.0
                cum_usdt_ask = 0.0
                for p, v in reversed(asks):
                    if p > 0:
                        usdt = p * v
                        cum_btc_ask += v
                        cum_usdt_ask += usdt
                        print(f"\033[91m{p:>11,.2f}\033[0m  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc_ask:>10,.4f}  {cum_usdt_ask:>9,.0f}")
                    else:
                        print(f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc_ask:>10,.4f}  {cum_usdt_ask:>9,.0f}")
                
                print()
                print(f"{'BIDS (Buy Orders)':^70}")
                print(f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}")
                print(f"{'-' * 70}")
                
                cum_btc_bid = 0.0
                cum_usdt_bid = 0.0
                for p, v in bids:
                    if p > 0:
                        usdt = p * v
                        cum_btc_bid += v
                        cum_usdt_bid += usdt
                        print(f"\033[92m{p:>11,.2f}\033[0m  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc_bid:>10,.4f}  {cum_usdt_bid:>9,.0f}")
                    else:
                        print(f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc_bid:>10,.4f}  {cum_usdt_bid:>9,.0f}")
                
                print(f"{'=' * 70}")
                
    except KeyboardInterrupt:
        feed.stop()
        print("\n✓ Stopped")
