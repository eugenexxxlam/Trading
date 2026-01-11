import requests
import json
import hashlib
import hmac
import time
import random
import logging
from logging.handlers import RotatingFileHandler
from prettytable import PrettyTable  # Ensure PrettyTable is imported

# Constants
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH = '/sapi/v1/order'
RANDOM_TRADE_COUNT = 60
TOTAL_VOLUME = 1000  # Total amount to buy/sell
ORDER_COUNT = 5    # Number of market orders to be placed
WAIT_TIME = 1      # <-- Constant wait time in seconds

def load_config(file):
    try:
        with open(file, 'r') as f:
            config = json.load(f)
        logging.info("Configuration loaded successfully.")
        return config
    except FileNotFoundError:
        logging.error(f"Configuration file {file} not found.")
        exit()
    except json.JSONDecodeError:
        logging.error(f"Error decoding JSON from the configuration file {file}.")
        exit()

def generate_signature(secret_key, payload):
    signature = hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()
    logging.debug(f"Generated signature: {signature}")
    return signature

def create_headers_spot(api_key, secret_key, timestamp, body):
    signature = generate_signature(secret_key, f"{timestamp}POST{REQUEST_PATH}{body}")
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }
    logging.debug(f"Headers created: {headers}")
    return headers

def fetch_spot_symbols():
    try:
        response = requests.get(f"{BASE_URL}/sapi/v1/symbols")
        if response.status_code != 200:
            logging.error(f"Failed to fetch pairs list: {response.status_code} - {response.text}")
            return []
        symbols_data = response.json().get('symbols', [])
        symbols = [
            f"{s['baseAsset']}/{s['quoteAsset']}" for s in symbols_data
            if s['quoteAsset'].upper() == 'USDT' and s['baseAsset'].upper() != 'USDT'
        ]
        logging.info(f"Fetched {len(symbols)} tradable symbols.")

        # Create the table
        table = PrettyTable()
        table.field_names = ["No.", "Symbol"]
        for idx, symbol in enumerate(symbols, start=1):
            table.add_row([idx, symbol])
        
        # Convert table to string
        table_str = table.get_string()

        # Log the table
        logging.info("Available Tradable Symbols:\n" + table_str)

        # Optional: Print the table to the console
        print("\nAvailable Tradable Symbols:")
        print(table)

        return symbols
    except requests.RequestException as e:
        logging.error(f"Request exception while fetching tradable symbols: {e}")
        return []

def open_position_in_spot(api_key, secret_key, symbol_name):
    """
    Places ORDER_COUNT market orders for the given symbol_name.
    """
    per_order_volume = TOTAL_VOLUME / ORDER_COUNT
    url = f"{BASE_URL}{REQUEST_PATH}"

    for i in range(ORDER_COUNT):
        timestamp = int(time.time() * 1000)
        params = json.dumps({
            'symbolName': symbol_name,
            'volume': per_order_volume,
            'side': 'BUY',
            'type': 'MARKET',
            'timestamp': timestamp,
            'recvWindow': 5000
        })
        headers = create_headers_spot(api_key, secret_key, timestamp, params)

        try:
            response = requests.post(url, headers=headers, data=params)
            if response.status_code == 200:
                logging.info(f"Order {i + 1} placed successfully for {symbol_name}: {response.json()}")
            else:
                logging.error(
                    f"Failed to place order {i + 1} for {symbol_name}: "
                    f"{response.status_code} - {response.text}"
                )
        except requests.RequestException as e:
            logging.error(f"Error placing order {i + 1} for {symbol_name}: {e}")
        # Removed the old time.sleep(1) here; 
        # We will handle the constant wait time in the main loop.

if __name__ == '__main__':
    # Configure Logging with RotatingFileHandler
    timestamp_str = time.strftime("%Y%m%d_%H%M%S")
    log_file_name = f"{timestamp_str}_random_trading.log"

    handler = RotatingFileHandler(log_file_name, maxBytes=5*1024*1024, backupCount=5)  # 5 MB per file, keep 5 backups
    logging.basicConfig(
        handlers=[handler],
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    logging.info("Program started.")

    config = load_config(CONFIG_FILE)
    api_key = config.get('GAIAEX_API_KEY')
    secret_key = config.get('GAIAEX_SECRET_KEY')

    if not api_key or not secret_key:
        logging.error("API key or Secret key not found in the configuration.")
        exit()

    # Fetch tradable symbols
    tradable_list = fetch_spot_symbols()
    if not tradable_list:
        logging.error("No tradable symbols found. Exiting.")
        exit()

    # Execute random trades
    for trade_number in range(1, RANDOM_TRADE_COUNT + 1):
        symbol_name = random.choice(tradable_list)
        logging.info(f"Trade {trade_number}: Randomly selected symbol for trade: {symbol_name}")

        # Place market orders for this symbol
        open_position_in_spot(api_key, secret_key, symbol_name)

        # Wait a constant amount of time (in seconds) after each random trade
        logging.info(f"Waiting for {WAIT_TIME} seconds before next trade...")
        time.sleep(WAIT_TIME)

    logging.info("Program completed.")
