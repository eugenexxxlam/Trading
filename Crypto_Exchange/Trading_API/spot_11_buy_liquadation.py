import requests
import json
import time
import hashlib
import hmac

CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH_ORDER = '/sapi/v1/order'
REQUEST_PATH_SYMBOLS = '/sapi/v1/symbols'
REQUEST_PATH_ACCOUNT = '/sapi/v1/account'
ORDER_TYPE = 'MARKET'
SIDE_SELL = 'SELL'
USD_BUFFER = 1  # USD value buffer to leave in each asset

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

# Fetch account balances
def fetch_account_balances(api_key, secret_key):
    full_url = BASE_URL + REQUEST_PATH_ACCOUNT
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{REQUEST_PATH_ACCOUNT}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }

    try:
        response = requests.get(full_url, headers=headers)
        if response.status_code == 200:
            return response.json().get('balances', [])
        else:
            print("Failed to fetch account balances. Status Code:", response.status_code, ", Response:", response.text)
            return []
    except requests.RequestException as e:
        print(f"An error occurred: {e}")
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

    # Create a dictionary with baseAsset as key and the symbol as value for easy lookup
    pairs_dict = {pair['baseAsset'].lower(): pair['symbol'] for pair in pairs_list if pair['quoteAsset'].lower() == 'usdt'}

    # Debug: Print available pairs
    print("Available trading pairs:", pairs_dict)

    # Fetch account balances
    account_balances = fetch_account_balances(api_key, secret_key)

    # Loop through each balance and sell if above USD 5
    for balance in account_balances:
        asset = balance['asset'].lower()
        free_balance = float(balance['free'])

        if free_balance > 0:
            # Find the corresponding trading pair
            symbol = pairs_dict.get(asset)
            if not symbol:
                print(f"No USDT trading pair found for asset {asset}. Skipping...")
                continue

            # Fetch the current price (this should be replaced with a real price-fetching function)
            current_price = 0.5  # Placeholder value, replace this with actual price-fetching logic

            if current_price <= 0:
                print(f"Invalid current price for {symbol}. Skipping...")
                continue

            # Calculate the USD value of the free balance
            usd_value = free_balance * current_price

            # Sell if the value is greater than USD 5
            if usd_value > USD_BUFFER:
                volume = round(free_balance - (USD_BUFFER / current_price), 8)  # Leave USD 5 worth of the asset
                if volume > 0:
                    print(f"Placing sell order for {symbol}: Volume={volume}, Price={current_price}")
                    place_spot_market_order(api_key, secret_key, symbol, volume, SIDE_SELL, ORDER_TYPE)
                    time.sleep(1)  # Sleep to avoid rate limit issues
