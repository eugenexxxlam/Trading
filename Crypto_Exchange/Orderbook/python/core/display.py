"""
Display functions for CLOB Aggregator
"""
import time
import curses
from datetime import datetime, timezone, timedelta
from .config import CONTROLS, EXCHANGES


def display_exchange_status(stdscr, aggregator, row):
    """Display per-exchange confidence scores"""
    status = aggregator.get_exchange_status()
    max_lines = curses.LINES - 1
    
    def safe_addstr(r, c, text, attr=0):
        if r >= max_lines:
            return False
        try:
            stdscr.addstr(r, c, text, attr)
            return True
        except curses.error:
            return False
    
    high_quality_count = sum(1 for ex in EXCHANGES if ex in status and status[ex].get('confidence', 0) >= 0.7)
    total_count = len(EXCHANGES)
    
    if not safe_addstr(row, 0, f"{'EXCHANGE STATUS - Confidence Weighting (' + str(high_quality_count) + '/' + str(total_count) + ' high quality)':^70}", curses.A_BOLD):
        return row
    row += 1
    if not safe_addstr(row, 0, "-" * 70):
        return row
    row += 1
    if not safe_addstr(row, 0, f"{'Exchange':<12} {'Confidence':>11} {'Weight':>7} {'Spread':>10} {'Age':>7} {'Quality':>12}"):
        return row
    row += 1
    if not safe_addstr(row, 0, "-" * 70):
        return row
    row += 1
    
    for ex in EXCHANGES:
        if row >= max_lines:
            break
        
        if ex in status:
            ex_status = status[ex]
            age = ex_status.get('age', 0)
            confidence = ex_status.get('confidence', 0)
            spread = ex_status.get('spread', 0)
            base_weight = aggregator.cfg['weights'].get(ex, 1.0)
            effective_weight = base_weight * confidence
            
            if confidence >= 0.8:
                color_pair, quality = 1, 'EXCELLENT'
            elif confidence >= 0.4:
                color_pair, quality = 2, 'GOOD'
            elif confidence >= 0.15:
                color_pair, quality = 2, 'AGING'
            else:
                color_pair, quality = 3, 'STALE'
            
            line = f"{ex:<12} {confidence*100:>9,.0f}%  {effective_weight:>6,.2f}x ${spread:>8,.2f} {age:>6,.1f}s {quality:>12}"
            if not safe_addstr(row, 0, line, curses.color_pair(color_pair)):
                break
        else:
            line = f"{ex:<12} {'0%':>11}  {'0.00x':>7} {'---':>10} {'---':>7} {'DISCONNECTED':>12}"
            if not safe_addstr(row, 0, line, curses.color_pair(3)):
                break
        row += 1
    
    safe_addstr(row, 0, "-" * 70)
    row += 1
    return row


