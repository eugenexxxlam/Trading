import requests
import json
import hashlib
import hmac
import time
import pandas as pd
from prettytable import PrettyTable
from datetime import datetime

# Constants
BASE_URL = 'https://openapi.gaiaex.com'
CONFIG_FILE = 'UID32937591.json'
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
THRESHOLD = 1

# Utility Functions
def load_config(config_file):
    """Load API key and secret key from a configuration file."""
    with open(config_file, 'r') as file:
        return json.load(file)

def generate_signature(secret_key, payload):
    """Generate HMAC SHA256 signature."""
    return hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()

# API Interaction Functions
def fetch_account_info(api_key, secret_key):
    """Fetch account balances."""
    full_url = BASE_URL + ACCOUNT_PATH
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{ACCOUNT_PATH}"
    signature = generate_signature(secret_key, signature_payload)

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

def fetch_pairs_precision():
    """Fetch symbol precision and map by base asset."""
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

def fetch_best_bid(asset):
    """Fetch the best bid price for a given asset."""
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

# Functional Modules
def login():
    """Login and retrieve API keys."""
    config = load_config(CONFIG_FILE)
    return config['GAIAEX_API_KEY'], config['GAIAEX_SECRET_KEY']

def account_balance(api_key, secret_key):
    """Fetch account balance and return a DataFrame of assets above the threshold."""
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    precision_data, asset_to_symbol_map = fetch_pairs_precision()
    account_dict = {balance['asset']: float(balance['free']) for balance in account_info.get('balances', [])}

    assets_above_threshold = [
        {
            "Asset": asset,
            "Symbol": f"{asset.lower()}usdt1802",
            "Base Asset": asset[:-4],
            "Free Balance": balance,
            "Bid Price": fetch_best_bid(asset) if asset != "USDT1802" else 1.0,
        }
        for asset, balance in account_dict.items()
        if balance > THRESHOLD and asset.endswith("1802")
    ]

    for asset_data in assets_above_threshold:
        symbol = asset_data["Symbol"]
        base_asset = asset_data["Base Asset"]
        bid_price = asset_data["Bid Price"]
        free_balance = asset_data["Free Balance"]
        asset_data["Value"] = free_balance * bid_price if bid_price else 0
        precision = precision_data.get(symbol, asset_to_symbol_map.get(base_asset.lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))
        asset_data["Price Precision"] = precision['pricePrecision']
        asset_data["Quantity Precision"] = precision['quantityPrecision']

    df = pd.DataFrame(assets_above_threshold).sort_values(by="Value", ascending=False)
    return df

def print_prettytable(df, title):
    """Print the DataFrame in PrettyTable format with a custom title."""
    df = df.sort_values(by="Value", ascending=False)  # Sort by 'Value' column in descending order
    print(f"\n{title}\n")
    table = PrettyTable()
    table.field_names = df.columns.tolist()
    for _, row in df.iterrows():
        table.add_row(row.tolist())
    print(table)

def trade(df):
    """dummy trade"""

# Main Execution
if __name__ == '__main__':
    api_key, secret_key = login()
    df_assets = account_balance(api_key, secret_key)
    print_prettytable(df_assets, "Assets Above Threshold USD1 (Sorted by Value) before trading")
    trade(df_assets)
    print_prettytable(df_assets, "Assets Above Threshold USD1 (Sorted by Value) after trading")
