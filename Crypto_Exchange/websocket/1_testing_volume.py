import websocket
import gzip
import json
import threading

def on_message(ws, message):
    # Decompress the Gzip-compressed message
    decompressed_data = gzip.decompress(message).decode('utf-8')
    data = json.loads(decompressed_data)
    
    # Handle ping messages
    if 'ping' in data:
        pong_response = json.dumps({'pong': data['ping']})
        ws.send(pong_response)
        print(f"Sent pong: {pong_response}")
    else:
        print(f"Received data: {data}")

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("Connection closed")

def on_open(ws):
    print("Connection opened")
    # Subscribe to BTC/USDT trade ticker
    subscription_message = {
        "event": "sub",
        "params": {
            "channel": "market_btcusdt_trade_ticker",
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
