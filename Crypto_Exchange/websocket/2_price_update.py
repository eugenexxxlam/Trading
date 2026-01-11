import websocket
import gzip
import json
import threading

# === User Configuration ===
# Set the trading pair symbol here (e.g., 'btcusdt', 'ethusdt')
SYMBOL = 'e_btc_usdt'
# ==========================

def on_message(ws, message):
    try:
        # Decompress the Gzip-compressed message
        decompressed_data = gzip.decompress(message).decode('utf-8')
        data = json.loads(decompressed_data)

        # Handle ping messages to keep the connection alive
        if 'ping' in data:
            pong_response = json.dumps({'pong': data['ping']})
            ws.send(pong_response)
            print(f"Sent pong: {pong_response}")
        elif 'tick' in data:
            tick = data['tick']
            bids = tick.get('buys', [])
            asks = tick.get('asks', [])

            if bids and asks:
                best_bid = float(bids[0][0])
                best_ask = float(asks[0][0])
                spread = best_ask - best_bid
                print(f"Best Bid: {best_bid}, Best Ask: {best_ask}, Spread: {spread}")
            else:
                print("No bid/ask data available.")
        else:
            print(f"Received data: {data}")
    except Exception as e:
        print(f"Error processing message: {e}")

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("Connection closed")

def on_open(ws):
    print("Connection opened")
    # Subscribe to the specified symbol's depth channel
    subscription_message = {
        "event": "sub",
        "params": {
            "channel": f"market_{SYMBOL}_depth_step0",
            "cb_id": "1"
        }
    }
    ws.send(json.dumps(subscription_message))
    print(f"Sent subscription: {subscription_message}")

if __name__ == "__main__":
    websocket.enableTrace(False)
    ws_app = websocket.WebSocketApp(
        "wss://ws.gaiaex.com/kline-api/ws",
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close
    )

    # Run WebSocket in a separate thread
    wst = threading.Thread(target=ws_app.run_forever)
    wst.daemon = True
    wst.start()

    try:
        while True:
            pass  # Keep main thread alive
    except KeyboardInterrupt:
        ws_app.close()
