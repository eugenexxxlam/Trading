import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

BASE_URL = 'https://openapi.gaiaex.com'
ACCOUNT_PATH = '/sapi/v1/account'
CONFIG_FILE = 'config_gaiaex_testing2.json'
PRICE_API_URL = 'https://openapi.gaiaex.com/sapi/v1/ticker/price?symbol='
PAIRS_LIST_URL = 'https://openapi.gaiaex.com/sapi/v1/symbols'
BID_ASK_API_URL = 'https://openapi.gaiaex.com/sapi/v1/ticker/bookTicker?symbol='

# Load API credentials from a configuration file
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

# Fetch price of an asset
def fetch_asset_price(asset):
    try:
        symbol = asset.lower() + 'usdt1802'
        response = requests.get(PRICE_API_URL + symbol)
        if response.status_code == 200:
            data = response.json()
            if 'price' in data:
                return float(data['price'])
            else:
                print(f"Price information not available for {asset}. Response: {data}")
        else:
            print(f"Failed to fetch price for {asset}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while fetching price for {asset}: {e}")

    return None

# Fetch bid and ask price of an asset
def fetch_bid_ask(asset):
    try:
        symbol = asset.lower() + 'usdt1802'
        response = requests.get(BID_ASK_API_URL + symbol)
        if response.status_code == 200:
            data = response.json()
            if 'bidPrice' in data and 'askPrice' in data:
                return float(data['bidPrice']), float(data['askPrice'])
            else:
                print(f"Bid/Ask information not available for {asset}. Response: {data}")
        else:
            print(f"Failed to fetch bid/ask for {asset}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while fetching bid/ask for {asset}: {e}")

    return None, None

# Get all assets above 10 USDT
def get_assets_above_threshold(account_info, threshold=10):
    if account_info and 'balances' in account_info:
        balances = account_info['balances']
        assets_above_threshold = []

        for balance in balances:
            free_balance = float(balance['free'])
            if free_balance > threshold:
                price = fetch_asset_price(balance['asset'])
                if price is not None:
                    usdt_value = free_balance * price
                    bid, ask = fetch_bid_ask(balance['asset'])
                    assets_above_threshold.append({
                        'asset': balance['asset'],
                        'free': free_balance,
                        'usdt_value': usdt_value,
                        'bid': bid,
                        'ask': ask
                    })

        # Sort assets by USDT value in descending order
        assets_above_threshold.sort(key=lambda x: x['usdt_value'], reverse=True)

        # Create a PrettyTable and add rows
        table = PrettyTable()
        table.field_names = ["Asset", "Free", "USDT Value", "Bid Price", "Ask Price"]
        for asset in assets_above_threshold:
            asset_name = asset['asset']
            free_amount = f"{asset['free']:,.2f}"
            usdt_value = f"{asset['usdt_value']:,.2f}"
            bid_price = f"{asset['bid']:,.2f}" if asset['bid'] is not None else 'N/A'
            ask_price = f"{asset['ask']:,.2f}" if asset['ask'] is not None else 'N/A'
            table.add_row([asset_name, free_amount, usdt_value, bid_price, ask_price])

        print(table)

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetching account information
    account_info = fetch_account_info(api_key, secret_key)

    # Get and print assets above 10 USDT
    get_assets_above_threshold(account_info)