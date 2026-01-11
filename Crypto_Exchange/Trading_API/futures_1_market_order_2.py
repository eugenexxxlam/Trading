import requests
import json
import hashlib
import hmac
import time
import logging

# Setup logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for API
SPOT_BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for spot API
REQUEST_PATH = '/fapi/v1/order'  # Request path for placing a new futures order
SPOT_REQUEST_PATH = '/api/v1/order'  # Request path for placing a new spot order
CONTRACT_NAME = 'USDT-BTC-USDT'
TOTAL_VOLUME = 10000  # Original total volume
PARTS = 10  # Split into 10 parts
SIDE = 'BUY'
OPEN_POSITION = 'OPEN'
POSITION_TYPE = 1
RECV_WINDOW = 5000

# Derived constants
VOLUME_PER_ORDER = TOTAL_VOLUME / PARTS

def load_config(config_file):
    try:
        with open(config_file, 'r') as file:
            return json.load(file)
    except FileNotFoundError:
        logging.error(f"Configuration file '{config_file}' not found.")
        raise
    except json.JSONDecodeError:
        logging.error(f"Error decoding JSON from the configuration file '{config_file}'.")
        raise

def place_market_order(api_key, secret_key, base_url, request_path, contract_name, volume, side, recv_window=5000):
    full_url = base_url + request_path
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': contract_name,
        'volume': volume,
        'side': side,
        'type': 'MARKET',
        'timestamp': timestamp
    }
    body = json.dumps(params)  # Body for the POST request

    # Creating the signature based on the documentation
    signature_payload = f"{timestamp}POST{request_path}{body}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()
    
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.post(full_url, headers=headers, data=body)
        response.raise_for_status()  # Raise an error for bad status codes
        data = response.json()
        logging.info(f"Market order placed successfully: {data}")
    except requests.RequestException as e:
        logging.error(f"An error occurred while placing the market order: {e}")
    except json.JSONDecodeError:
        logging.error(f"Failed to decode JSON response from server: {response.text}")

if __name__ == '__main__':
    try:
        config = load_config(CONFIG_FILE)
        api_key = config['GAIAEX_API_KEY']
        secret_key = config['GAIAEX_SECRET_KEY']
    except KeyError as e:
        logging.error(f"Missing expected configuration key: {e}")
        raise
    
    # Loop to place smaller futures orders
    for i in range(PARTS):
        logging.info(f"Placing futures order {i + 1} of {PARTS}...")
        place_market_order(api_key, secret_key, base_url=BASE_URL, request_path=REQUEST_PATH, contract_name=CONTRACT_NAME, volume=VOLUME_PER_ORDER, side=SIDE, recv_window=RECV_WINDOW)
        time.sleep(1)  # Adding a 1-second delay between orders to avoid rate limits

    # Example spot market order placement
    SPOT_CONTRACT_NAME = 'USDT-BTC-USDT'
    SPOT_VOLUME = 1  # Example volume for spot trading
    logging.info("Placing spot market order...")
    place_market_order(api_key, secret_key, base_url=SPOT_BASE_URL, request_path=SPOT_REQUEST_PATH, contract_name=SPOT_CONTRACT_NAME, volume=SPOT_VOLUME, side=SIDE, recv_window=RECV_WINDOW)