def display_orderbook(stdscr, aggregator, frame_times):
    """Display aggregated CLOB with curses"""
    start_time = time.time()
    stdscr.clear()
    
    bids, asks = aggregator.get_orderbook()
    valid_bids = [(p, v, s) for p, v, s in bids if v > 0]
    valid_asks = [(p, v, s) for p, v, s in asks if v > 0]
    
    if not valid_bids or not valid_asks:
        stdscr.refresh()
        return
    
    spread = valid_asks[0][0] - valid_bids[0][0]
    mid = (valid_bids[0][0] + valid_asks[0][0]) / 2
    now_utc = datetime.now(timezone.utc)
    now_gmt8 = now_utc.astimezone(timezone(timedelta(hours=8)))
    
    fps = 1.0 / (sum(frame_times) / len(frame_times)) if frame_times else 0
    max_lines = curses.LINES - 1
    row = 0
    
    def safe_addstr(r, c, text, attr=0):
        if r >= max_lines:
            return False
        try:
            stdscr.addstr(r, c, text, attr)
            return True
        except curses.error:
            return False
    
    # Header
    headers = [
        ("=" * 70, curses.A_BOLD),
        (f"{'AGGREGATED CLOB - Multi-Exchange':^70}", curses.A_BOLD),
        (f"{'GMT: ' + now_utc.strftime('%Y-%m-%d %H:%M:%S'):^70}", 0),
        (f"{'GMT+8: ' + now_gmt8.strftime('%Y-%m-%d %H:%M:%S'):^70}", 0),
        (f"{'Spread: $' + f'{spread:.2f}' + '  |  Mid: $' + f'{mid:,.2f}' + f'  |  FPS: {fps:.1f}':^70}", curses.A_BOLD),
        ("=" * 70, curses.A_BOLD),
    ]
    
    for text, attr in headers:
        if not safe_addstr(row, 0, text, attr):
            stdscr.refresh()
            return
        row += 1
    row += 1
    
    # Exchange status
    if row < max_lines:
        row = display_exchange_status(stdscr, aggregator, row)
        if row >= max_lines:
            stdscr.refresh()
            return
        row += 1
    
    # Calculate orderbook space
    remaining_space = max_lines - row - 1
    available_for_data = remaining_space - 8
    if available_for_data < 2:
        stdscr.refresh()
        return
    
    rows_per_side = available_for_data // 2
    max_ask_rows = min(CONTROLS['depth'], rows_per_side)
    max_bid_rows = min(CONTROLS['depth'], rows_per_side)
    
    # Display asks
    ask_headers = [
        (f"{'ASKS (Sell Orders)':^70}", curses.A_BOLD),
        (f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}  {'LP':<8}", 0),
        ("-" * 70, 0),
    ]
    
    for text, attr in ask_headers:
        if not safe_addstr(row, 0, text, attr):
            stdscr.refresh()
            return
        row += 1
    
    cum_btc = cum_usdt = 0.0
    for p, v, lp in list(reversed(asks))[:max_ask_rows]:
        if row >= max_lines:
            break
        if p > 0:
            usdt = p * v
            cum_btc += v
            cum_usdt += usdt
            line = f"{p:>11,.2f}  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {lp:<8}"
            if not safe_addstr(row, 0, line, curses.color_pair(3)):
                break
        else:
            line = f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {'':8}"
            if not safe_addstr(row, 0, line):
                break
        row += 1
    
    row += 1
    
    # Display bids
    bid_headers = [
        (f"{'BIDS (Buy Orders)':^70}", curses.A_BOLD),
        (f"{'Price':>11}  {'BTC':>10}  {'USDT':>9}  {'Cum BTC':>10}  {'Cum USDT':>9}  {'LP':<8}", 0),
        ("-" * 70, 0),
    ]
    
    for text, attr in bid_headers:
        if not safe_addstr(row, 0, text, attr):
            stdscr.refresh()
            return
        row += 1
    
    cum_btc = cum_usdt = 0.0
    for p, v, lp in bids[:max_bid_rows]:
        if row >= max_lines:
            break
        if p > 0:
            usdt = p * v
            cum_btc += v
            cum_usdt += usdt
            line = f"{p:>11,.2f}  {v:>10,.7f}  {usdt:>9,.0f}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {lp:<8}"
            if not safe_addstr(row, 0, line, curses.color_pair(1)):
                break
        else:
            line = f"{'---':>11}  {'---':>10}  {'---':>9}  {cum_btc:>10,.4f}  {cum_usdt:>9,.0f}  {'':8}"
            if not safe_addstr(row, 0, line):
                break
        row += 1
    
    safe_addstr(row, 0, "=" * 70, curses.A_BOLD)
    stdscr.refresh()
    
    # Track FPS
    frame_time = time.time() - start_time
    frame_times.append(frame_time)
    if len(frame_times) > 30:
        frame_times.pop(0)


def run_display(stdscr, aggregator):
    """Run the display loop with curses"""
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_GREEN, -1)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_RED, -1)
    
    stdscr.nodelay(True)
    curses.curs_set(0)
    
    frame_times = []
    
    try:
        while True:
            display_orderbook(stdscr, aggregator, frame_times)
            time.sleep(1.0 / CONTROLS['display_fps'])
            
            try:
                key = stdscr.getch()
                if key in (ord('q'), ord('Q')):
                    break
            except:
                pass
    except KeyboardInterrupt:
        pass
