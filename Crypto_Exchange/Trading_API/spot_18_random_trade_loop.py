import requests
import json
import hashlib
import hmac
import time
import random

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
VOLUME = 10  # Amount to buy/sell
SIDE = 'BUY'  # BUY or SELL
ORDER_TYPE = 'MARKET'  # Order type
ORDER_COUNT = 10  # Number of market orders to be placed
SYMBOL_SUFFIX = '1802USDT1802'  # Updated suffix for the symbol
TRADE_INTERVAL = 30  # Interval in seconds between trades

# Load configuration
def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

# Fetch symbol list and return a random symbol with the suffix
def fetch_random_symbol_with_suffix():
    url = 'https://openapi.gaiaex.com/sapi/v1/symbols'  # URL to fetch pairs list
    response = requests.get(url)

    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        if not symbols:
            raise ValueError("No symbols available from the API.")

        # Pick a random symbol from the list
        random_symbol = random.choice(symbols)
        base_asset = random_symbol['baseAsset']
        quote_asset = random_symbol['quoteAsset']
        modified_symbol = f"{base_asset}{SYMBOL_SUFFIX}"

        print(f"Selected Symbol: {base_asset}{quote_asset}, Modified Symbol: {modified_symbol}")
        return modified_symbol
    else:
        raise ValueError(f"Failed to fetch pairs list: {response.text}")

# Place a spot market order
def place_spot_market_order(api_key, secret_key, symbol, volume, side, order_type='MARKET', recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH
    print(full_url)
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

    # Creating the signature based on the documentation
    signature_payload = f"{timestamp}POST{REQUEST_PATH}{body}".encode('utf-8')
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
            print(f"Spot market order placed successfully: {data}")
        else:
            print(f"Failed to place spot market order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    try:
        while True:
            # Get a random symbol with the suffix
            random_symbol = fetch_random_symbol_with_suffix()

            # Loop to place multiple market orders
            for i in range(ORDER_COUNT):
                print(f"Placing order {i + 1} of {ORDER_COUNT} for symbol {random_symbol}")
                place_spot_market_order(api_key, secret_key, symbol=random_symbol, volume=VOLUME / ORDER_COUNT, side=SIDE, order_type=ORDER_TYPE)
                time.sleep(1)  # Optional delay between orders to avoid rate limit issues

            # Wait for the trade interval before placing the next batch of orders
            time.sleep(TRADE_INTERVAL)
    except ValueError as e:
        print(f"Error: {e}")
