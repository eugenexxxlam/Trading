import requests
import json
import time
import hashlib
import hmac

CONFIG_FILE = 'config_gaiaex_testing2.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH_ORDER = '/sapi/v1/order'
REQUEST_PATH_SYMBOLS = '/sapi/v1/symbols'
REQUEST_PATH_ACCOUNT = '/sapi/v1/account'
REQUEST_PATH_TICKER_PRICE = '/sapi/v1/ticker/price'
ORDER_TYPE = 'MARKET'
USD_BUFFER = 5  # USD value buffer to leave in each asset


# Load API configuration
def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)


# Fetch the current price for a trading pair
def fetch_current_price(symbol):
    url = f"{BASE_URL}{REQUEST_PATH_TICKER_PRICE}?symbol={symbol}"
    response = requests.get(url)
    if response.status_code == 200:
        return float(response.json().get('price', 0))
    else:
        print(f"Failed to fetch current price for {symbol}: {response.text}")
        return 0


# Place a market order
def place_spot_market_order(api_key, secret_key, symbol, volume, side, order_type='MARKET', recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH_ORDER
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': symbol,
        'volume': volume,
        'side': side,
        'type': order_type,
        'timestamp': timestamp,
        'recvWindow': recv_window
    }
    body = json.dumps(params)

    # Create signature
    signature_payload = f"{timestamp}POST{REQUEST_PATH_ORDER}{body}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.post(full_url, headers=headers, data=body)
        if response.status_code == 200:
            data = response.json()
            print(f"Spot market order placed successfully for {symbol}: {data}")
        else:
            print(f"Failed to place spot market order for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")


if __name__ == '__main__':
    # Load configuration
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Define parameters for the market order
    symbol = 'BTCUSDT'  # Trading pair
    side = 'BUY'  # Use 'SELL' if selling BTC
    volume = 0.001  # Example volume (BTC amount to trade)

    # Fetch the current price
    current_price = fetch_current_price(symbol)

    if current_price > 0:
        print(f"Current price for {symbol} is {current_price}. Placing order...")
        place_spot_market_order(api_key, secret_key, symbol, volume, side, ORDER_TYPE)
    else:
        print(f"Could not fetch a valid price for {symbol}. Order not placed.")
