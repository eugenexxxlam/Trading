import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
SYMBOL_NAME = 'btc/USDT'  # Updated symbol name for the currency pair
TOTAL_VOLUME = 4.88  # Total amount to buy/sell
SIDE = 'SELL'  # BUY or SELL
ORDER_TYPE = 'MARKET'  # Order type
ORDER_COUNT = 5  # Number of market orders to be placed

def load_config(config_file):
    """Load API configuration from a JSON file."""
    with open(config_file, 'r') as file:
        return json.load(file)

def place_spot_market_order(api_key, secret_key, symbol_name, volume, side, order_type='MARKET', recv_window=5000):
    """Place a spot market order using the API."""
    full_url = BASE_URL + REQUEST_PATH
    print(full_url)
    timestamp = int(time.time() * 1000)
    
    params = {
        'symbolName': symbol_name,  # Use symbolName with slash
        'volume': volume,
        'side': side,
        'type': order_type,
        'timestamp': timestamp,
        'recvWindow': recv_window
    }
    body = json.dumps(params)

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
        print(f"Request Headers: {headers}")
        print(f"Request Body: {body}")  # Log request body for debugging
        response = requests.post(full_url, headers=headers, data=body)
        if response.status_code == 200:
            data = response.json()
            print(f"Spot market order placed successfully: {data}")
        else:
            print(f"Failed to place spot market order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Calculate the volume for each order
    per_order_volume = TOTAL_VOLUME / ORDER_COUNT

    # Loop to place multiple market orders
    for i in range(ORDER_COUNT):
        print(f"Placing order {i + 1} of {ORDER_COUNT}")
        place_spot_market_order(api_key, secret_key, symbol_name=SYMBOL_NAME, volume=per_order_volume, side=SIDE, order_type=ORDER_TYPE)
        time.sleep(1)  # Optional delay between orders to avoid rate limit issues
