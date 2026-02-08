#!/usr/bin/env python3
"""
Aggregated CLOB - Multi-Exchange Order Book Aggregator

Production-ready orderbook aggregator with modular architecture.
See core/ modules for implementation details.

Usage: python 0_CLOB_MM_display.py
Press 'q' to quit the display.
"""

import time
import curses
from importlib import import_module
from core import CONTROLS, EXCHANGES, CLOBAggregator, run_display


def main():
    """Main entry point"""
    print("=" * 60)
    print("CLOB Aggregator - Multi-Exchange Order Book")
    print("=" * 60)
    print(f"Config: Markup {CONTROLS['bid_markup_bps']}/{CONTROLS['ask_markup_bps']}bps | "
          f"Spread Floor {CONTROLS['spread_floor_bps']}bps | Min ${CONTROLS['min_notional']:,}")
    print("=" * 60)
    
    # Start aggregator
    aggregator = CLOBAggregator(CONTROLS)
    aggregator.start()
    print("✓ Aggregation engine started\n")
    
    # Start exchange feeds
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
    
    print(f"\n✓ Started {len(feeds)} feeds")
    print("\nStarting live display (press 'q' to quit)...\n")
    time.sleep(2)
    
    try:
        curses.wrapper(run_display, aggregator)
    except KeyboardInterrupt:
        pass
    finally:
        print("\n\nShutting down...")
        aggregator.stop()
        for feed in feeds:
            if hasattr(feed, 'stop'):
                feed.stop()


if __name__ == "__main__":
    main()