import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
CONFIG_FILE = 'UID32937591.json'
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
THRESHOLD = 1

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
            return response.json()
        else:
            print(f"Failed to fetch account information. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Fetch symbol precision details and map by base asset
def fetch_pairs_precision():
    url = BASE_URL + SYMBOLS_PATH
    response = requests.get(url)
    precision_data = {}
    asset_to_symbol_map = {}

    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        for symbol in symbols:
            precision_data[symbol['symbol'].lower()] = {
                'pricePrecision': symbol['pricePrecision'],
                'quantityPrecision': symbol['quantityPrecision']
            }
            asset_to_symbol_map[symbol['baseAsset'].lower()] = {
                'pricePrecision': symbol['pricePrecision'],
                'quantityPrecision': symbol['quantityPrecision']
            }
    else:
        print("Failed to fetch pairs list:", response.text)

    return precision_data, asset_to_symbol_map

# Fetch the best bid price for a given asset
def fetch_best_bid(asset):
    symbol = f"{asset.lower()}usdt1802"
    depth_url = f"{BASE_URL}{DEPTH_PATH}"
    params = {'symbol': symbol, 'limit': 1}

    try:
        response = requests.get(depth_url, params=params)
        if response.status_code == 200:
            data = response.json()
            if data.get('bids'):
                return float(data['bids'][0][0])
            else:
                print(f"No bids found for {symbol}. Response: {data}")
        else:
            print(f"Failed to fetch bid price for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
        return None
    except requests.RequestException as e:
        print(f"Error fetching bid for {symbol}: {e}")
        return None

# Login function
def login():
    config = load_config(CONFIG_FILE)
    return config['GAIAEX_API_KEY'], config['GAIAEX_SECRET_KEY']

def balance_snapshot(api_key, secret_key):
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    precision_data, asset_to_symbol_map = fetch_pairs_precision()

    account_dict = {balance['asset']: float(balance['free']) for balance in account_info.get('balances', [])}

    rows = []
    for asset, balance in account_dict.items():
        symbol = f"{asset.lower()}usdt1802"
        base_asset = asset[:-4]
        bid_price = 1.0 if asset == "USDT1802" else fetch_best_bid(asset)
        value = balance * bid_price if bid_price else 0

        precision = precision_data.get(symbol, asset_to_symbol_map.get(base_asset.lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))

        meets_threshold = "Yes" if balance > THRESHOLD and asset.endswith("1802") else "No"

        # Only include rows where the value is greater than 0.1 USDT
        if value > 0.1:
            rows.append([asset, symbol, base_asset, balance, bid_price, value, precision['pricePrecision'], precision['quantityPrecision'], meets_threshold])

    # Sort rows by Value (USDT) in descending order
    rows.sort(key=lambda x: x[5], reverse=True)

    # Create PrettyTable
    table = PrettyTable()
    table.field_names = ["Asset", "Symbol", "Base Asset", "Free Balance", "Bid Price", "Value (USDT)", "Price Precision", "Quantity Precision", "Meets Threshold"]

    for row in rows:
        asset, symbol, base_asset, balance, bid_price, value, price_precision, quantity_precision, meets_threshold = row
        bid_display = f"{bid_price:,.8f}" if bid_price else "N/A"
        value_display = f"{value:,.8f}" if bid_price else "N/A"
        table.add_row([asset, symbol, base_asset, f"{balance:,.8f}", bid_display, value_display, price_precision, quantity_precision, meets_threshold])

    print("\nAssets with Value > 0.1 USDT (Sorted by Value):")
    print(table)


# Main execution
if __name__ == '__main__':
    api_key, secret_key = login()
    balance_snapshot(api_key, secret_key)
