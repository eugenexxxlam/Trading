import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
ACCOUNT_PATH = '/sapi/v1/account'
REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
THRESHOLD = 1  # Minimum balance to consider for liquidation
SUFFIX = 'USDT1802'  # Suffix for the trading pair
ORDER_TYPE = 'MARKET'  # Order type

# Load configuration
def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

# Fetch account information
def fetch_account_info(api_key, secret_key):
    full_url = BASE_URL + ACCOUNT_PATH
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{ACCOUNT_PATH}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }

    try:
        response = requests.get(full_url, headers=headers)
        if response.status_code == 200:
            data = response.json()
            return data
        else:
            print(f"Failed to fetch account information. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Fetch symbol precision
def fetch_symbol_precision(api_key, secret_key, base_asset, suffix):
    url = f"{BASE_URL}/sapi/v1/symbols"
    try:
        response = requests.get(url)
        if response.status_code == 200:
            data = response.json()
            symbols = data.get('symbols', [])
            for symbol in symbols:
                if symbol['symbol'] == f"{base_asset}{suffix}":
                    return symbol['quantityPrecision']  # Adjust this key to match the API response
        else:
            print(f"Failed to fetch symbol precision. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while fetching symbol precision: {e}")
    return None

# Get assets above threshold
def get_assets_above_threshold(account_info, threshold):
    if account_info and 'balances' in account_info:
        balances = account_info['balances']
        assets_above_threshold = [balance for balance in balances if float(balance['free']) > threshold]

        # Sort assets by free balance in descending order
        assets_above_threshold.sort(key=lambda x: float(x['free']), reverse=True)

        # Create and print a table of assets with free balance greater than the threshold
        table = PrettyTable()
        table.field_names = ["Asset", "Free Balance"]
        for asset in assets_above_threshold:
            table.add_row([asset['asset'], f"{float(asset['free']):,.8f}"])

        print(table)
        return assets_above_threshold
    else:
        print("No balances found in account information.")
    return []

# Place a spot market sell order
def place_spot_market_order(api_key, secret_key, symbol, volume, side='SELL', order_type='MARKET', recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH
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
            print(f"Spot market sell order placed successfully: {data}")
        else:
            print(f"Failed to place spot market sell order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetch account information
    account_info = fetch_account_info(api_key, secret_key)

    # Get assets above the threshold
    assets_above_threshold = get_assets_above_threshold(account_info, THRESHOLD)

    # Liquidate assets
    for asset in assets_above_threshold:
        asset_name = asset['asset']
        free_balance = float(asset['free'])

        # Skip USDT as it's the quote currency
        if asset_name.endswith('USDT'):
            print(f"Skipping liquidation for {asset_name} as it is the quote currency.")
            continue

        # Fetch precision and adjust volume
        precision = fetch_symbol_precision(api_key, secret_key, asset_name, SUFFIX)
        if precision is not None:
            free_balance = round(free_balance, precision)

        # Construct the correct symbol
        symbol = f"{asset_name}{SUFFIX}"
        print(f"Liquidating {free_balance} of {asset_name} with symbol {symbol}...")
        place_spot_market_order(api_key, secret_key, symbol=symbol, volume=free_balance, side='SELL')