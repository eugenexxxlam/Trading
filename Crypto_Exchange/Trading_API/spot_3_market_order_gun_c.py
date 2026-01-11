import requests
import json
import hashlib
import hmac
import time
import logging

# Setup logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# Constants for user customization
CONFIG_FILE = 'config_gaiaex_testing2.json'
BASE_URL = 'https://futuresopenapi.gaiaex.com'  # Base URL for futures API
SPOT_BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for spot API
REQUEST_PATH = '/fapi/v1/order'  # Request path for placing a new futures order
SPOT_REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
REQUEST_PATH_SYMBOLS = '/sapi/v1/symbols'  # Request path for fetching trading pairs
REQUEST_PATH_ACCOUNT = '/sapi/v1/account'  # Request path for fetching account balances
CONTRACT_NAME = 'USDT-TON-USDT'
TOTAL_VOLUME = 10000  # Original total volume
PARTS = 10  # Split into 10 parts
SIDE = 'BUY'
OPEN_POSITION = 'OPEN'
POSITION_TYPE = 1
RECV_WINDOW = 5000
USD_BUFFER = 5  # USD value buffer to leave in each asset

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

def fetch_pairs_list():
    url = SPOT_BASE_URL + REQUEST_PATH_SYMBOLS
    response = requests.get(url)
    if response.status_code == 200:
        return response.json().get('symbols', [])
    else:
        logging.error(f"Failed to fetch pairs list: {response.text}")
        return []

def fetch_account_balances(api_key, secret_key):
    full_url = SPOT_BASE_URL + REQUEST_PATH_ACCOUNT
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{REQUEST_PATH_ACCOUNT}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }

    try:
        response = requests.get(full_url, headers=headers)
        response.raise_for_status()
        return response.json().get('balances', [])
    except requests.RequestException as e:
        logging.error(f"An error occurred while fetching account balances: {e}")
        return []

def place_market_order(api_key, secret_key, base_url, request_path, contract_name, volume, side, recv_window=5000):
    full_url = base_url + request_path
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': contract_name,
        'volume': volume,
        'side': side,
        'type': 'MARKET',
        'timestamp': timestamp,
        'recvWindow': recv_window
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
    logging.info("Fetching trading pairs...")
    pairs_list = fetch_pairs_list()
    pairs_dict = {pair['baseAsset']: pair['symbol'] for pair in pairs_list if pair['quoteAsset'] == 'USDT'}

    logging.info("Fetching account balances...")
    account_balances = fetch_account_balances(api_key, secret_key)

    # Loop through each balance and sell if above USD 5
    for balance in account_balances:
        asset = balance['asset']
        free_balance = float(balance['free'])

        if free_balance > 0:
            # Find the corresponding trading pair
            symbol = pairs_dict.get(asset)
            if not symbol:
                logging.info(f"No USDT trading pair found for asset {asset}. Skipping...")
                continue

            # Fetch the current price (this should be replaced with a real price-fetching function)
            current_price = 0.5  # Placeholder value, replace this with actual price-fetching logic

            if current_price <= 0:
                logging.info(f"Invalid current price for {symbol}. Skipping...")
                continue

            # Calculate the USD value of the free balance
            usd_value = free_balance * current_price

            # Sell if the value is greater than USD 5
            if usd_value > USD_BUFFER:
                volume = round(free_balance - (USD_BUFFER / current_price), 8)  # Leave USD 5 worth of the asset
                if volume > 0:
                    logging.info(f"Placing sell order for {symbol}: Volume={volume}, Price={current_price}")
                    place_market_order(api_key, secret_key, base_url=SPOT_BASE_URL, request_path=SPOT_REQUEST_PATH, contract_name=symbol, volume=volume, side='SELL', recv_window=RECV_WINDOW)
                    time.sleep(1)  # Sleep to avoid rate limit issues
