import requests
import json
import hashlib
import hmac
import time
import logging
from logging.handlers import RotatingFileHandler
from prettytable import PrettyTable  # Ensure PrettyTable is imported

# Constants
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
REQUEST_PATH = '/sapi/v1/order'
TOTAL_VOLUME = 15000  # Total amount to buy/sell
ORDER_COUNT = 1  # Number of market orders to be placed
THRESHOLD = 1  # Minimum balance threshold for snapshot

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

def create_headers(api_key, secret_key, timestamp, body):
    signature = generate_signature(secret_key, f"{timestamp}POST{REQUEST_PATH}{body}")
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }
    logging.debug(f"Headers created: {headers}")
    return headers

def fetch_account_info(api_key, secret_key):
    timestamp = int(time.time() * 1000)
    payload = f"{timestamp}GET/sapi/v1/account"
    headers = create_headers(api_key, secret_key, timestamp, "")
    try:
        response = requests.get(f"{BASE_URL}/sapi/v1/account", headers=headers)
        if response.status_code == 200:
            logging.info("Account information fetched successfully.")
            return response.json()
        else:
            logging.error(f"Failed to fetch account info: {response.status_code} - {response.text}")
            return {}
    except requests.RequestException as e:
        logging.error(f"Request exception while fetching account info: {e}")
        return {}

def fetch_tradable_symbols():
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

def fetch_pairs_precision():
    """Fetch precision details for tradable pairs."""
    try:
        response = requests.get(f"{BASE_URL}/sapi/v1/symbols")
        if response.status_code != 200:
            logging.error(f"Failed to fetch symbol precision data: {response.status_code} - {response.text}")
            return {}, {}

        symbols_data = response.json().get('symbols', [])
        
        # Prepare precision data and asset-to-symbol mapping
        precision_data = {}
        asset_to_symbol_map = {}
        
        for symbol_info in symbols_data:
            base_asset = symbol_info['baseAsset'].lower()
            quote_asset = symbol_info['quoteAsset'].lower()

            symbol = f"{base_asset}/{quote_asset}"

            # Get price and quantity precision
            price_precision = symbol_info.get('pricePrecision', 0)
            quantity_precision = symbol_info.get('quantityPrecision', 0)

            precision_data[f"{base_asset}{quote_asset}"] = {
                'pricePrecision': price_precision,
                'quantityPrecision': quantity_precision
            }

            # Build the asset-to-symbol map
            asset_to_symbol_map[base_asset] = {
                'pricePrecision': price_precision,
                'quantityPrecision': quantity_precision
            }
        
        logging.info("Fetched precision data for all symbols.")
        return precision_data, asset_to_symbol_map

    except requests.RequestException as e:
        logging.error(f"Request exception while fetching precision data: {e}")
        return {}, {}

# Create a snapshot of balances
def balance_snapshot(api_key, secret_key):
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        return

    precision_data, asset_to_symbol_map = fetch_pairs_precision()
    tradable_symbols = fetch_tradable_symbols()
    
    # Create a mapping for base assets to symbols
    base_asset_to_symbol = {}
    for symbol in tradable_symbols:
        base_asset = symbol.split('/')[0].lower()
        base_asset_to_symbol[base_asset] = symbol
    
    account_balances = {b['asset']: float(b['free']) for b in account_info.get('balances', [])}

    rows = []
    for asset, balance in account_balances.items():
        if balance <= THRESHOLD or not asset.endswith("1802"):
            continue

        # Check if the asset is part of any tradable pair
        base_asset = asset[:-4].lower()  # Remove the "1802" suffix to get the base asset
        symbol = base_asset_to_symbol.get(base_asset, None)

        if symbol:
            bid_price = fetch_best_bid(asset) if asset != "USDT1802" else 1.0
            value = balance * bid_price if bid_price else 0
            precision = precision_data.get(f"{asset.lower()}usdt1802", asset_to_symbol_map.get(base_asset, {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))

            rows.append({
                "Asset": asset,
                "Symbol": symbol,
                "Free Balance": f"{balance:,.8f}",
                "Bid Price": f"{bid_price:,.8f}" if bid_price else "N/A",
                "Value (USDT)": f"{value:,.8f}" if value else "N/A",
                "Price Precision": precision['pricePrecision'],
                "Quantity Precision": precision['quantityPrecision']
            })

    # Sort rows by Value in descending order
    rows.sort(key=lambda x: float(x["Value (USDT)"].replace(',', '')) if x["Value (USDT)"] != "N/A" else 0, reverse=True)

    # Display the data in a PrettyTable
    table = PrettyTable(field_names=["Asset", "Symbol", "Free Balance", "Bid Price", "Value (USDT)", "Price Precision", "Quantity Precision"])
    for row in rows:
        table.add_row([row[field] for field in table.field_names])

    print("\nAssets with Value > 1 USDT:")
    print(table)

# Place market orders and log balances before and after
def place_market_orders(api_key, secret_key, symbol_name):
    per_order_volume = TOTAL_VOLUME / ORDER_COUNT
    url = f"{BASE_URL}{REQUEST_PATH}"

    # Take snapshot before placing orders
    logging.info("Taking balance snapshot before executing trades:")
    balance_snapshot(api_key, secret_key)

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
        headers = create_headers(api_key, secret_key, timestamp, params)

        try:
            response = requests.post(url, headers=headers, data=params)
            if response.status_code == 200:
                logging.info(f"Order {i + 1} placed successfully for {symbol_name}: {response.json()}")
            else:
                logging.error(f"Failed to place order {i + 1} for {symbol_name}: {response.status_code} - {response.text}")
        except requests.RequestException as e:
            logging.error(f"Error placing order {i + 1} for {symbol_name}: {e}")
        time.sleep(1)  # To avoid rate limits

    # Take snapshot after placing orders
    logging.info("Taking balance snapshot after executing trades:")
    balance_snapshot(api_key, secret_key)

if __name__ == '__main__':
    # Configure Logging with RotatingFileHandler
    timestamp_str = time.strftime("%Y%m%d_%H%M%S")
    log_file_name = f"{timestamp_str}_sequential_trading.log"

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
    tradable_list = fetch_tradable_symbols()
    if not tradable_list:
        logging.error("No tradable symbols found. Exiting.")
        exit()

    # Execute trades sequentially on each symbol
    for trade_number, symbol_name in enumerate(tradable_list, start=1):
        logging.info(f"Trade {trade_number}: Trading symbol {symbol_name}")
        place_market_orders(api_key, secret_key, symbol_name)

    logging.info("Program completed.")
