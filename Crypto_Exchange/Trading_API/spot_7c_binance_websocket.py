import websocket
import json
import time
import threading

# Dictionary to store symbol and bid-ask spread
symbol_spread = {}

def on_message(ws, message):
    data = json.loads(message)
    if "s" in data and "b" in data and "a" in data:
        symbol = data["s"]  # Symbol
        best_bid = float(data["b"])  # Best bid price
        best_ask = float(data["a"])  # Best ask price
        spread = best_ask - best_bid
        symbol_spread[symbol] = spread

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("Closed connection, reconnecting in 1 second...")
    time.sleep(1)
    ws.run_forever()

def on_open(ws):
    params = {
        "method": "SUBSCRIBE",
        "params": [
            "btcusdt@bookTicker"  # Subscribe to BTC/USDT best bid/ask updates
        ],
        "id": 1
    }
    ws.send(json.dumps(params))

# Connect to Binance WebSocket for Spot trading
def run_websocket():
    ws = websocket.WebSocketApp("wss://stream.binance.com:9443/ws",
                                on_message=on_message,
                                on_error=on_error,
                                on_close=on_close)
    ws.on_open = on_open
    ws.run_forever()

def print_spread():
    while True:
        if symbol_spread:
            for symbol, spread in symbol_spread.items():
                print({"symbol": symbol, "bid_ask_spread": spread})
        time.sleep(1)

# Run WebSocket in a separate thread
threading.Thread(target=run_websocket, daemon=True).start()

# Print bid-ask spread every second
print_spread()
