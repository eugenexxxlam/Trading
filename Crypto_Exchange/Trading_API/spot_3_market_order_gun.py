import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
SYMBOL = 'BTC1802USDT1802'  # Updated symbol from the pairs list
VOLUME = 1  # Amount to buy/sell
SIDE = 'BUY'  # BUY or SELL
ORDER_TYPE = 'MARKET'  # Order type
ORDER_COUNT = 50  # Number of market orders to be placed


def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)


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
    
    # Loop to place multiple market orders
    for i in range(ORDER_COUNT):
        print(f"Placing order {i + 1} of {ORDER_COUNT}")
        place_spot_market_order(api_key, secret_key, symbol=SYMBOL, volume=VOLUME, side=SIDE, order_type=ORDER_TYPE)
        time.sleep(1)  # Optional delay between orders to avoid rate limit issues
