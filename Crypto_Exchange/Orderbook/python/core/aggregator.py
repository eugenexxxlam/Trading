"""
Central Limit Order Book Aggregator
"""
import time
from threading import Thread
from collections import defaultdict
from .config import CONTROLS, EXCHANGES
from .exchange_queue import ExchangeQueue


def calculate_confidence(age_seconds):
    """
    Calculate confidence score based on data age.
    
    - 0-3s:   100% confidence (excellent)
    - 3-8s:   Linear decay 100% → 40% (good)
    - 8-15s:  Linear decay 40% → 10% (aging)
    - 15s+:   5% confidence (stale)
    
    Returns: float 0.05-1.0
    """
    if age_seconds < 3:
        return 1.0
    elif age_seconds < 8:
        return 1.0 - (age_seconds - 3) * 0.12
    elif age_seconds < 15:
        return 0.4 - (age_seconds - 8) * 0.0429
    else:
        return 0.05


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
        self.exchange_status = {}
        self.last_warning = {}
    
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
        """Main aggregation loop"""
        latest = {}
        stale_threshold = 20.0
        
        while self.running:
            now = time.time()
            
            # Drain all queues
            for ex, q in self.queues.items():
                update = q.pop()
                if update:
                    latest[ex] = update
            
            # Filter stale data
            active_updates = {
                ex: book for ex, book in latest.items()
                if now - book['time'] < stale_threshold
            }
            
            self._rebuild_clob(active_updates)
            time.sleep(1.0 / self.cfg['aggregation_hz'])
    
    def _rebuild_clob(self, updates):
        """Aggregate order books with markup and filtering"""
        all_bids, all_asks = [], []
        
        bid_mult = 1 + self.cfg['bid_markup_bps'] / 10000
        ask_mult = 1 + self.cfg['ask_markup_bps'] / 10000
        min_notional = self.cfg['min_notional']
        weights = self.cfg['weights']
        
        now = time.time()
        
        # Track exchange status
        for ex, book in updates.items():
            if book['bids'] and book['asks']:
                age = now - book['time']
                confidence = calculate_confidence(age)
                
                if age > 10.0 and (ex not in self.last_warning or now - self.last_warning.get(ex, 0) > 60):
                    self.last_warning[ex] = now
                
                self.exchange_status[ex] = {
                    'connected': True,
                    'last_update': book['time'],
                    'age': age,
                    'confidence': confidence,
                    'best_bid': book['bids'][0][0],
                    'best_ask': book['asks'][0][0],
                    'spread': book['asks'][0][0] - book['bids'][0][0],
                }
        
        # Collect all levels with confidence weighting
        for ex, book in updates.items():
            base_weight = weights.get(ex, 1.0)
            confidence = self.exchange_status[ex].get('confidence', 0.1)
            effective_weight = base_weight * confidence
            
            for p, v in book['bids']:
                p_adj = p * bid_mult
                v_weighted = v * effective_weight
                if p_adj * v_weighted >= min_notional:
                    all_bids.append((p_adj, v_weighted, ex))
            
            for p, v in book['asks']:
                p_adj = p * ask_mult
                v_weighted = v * effective_weight
                if p_adj * v_weighted >= min_notional:
                    all_asks.append((p_adj, v_weighted, ex))
        
        # Sort by price
        all_bids.sort(key=lambda x: x[0], reverse=True)
        all_asks.sort(key=lambda x: x[0])
        
        # Enforce spread floor & remove crossed levels
        if all_bids and all_asks:
            best_bid, best_ask = all_bids[0][0], all_asks[0][0]
            spread_bps = (best_ask / best_bid - 1) * 10000
            
            if spread_bps < self.cfg['spread_floor_bps']:
                mid = (best_bid + best_ask) / 2
                half_spread = mid * self.cfg['spread_floor_bps'] / 10000 / 2
                all_bids = [(p, v, lp) for p, v, lp in all_bids if p <= mid - half_spread]
                all_asks = [(p, v, lp) for p, v, lp in all_asks if p >= mid + half_spread]
            else:
                all_bids = [(p, v, lp) for p, v, lp in all_bids if p < best_ask]
                all_asks = [(p, v, lp) for p, v, lp in all_asks if p > best_bid]
        
        # Take top N levels and pad
        self.merged_bids = all_bids[:self.depth]
        self.merged_asks = all_asks[:self.depth]
        self.merged_bids += [(0.0, 0.0, '')] * (self.depth - len(self.merged_bids))
        self.merged_asks += [(0.0, 0.0, '')] * (self.depth - len(self.merged_asks))
    
    def get_orderbook(self):
        """Get current aggregated orderbook"""
        return self.merged_bids, self.merged_asks
    
    def get_exchange_status(self):
        """Get per-exchange status"""
        return self.exchange_status
