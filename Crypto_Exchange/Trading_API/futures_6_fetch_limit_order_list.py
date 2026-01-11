import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'config_gaiaex_testing2.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for API
OPEN_ORDERS_PATH = '/fapi/v1/openOrders'  # Request path for getting open orders

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def get_open_orders(api_key, secret_key, contract_name):
    full_url = BASE_URL + OPEN_ORDERS_PATH
    timestamp = int(time.time() * 1000)  # Generate the current timestamp in milliseconds
    params = {
        'contractName': contract_name
    }
    query_string = '&'.join([f"{key}={value}" for key, value in params.items()])
    signature_payload = f"{timestamp}GET{OPEN_ORDERS_PATH}?{query_string}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()
    
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.get(full_url, headers=headers, params=params)
        if response.status_code == 200:
            data = response.json()
            # Printing the entire response to verify its structure
            print("Response JSON:", json.dumps(data, indent=4))
            if isinstance(data, list):
                return data
            elif 'data' in data and data['data']:
                return data['data']
            elif isinstance(data, dict):
                return data  # In case the data is directly returned as a dict
            else:
                return []
        else:
            print(f"Failed to fetch open orders. Status Code: {response.status_code}, Response: {response.text}")
            return []
    except requests.RequestException as e:
        print(f"An error occurred: {e}")
        return []

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetch all open orders
    contract_name = 'USDT1802-BTC-USDT'  # Example contract name
    open_orders = get_open_orders(api_key, secret_key, contract_name=contract_name)
    
    order_ids = []
    if open_orders:
        for order in open_orders:
            if 'orderId' in order:
                order_ids.append(order['orderId'])
        print("Order IDs:", order_ids)
    else:
        print("No open orders found.")
