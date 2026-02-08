"""
Lock-free queue for exchange updates
"""
import time
from queue import Queue


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
