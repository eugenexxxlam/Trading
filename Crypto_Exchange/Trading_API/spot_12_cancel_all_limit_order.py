import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'UID32751170.json'  # Replace with correct config file

BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
REQUEST_PATH_OPEN_ORDERS = '/sapi/v1/openOrders'  # Request path for getting open orders
REQUEST_PATH_CANCEL_ORDER = '/sapi/v1/order'  # Request path for cancelling an order

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

# Function to fetch all open orders
def get_open_orders(api_key, secret_key):
    full_url = BASE_URL + REQUEST_PATH_OPEN_ORDERS
    timestamp = int(time.time() * 1000)
    params = {
        'timestamp': timestamp
    }
    query_string = '&'.join([f"{key}={value}" for key, value in params.items()])
    signature_payload = f"{query_string}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()
    
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }

    try:
        response = requests.get(full_url, headers=headers, params=params)
        if response.status_code == 200:
            data = response.json()
            if isinstance(data, list):
                return data
            else:
                print(f"Unexpected data format: {data}")
                return []
        else:
            print(f"Failed to fetch open orders. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return []

# Function to cancel an order
def cancel_order(api_key, secret_key, symbol, order_id):
    full_url = BASE_URL + REQUEST_PATH_CANCEL_ORDER
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': symbol,
        'orderId': order_id,
        'timestamp': timestamp
    }
    body = json.dumps(params)
    signature_payload = f"{timestamp}DELETE{REQUEST_PATH_CANCEL_ORDER}{body}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()
    
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.delete(full_url, headers=headers, data=body)
        if response.status_code == 200:
            data = response.json()
            print(f"Order {order_id} cancelled successfully: {data}")
        else:
            print(f"Failed to cancel order {order_id}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while cancelling order {order_id}: {e}")

# Function to cancel all open limit orders
def cancel_all_open_orders(api_key, secret_key):
    open_orders = get_open_orders(api_key, secret_key)
    if open_orders:
        for order in open_orders:
            if order.get('type') == 'LIMIT':
                cancel_order(api_key, secret_key, order['symbol'], order['orderId'])
    else:
        print("No open limit orders found.")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Cancel all open limit orders
    cancel_all_open_orders(api_key, secret_key)
