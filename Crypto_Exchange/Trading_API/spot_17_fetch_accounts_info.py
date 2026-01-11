import requests
import json
import hashlib
import hmac
import time

BASE_URL = 'https://futuresopenapi.gaiaex.com'
CONFIG_FILE = 'UID32937591.json'
ACCOUNT_PATH = '/fapi/v1/account'

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
            print("Account information fetched successfully")
            print(json.dumps(data, indent=4))
        else:
            print(f"Failed to fetch account information. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetching account information
    fetch_account_info(api_key, secret_key)
