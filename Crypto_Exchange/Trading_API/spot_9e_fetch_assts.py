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
            data = response.json()
            return data
        else:
            print(f"Failed to fetch account information. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Convert account info to a dictionary
def account_info_to_dict(account_info):
    if account_info and 'balances' in account_info:
        balances = account_info['balances']
        return {balance['asset']: float(balance['free']) for balance in balances}
    return {}

# Fetch the best bid price for a given asset
def fetch_best_bid(asset):
    # Modify symbol to match the correct trading pair format
    symbol = f"{asset.lower()}usdt{asset[-4:]}"  # Assumes assets end with '1802', so we append 'usdt1802'
    depth_url = f"{BASE_URL}{DEPTH_PATH}"
    params = {'symbol': symbol, 'limit': 1}  # Fetching only the top bid

    try:
        response = requests.get(depth_url, params=params)
        if response.status_code == 200:
            data = response.json()
            if data.get('bids'):
                return float(data['bids'][0][0])  # Return the price of the top bid
            else:
                print(f"No bids found for {symbol}. Response: {data}")
        else:
            print(f"Failed to fetch bid price for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
        return None
    except requests.RequestException as e:
        print(f"Error fetching bid for {symbol}: {e}")
        return None

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetching account information
    account_info = fetch_account_info(api_key, secret_key)

    # Convert account info to a dictionary and print it
    account_dict = account_info_to_dict(account_info)
    print("Account Information Dictionary:")
    print(json.dumps(account_dict, indent=4))

    # Display assets above a certain threshold
    threshold = THRESHOLD
    assets_above_threshold = {asset: balance for asset, balance in account_dict.items() if balance > threshold}
    
    if assets_above_threshold:
        table = PrettyTable()
        table.field_names = ["Asset", "Free Balance", "Bid Price", "Value (USDT)"]

        for asset, balance in assets_above_threshold.items():
            if asset == "USDT1802":
                bid_price = 1.0  # Use 1 for USDT1802
            else:
                bid_price = fetch_best_bid(asset)

            bid_display = f"{bid_price:,.8f}" if bid_price else "N/A"
            value = balance * bid_price if bid_price else 0
            value_display = f"{value:,.8f}" if bid_price else "N/A"

            table.add_row([asset, f"{balance:,.8f}", bid_display, value_display])

        print("\nAssets Above Threshold:")
        print(table)
    else:
        print("\nNo assets found with free balance above the threshold.")
