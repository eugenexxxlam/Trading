import requests
import json
import hashlib
import hmac
import time
import random
from prettytable import PrettyTable

# Constants
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH = '/sapi/v1/order'
RANDOM_TRADE_COUNT = 500
TOTAL_VOLUME = 1000  # Total amount to buy/sell
ORDER_COUNT = 1  # Number of market orders to be placed

def load_config(file):
    with open(file, 'r') as f:
        return json.load(f)

def generate_signature(secret_key, payload):
    return hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()

def create_headers(api_key, secret_key, timestamp, body):
    signature = generate_signature(secret_key, f"{timestamp}POST{REQUEST_PATH}{body}")
    return {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

def fetch_account_info(api_key, secret_key):
    timestamp = int(time.time() * 1000)
    payload = f"{timestamp}GET/sapi/v1/account"
    headers = create_headers(api_key, secret_key, timestamp, "")
    response = requests.get(f"{BASE_URL}/sapi/v1/account", headers=headers)
    return response.json() if response.status_code == 200 else {}

def fetch_tradable_symbols():
    response = requests.get(f"{BASE_URL}/sapi/v1/symbols")
    if response.status_code != 200:
        print("Failed to fetch pairs list:", response.text)
        return []
    return [
        f"{s['baseAsset']}/{s['quoteAsset']}" for s in response.json().get('symbols', [])
        if s['quoteAsset'].upper() == 'USDT'
    ]

def place_market_orders(api_key, secret_key, symbol_name):
    per_order_volume = TOTAL_VOLUME / ORDER_COUNT
    url = f"{BASE_URL}{REQUEST_PATH}"

    for i in range(ORDER_COUNT):
        timestamp = int(time.time() * 1000)
        params = json.dumps({
            'symbolName': symbol_name,
            'volume': per_order_volume,
            'side': 'BUY',
            'type': 'MARKET',
            'timestamp': timestamp,
            'recvWindow': 5000
        })
        headers = create_headers(api_key, secret_key, timestamp, params)

        try:
            response = requests.post(url, headers=headers, data=params)
            if response.status_code == 200:
                print(f"Order {i + 1} placed successfully: {response.json()}")
            else:
                print(f"Failed to place order {i + 1}: {response.text}")
        except requests.RequestException as e:
            print(f"Error placing order {i + 1}: {e}")
        time.sleep(1)  # To avoid rate limits

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetch tradable symbols
    tradable_list = fetch_tradable_symbols()
    if not tradable_list:
        print("No tradable symbols found. Exiting.")
        exit()

    # Execute random trades
    for _ in range(RANDOM_TRADE_COUNT):
        symbol_name = random.choice(tradable_list)
        print(f"\nRandomly selected symbol for trade: {symbol_name}")
        place_market_orders(api_key, secret_key, symbol_name)

