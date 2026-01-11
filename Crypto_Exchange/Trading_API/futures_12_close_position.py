import requests
import json
import hashlib
import hmac
import time

CONFIG_FILE = 'UID32937591.json'
# CONFIG_FILE = 'UID32937591.json'

BASE_URL = 'https://futuresopenapi.gaiaex.com'
CLOSE_ORDER_PATH = '/fapi/v1/order'
ACCOUNT_PATH = '/fapi/v1/account'


def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def get_open_positions(api_key, secret_key):
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
            print(f"Failed to fetch open positions. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")
    
    return None

def standardize_contract_name(contract_name):
    # Convert names like 'usdt1802_btcusdt' to 'USDT1802-BTC-USDT'
    return contract_name.replace('_', '-').upper()

def close_position(api_key, secret_key, contract_name, volume, side):
    full_url = BASE_URL + CLOSE_ORDER_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'volume': volume,
        'side': side,
        'type': 'MARKET',  # or 'LIMIT' if you prefer
        'open': 'CLOSE',
        'timestamp': timestamp
    }
    body = json.dumps(params)
    signature_payload = f"{timestamp}POST{CLOSE_ORDER_PATH}{body}".encode('utf-8')
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
            print(f"Position closed successfully: {data}")
        else:
            print(f"Failed to close position. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Retrieve all open positions
    account_data = get_open_positions(api_key, secret_key)
    if account_data and 'account' in account_data:
        for account in account_data['account']:
            if 'positionVos' in account:
                for position in account['positionVos']:
                    if 'positions' in position:
                        for pos_detail in position['positions']:
                            # Debug print to understand the structure
                            print(f"Position detail: {pos_detail}")

                            contract_name = standardize_contract_name(pos_detail.get('config', {}).get('contractName', ''))
                            volume = pos_detail.get('volume')
                            side = 'SELL' if pos_detail.get('side') == 'BUY' else 'BUY'  # Opposite side to close position

                            if contract_name and volume:
                                print(f"Closing position for contract: {contract_name}, volume: {volume}, side: {side}")
                                close_position(api_key, secret_key, contract_name, volume, side)
                            else:
                                print("Missing contract name or volume, skipping...")
