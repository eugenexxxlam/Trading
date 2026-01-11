import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for API
OPEN_ORDERS_PATH = '/fapi/v1/openOrders'  # Request path for getting open orders
CANCEL_ORDER_PATH = '/fapi/v1/cancel'  # Request path for cancelling an order


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


def cancel_order(api_key, secret_key, contract_name, order_id):
    full_url = BASE_URL + CANCEL_ORDER_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'orderId': order_id
    }
    body = json.dumps(params)
    signature_payload = f"{timestamp}POST{CANCEL_ORDER_PATH}{body}".encode('utf-8')
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
            print(f"Order {order_id} cancelled successfully: {data}")
        else:
            print(f"Failed to cancel order {order_id}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while cancelling order {order_id}: {e}")


if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetch all open orders
    contract_name = 'USDT1802-BTC-USDT'  # Example contract name
    open_orders = get_open_orders(api_key, secret_key, contract_name=contract_name)
    
    if open_orders:
        for order in open_orders:
            if 'orderId' in order:
                cancel_order(api_key, secret_key, contract_name, order['orderId'])
    else:
        print("No open orders found.")
