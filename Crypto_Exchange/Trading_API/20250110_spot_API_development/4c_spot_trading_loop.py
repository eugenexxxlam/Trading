import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable
import threading
import logging
from logging.handlers import RotatingFileHandler
import random

# Hard-coded fix for symbols that cause '-1122' errors.
# Key   = Symbol returned by your code
# Value = Actual symbol name that the exchange will accept
symbol_name_mapping = {
    "SOLANA/USDT":    "SOL/USDT",
    "LTCBSC/USDT":    "LTC/USDT",
    "POL/USDT":       "MATIC/USDT",
    "ORDIBRC20/USDT": "ORDI/USDT",
    "DAI1/USDT":      "DAI/USDT",
    "ETCBEP20/USDT":  "ETC/USDT",
    "WLD1/USDT":      "WLD/USDT",
    "BCHBSC/USDT":    "BCH/USDT"
}

CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
REQUEST_PATH = '/sapi/v1/order'
THRESHOLD = 1  # Threshold for asset inclusion in snapshot

RANDOM_TRADE_COUNT = 60
TOTAL_VOLUME = 1000  # Total amount to buy/sell
ORDER_COUNT = 3    # Number of market orders to be placed
WAIT_TIME = 2      # <-- Constant wait time in seconds
LIQUIDATION_TIME = 30

# We'll store these globally after we fetch them, so sell_asset() can reference them.
precision_data = {}
asset_to_symbol_map = {}

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def generate_signature(secret_key, payload):
    return hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()

def generate_signature_post(secret_key, timestamp, body):
    signature_payload = f"{timestamp}POST{REQUEST_PATH}{body}".encode('utf-8')
    return hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

def create_headers(api_key, secret_key, timestamp, body):
    signature = generate_signature_post(secret_key, timestamp, body)
    return {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

def fetch_account_info(api_key, secret_key):
    """Fetch account balances."""
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{ACCOUNT_PATH}"
    signature = generate_signature(secret_key, signature_payload)
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }
    response = requests.get(f"{BASE_URL}{ACCOUNT_PATH}", headers=headers)
    return response.json() if response.status_code == 200 else None

def fetch_pairs_precision():
    """Fetch precision and mappings for trading pairs from the exchange."""
    response = requests.get(f"{BASE_URL}{SYMBOLS_PATH}")
    if response.status_code != 200:
        return {}, {}
    data = response.json().get('symbols', [])
    precision_data = {
        s['symbol'].lower(): {
            'pricePrecision': s['pricePrecision'],
            'quantityPrecision': s['quantityPrecision']
        }
        for s in data
    }
    asset_to_symbol_map = {
        s['baseAsset'].lower(): {
            'pricePrecision': s['pricePrecision'],
            'quantityPrecision': s['quantityPrecision']
        }
        for s in data
    }
    return precision_data, asset_to_symbol_map

def fetch_best_bid(asset):
    """Fetch the best bid price for an asset. 
       Currently uses a custom 'symbol' approach: {asset.lower()}usdt1802
       If that doesn't exist, it might return None.
    """
    response = requests.get(f"{BASE_URL}{DEPTH_PATH}",
                            params={'symbol': f"{asset.lower()}usdt1802", 'limit': 1})
    if response.status_code == 200:
        bids = response.json().get('bids', [])
        return float(bids[0][0]) if bids else None
    else:
        return None

