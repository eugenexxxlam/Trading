import requests
import json
import hashlib
import hmac
import time
import random
from prettytable import PrettyTable

# ================================
# Constants
# ================================
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH = '/sapi/v1/order'
RANDOM_TRADE_COUNT = 99999999  # Still keep this for possible extension
TOTAL_VOLUME = 200  # Target total amount per trade
ORDER_COUNT = 3  # Number of market orders per asset
LOOP_INTERVAL = 30  # Wait time between full buy cycles (seconds)
STD_DEV_PERCENTAGE = 0.8  # 10% standard deviation for random volume

# ================================
# Functions
# ================================
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
    mean_volume = TOTAL_VOLUME
    min_volume = mean_volume * 0.95
    max_volume = mean_volume * 1.05
    base_volume = random.uniform(min_volume, max_volume)

    # ✅ New: apply random multiplier to each coin's volume
    multiplier = random.uniform(0.2, 2.0)  # between 0.2x and 2.0x
    final_volume = base_volume * multiplier

    # ✅ Round to nearest integer
    final_volume = round(final_volume)

    if final_volume <= 0:
        print(f"Warning: generated volume {final_volume} is invalid. Using fallback mean volume.")
        final_volume = round(mean_volume)

    url = f"{BASE_URL}{REQUEST_PATH}"

    for i in range(ORDER_COUNT):
        timestamp = int(time.time() * 1000)
        params = json.dumps({
            'symbolName': symbol_name,
            'volume': final_volume,
            'side': 'BUY',
            'type': 'MARKET',
            'timestamp': timestamp,
            'recvWindow': 5000
        })
        headers = create_headers(api_key, secret_key, timestamp, params)

        try:
            print(f"Placing order {i + 1} for {symbol_name} with volume: {final_volume} (multiplier: {multiplier:.2f})")
            response = requests.post(url, headers=headers, data=params)
            if response.status_code == 200:
                print(f"Order {i + 1} placed successfully: {response.json()}")
            else:
                print(f"Failed to place order {i + 1}: {response.text}")
        except requests.RequestException as e:
            print(f"Error placing order {i + 1}: {e}")
        time.sleep(1)  # Avoid rate limits

# ================================
# Main Program
# ================================
def main():
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetch tradable symbols once at the beginning
    tradable_list = fetch_tradable_symbols()
    if not tradable_list:
        print("No tradable symbols found. Exiting.")
        exit()

    print(f"\nTotal tradable assets found: {len(tradable_list)}")

    # Main infinite loop
    while True:
        print(f"\nStarting a new buy cycle...")
        for symbol_name in tradable_list:
            print(f"Buying asset: {symbol_name}")
            place_market_orders(api_key, secret_key, symbol_name)

        print(f"\nCompleted one full buy cycle. Waiting for {LOOP_INTERVAL} seconds...")
        time.sleep(LOOP_INTERVAL)

if __name__ == '__main__':
    main()
