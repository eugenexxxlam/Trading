import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'config_gaiaex_testing2.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for API
CANCEL_ORDER_PATH = '/fapi/v1/cancel'  # Request path for cancelling an order
CONTRACT_NAME = 'USDT1802-BTC-USDT'  # Example contract name
ORDER_ID = '2451502719155555374'  # Example order ID

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

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
            print(f"Order cancelled successfully: {data}")
        else:
            print(f"Failed to cancel order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Example order cancellation
    cancel_order(api_key, secret_key, CONTRACT_NAME, ORDER_ID)