def balance_snapshot(api_key, secret_key):
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    # Use the global precision_data, asset_to_symbol_map we fetch at startup
    global precision_data, asset_to_symbol_map

    account_dict = {
        balance['asset']: float(balance['free'])
        for balance in account_info.get('balances', [])
    }

    rows = []
    result_dict = {}
    for asset, balance in account_dict.items():
        symbol = f"{asset.lower()}usdt1802"
        base_asset = asset[:-4]
        bid_price = 1.0 if asset == "USDT1802" else fetch_best_bid(asset)
        value = balance * bid_price if bid_price else 0

        # Attempt to get precision from the “symbol” or from the “baseAsset”
        precision = precision_data.get(
            symbol, 
            asset_to_symbol_map.get(base_asset.lower(), 
                {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'})
        )

        meets_threshold = "Yes" if balance > THRESHOLD and asset.endswith("1802") else "No"

        # Only include rows where the value > 0.1 USDT
        if value > 0.1:
            rows.append([
                asset, symbol, base_asset, balance, bid_price, value,
                precision['pricePrecision'], precision['quantityPrecision'], meets_threshold
            ])
            result_dict[base_asset] = {
                "Asset": asset,
                "Symbol": symbol,
                "Free Balance": balance,
                "Bid Price": bid_price,
                "Value (USDT)": value,
                "Price Precision": precision['pricePrecision'],
                "Quantity Precision": precision['quantityPrecision'],
                "Meets Threshold": meets_threshold,
            }

    # Sort rows by Value (USDT) descending
    rows.sort(key=lambda x: x[5], reverse=True)

    # Create PrettyTable
    table = PrettyTable()
    table.field_names = [
        "Asset", "Symbol", "Base Asset", "Free Balance", "Bid Price",
        "Value (USDT)", "Price Precision", "Quantity Precision", "Meets Threshold"
    ]

    for row in rows:
        asset, symbol, base_asset, balance, bid_price, value, price_precision, quantity_precision, meets_threshold = row
        bid_display = f"{bid_price:,.8f}" if bid_price else "N/A"
        value_display = f"{value:,.1f}"
        table.add_row([
            asset, symbol, base_asset,
            f"{balance:,.8f}", bid_display, value_display,
            price_precision, quantity_precision, meets_threshold
        ])

    print("\nAssets with Value > 0.1 USDT (Sorted by Value):")
    print(table)
    return result_dict

def place_market_orders(api_key, secret_key, symbol_name, total_volume, side, order_type, order_count):
    """Example function that places multiple spot market orders for a given symbol."""
    full_url = BASE_URL + REQUEST_PATH
    per_order_volume = total_volume / order_count

    for i in range(order_count):
        print(f"Placing order {i + 1} of {order_count}")
        timestamp = int(time.time() * 1000)

        params = {
            'symbolName': symbol_name,
            'volume': per_order_volume,
            'side': side,
            'type': order_type,
            'timestamp': timestamp,
            'recvWindow': 5000
        }
        body = json.dumps(params)
        headers = create_headers(api_key, secret_key, timestamp, body)

        try:
            print(f"Request Headers: {headers}")
            print(f"Request Body: {body}")
            response = requests.post(full_url, headers=headers, data=body)
            if response.status_code == 200:
                data = response.json()
                print(f"Order {i + 1} placed successfully: {data}")
            else:
                print(f"Failed to place order {i + 1}. Status: {response.status_code}, Resp: {response.text}")
        except requests.RequestException as e:
            print(f"Error placing order {i + 1}: {e}")

        time.sleep(1)  # to avoid rate limits

def fetch_tradable_symbols():
    """Fetch trading pairs and format them into 'Base/Quote' strings."""
    url = f"{BASE_URL}{SYMBOLS_PATH}"
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        tradable_symbols = [
            f"{symbol['baseAsset']}/{symbol['quoteAsset']}" 
            for symbol in symbols
            if symbol['quoteAsset'].upper() == 'USDT'
        ]
        return tradable_symbols
    else:
        print("Failed to fetch pairs list:", response.text)
        return []

def create_asset_summary(api_key, secret_key):
    """Create a dictionary summarizing key details for each base asset."""
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    global precision_data, asset_to_symbol_map

    account_dict = {
        balance['asset']: float(balance['free'])
        for balance in account_info.get('balances', [])
    }

    asset_summary = {}
    for asset, balance in account_dict.items():
        symbol = f"{asset.lower()}usdt1802"
        base_asset = asset[:-4]
        bid_price = 1.0 if asset == "USDT1802" else fetch_best_bid(asset)
        value = balance * bid_price if bid_price else 0

        precision = precision_data.get(
            symbol,
            asset_to_symbol_map.get(base_asset.lower(), {'pricePrecision': 'N/A','quantityPrecision': 'N/A'})
        )

        if value > 0:
            asset_summary[base_asset] = {
                "USDT Value": value,
                "Free Balance": balance,
                "Quantity Precision": precision.get('quantityPrecision', 'N/A'),
                "Price Precision": precision.get('pricePrecision', 'N/A')
            }

    return asset_summary

def truncate(volume, precision):
    """Truncate volume based on integer precision."""
    try:
        precision = int(precision)
    except ValueError:
        print(f"Invalid precision: {precision}. Skipping truncation.")
        return volume
    factor = 10 ** precision
    truncated_volume = int(volume * factor) / factor
    print(f"Truncated volume: {volume} -> {truncated_volume} with precision {precision}")
    return truncated_volume

def get_symbol_precision(symbol_name, precision_data):
    """
    Convert symbol_name e.g. 'SOL/USDT' to 'solusdt',
    then look up in precision_data. Returns { pricePrecision, quantityPrecision }
    or {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'} if not found.
    """
    symbol_str = symbol_name.replace("/", "").lower()  # e.g. 'SOL/USDT' -> 'solusdt'
    return precision_data.get(
        symbol_str,
        {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}
    )

def sell_asset(api_key, secret_key, symbol_name, volume, old_quantity_precision):
    """Place a market sell order for a specific asset."""
    # 1) Map symbol if needed
    if symbol_name in symbol_name_mapping:
        print(f"Mapping {symbol_name} to {symbol_name_mapping[symbol_name]} to avoid '-1122' errors.")
        mapped_symbol = symbol_name_mapping[symbol_name]
    else:
        mapped_symbol = symbol_name

    # 2) Re-fetch the correct precision for the MAPPED symbol
    global precision_data
    new_prec = get_symbol_precision(mapped_symbol, precision_data)  # e.g. {'quantityPrecision': 5}
    final_quantity_precision = new_prec.get('quantityPrecision', 'N/A')

    if final_quantity_precision == 'N/A':
        # fallback to the one we got from asset_summary if still 'N/A'
        final_quantity_precision = old_quantity_precision

    # 3) Truncate the volume with the final quantity precision
    volume = truncate(volume, final_quantity_precision)

    # 4) Prepare request
    url = f"{BASE_URL}{REQUEST_PATH}"
    timestamp = int(time.time() * 1000)

    params = json.dumps({
        'symbolName': mapped_symbol,  # actual mapped symbol
        'volume': volume,
        'side': 'SELL',
        'type': 'MARKET',
        'timestamp': timestamp,
        'recvWindow': 5000
    })

    headers = create_headers(api_key, secret_key, timestamp, params)

    # 5) Send request
    try:
        print(f"\nPlacing SELL order for {mapped_symbol}: {volume}")
        response = requests.post(url, headers=headers, data=params)
        if response.status_code == 200:
            print(f"Order placed successfully: {response.json()}")
        else:
            print(f"Failed to place order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while placing the order: {e}")

def sell_all_assets_periodically(api_key, secret_key, asset_summary, interval):
    """Function to sell all assets periodically at the given interval."""
    while True:
        for base_asset, details in asset_summary.items():
            if details['USDT Value'] > 0:
                symbol_name = f"{base_asset}/USDT"
                volume = details['Free Balance']
                quantity_precision = details['Quantity Precision']
                sell_asset(api_key, secret_key, symbol_name, volume, quantity_precision)
        print(f"Liquidation will start again in {interval} seconds.")
        time.sleep(interval)  # Wait for the defined interval before selling again.

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
    
    # Load configuration and API keys
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetch precision data and create an asset summary
    precision_data, asset_to_symbol_map = fetch_pairs_precision()
    asset_summary = create_asset_summary(api_key, secret_key)

    # Optionally display tradable symbols
    tradable_symbols = fetch_tradable_symbols()
    print("Tradable Symbols (Base/Quote):")
    print(tradable_symbols)

    # Start the periodic selling in a separate thread
    sell_interval = LIQUIDATION_TIME  # Interval in seconds between sell operations
    thread = threading.Thread(target=sell_all_assets_periodically, args=(api_key, secret_key, asset_summary, sell_interval))
    thread.start()
    
    # The thread runs independently; the main program can continue to perform other tasks or enter a sleep state.
    thread.join()  
    
    
    
    
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