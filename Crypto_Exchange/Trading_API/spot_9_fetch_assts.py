import requests
import json
import hashlib
import hmac
import time

BASE_URL = 'https://openapi.gaiaex.com'
ACCOUNT_PATH = '/sapi/v1/account'
CONFIG_FILE = 'config_gaiaex_testing2.json'

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
            data = response.json()
            return data
        else:
            print(f"Failed to fetch account information. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Get all assets above 10 USDT
def get_assets_above_threshold(account_info, threshold=10):
    if account_info and 'balances' in account_info:
        balances = account_info['balances']
        assets_above_threshold = [balance for balance in balances if float(balance['free']) > threshold]
        
        # Print assets with free balance greater than the threshold
        for asset in assets_above_threshold:
            print(f"Asset: {asset['asset']}, Free: {asset['free']}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Fetching account information
    account_info = fetch_account_info(api_key, secret_key)

    # Get and print assets above 10 USDT
    get_assets_above_threshold(account_info)
