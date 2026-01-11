import requests
import json
import time
import hashlib
import hmac

CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH_ORDER = '/sapi/v1/order'
REQUEST_PATH_SYMBOLS = '/sapi/v1/symbols'
TRADE_USD_VALUE = 700  # USD value to trade for each asset
ORDER_TYPE = 'MARKET'
SIDE = 'BUY'  # Can be 'BUY' or 'SELL'

# Load API configuration
def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

# Fetch available trading pairs
def fetch_pairs_list():
    url = BASE_URL + REQUEST_PATH_SYMBOLS
    response = requests.get(url)
    if response.status_code == 200:
        return response.json().get('symbols', [])
    else:
        print("Failed to fetch pairs list:", response.text)
        return []

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

    # Fetch trading pairs
    pairs_list = fetch_pairs_list()

    # Loop through each pair and place an order worth USD 700
    for pair in pairs_list:
        symbol = pair['symbol']
        base_asset = pair['baseAsset']
        quantity_precision = pair['quantityPrecision']

        # Fetch the current price (this should be replaced with a real price-fetching function)
        current_price = 0.5  # Placeholder value, replace this with actual price-fetching logic

        if current_price <= 0:
            print(f"Invalid current price for {symbol}. Skipping...")
            continue

        # Calculate the volume to trade in the base asset
        volume = TRADE_USD_VALUE / current_price
        volume = round(volume, quantity_precision)  # Round volume to the correct precision

        # Place the order
        print(f"Placing order for {symbol}: Volume={volume}, Price={current_price}")
        place_spot_market_order(api_key, secret_key, symbol, volume, SIDE, ORDER_TYPE)
        time.sleep(1)  # Sleep to avoid rate limit issues
