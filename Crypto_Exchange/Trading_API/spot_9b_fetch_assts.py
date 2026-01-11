import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
CONFIG_FILE = 'UID32937591.json'
ACCOUNT_PATH = '/sapi/v1/account'

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

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

def get_assets_above_threshold(account_info, threshold=1):
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

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetching account information
    account_info = fetch_account_info(api_key, secret_key)

    # Get and print assets above 10 USDT
    get_assets_above_threshold(account_info)
