import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for API
REQUEST_PATH = '/fapi/v1/order'  # Request path for placing a new order
CONTRACT_NAME = 'USDT-BTC-USDT'
VOLUME = 1
SIDE = 'SELL'
PRICE = 102500  # Limit order price
OPEN_POSITION = 'OPEN'
POSITION_TYPE = 1
RECV_WINDOW = 5000
ORDER_COUNT = 10  # Number of orders to be placed

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def place_limit_order(api_key, secret_key, contract_name, volume, price, side, open_position='OPEN', position_type=1, recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'volume': volume,
        'side': side,
        'type': 'LIMIT',
        'price': price,
        'open': open_position,
        'positionType': position_type,
        'timestamp': timestamp
    }
    body = json.dumps(params)  # Body for the POST request

    # Creating the signature based on the documentation
    signature_payload = f"{timestamp}POST{REQUEST_PATH}{body}".encode('utf-8')
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
            print(f"Limit order placed successfully: {data}")
        else:
            print(f"Failed to place limit order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Loop to place multiple limit orders
    for i in range(ORDER_COUNT):
        print(f"Placing order {i + 1} of {ORDER_COUNT}")
        place_limit_order(api_key, secret_key, contract_name=CONTRACT_NAME, volume=VOLUME, price=PRICE, side=SIDE, open_position=OPEN_POSITION, position_type=POSITION_TYPE, recv_window=RECV_WINDOW)
        time.sleep(1)  # Optional delay between orders to avoid rate limit issues