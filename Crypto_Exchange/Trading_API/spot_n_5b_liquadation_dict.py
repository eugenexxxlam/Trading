import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable
import random


RANDOM_TRADE_COUNT = 20
# Constants for user customization
CONFIG_FILE = 'UID32937591.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
REQUEST_PATH = '/sapi/v1/order'  # Request path for placing a new spot order
SYMBOL_NAME = 'BTC/USDT'  # Updated symbol name for the currency pair
TOTAL_VOLUME = 10000  # Total amount to buy/sell
SIDE = 'BUY'  # BUY or SELL
ORDER_TYPE = 'MARKET'  # Order type
ORDER_COUNT = 5  # Number of market orders to be placed
THRESHOLD = 1  # Minimum balance threshold


def load_config(config_file):
    """Load API configuration from a JSON file."""
    with open(config_file, 'r') as file:
        return json.load(file)

def generate_signature(secret_key, payload):
    """Generate HMAC signature for GET requests."""
    return hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()

def generate_signature_post(secret_key, timestamp, body):
    """Generate HMAC signature for POST requests."""
    signature_payload = f"{timestamp}POST{REQUEST_PATH}{body}".encode('utf-8')
    return hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

def create_headers(api_key, secret_key, timestamp, body):
    """Create headers for the API request."""
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
    headers = {'X-CH-APIKEY': api_key, 'X-CH-SIGN': signature, 'X-CH-TS': str(timestamp)}
    response = requests.get(f"{BASE_URL}{ACCOUNT_PATH}", headers=headers)
    return response.json() if response.status_code == 200 else None

def fetch_pairs_precision():
    """Fetch precision and mappings for trading pairs."""
    response = requests.get(f"{BASE_URL}{SYMBOLS_PATH}")
    if response.status_code != 200:
        return {}, {}
    data = response.json().get('symbols', [])
    precision_data = {s['symbol'].lower(): {'pricePrecision': s['pricePrecision'], 'quantityPrecision': s['quantityPrecision']} for s in data}
    asset_to_symbol_map = {s['baseAsset'].lower(): {'pricePrecision': s['pricePrecision'], 'quantityPrecision': s['quantityPrecision']} for s in data}
    return precision_data, asset_to_symbol_map

def fetch_best_bid(asset):
    """Fetch the best bid price for an asset."""
    response = requests.get(f"{BASE_URL}{DEPTH_PATH}", params={'symbol': f"{asset.lower()}usdt1802", 'limit': 1})
    bids = response.json().get('bids', []) if response.status_code == 200 else []
    return float(bids[0][0]) if bids else None

def balance_snapshot(api_key, secret_key):
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    precision_data, asset_to_symbol_map = fetch_pairs_precision()

    account_dict = {balance['asset']: float(balance['free']) for balance in account_info.get('balances', [])}

    rows = []
    for asset, balance in account_dict.items():
        symbol = f"{asset.lower()}usdt1802"
        base_asset = asset[:-4]
        bid_price = 1.0 if asset == "USDT1802" else fetch_best_bid(asset)
        value = balance * bid_price if bid_price else 0

        precision = precision_data.get(symbol, asset_to_symbol_map.get(base_asset.lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))

        meets_threshold = "Yes" if balance > THRESHOLD and asset.endswith("1802") else "No"

        # Only include rows where the value is greater than 0.1 USDT
        if value > 0.1:
            rows.append([asset, symbol, base_asset, balance, bid_price, value, precision['pricePrecision'], precision['quantityPrecision'], meets_threshold])

    # Sort rows by Value (USDT) in descending order
    rows.sort(key=lambda x: x[5], reverse=True)
    # Create PrettyTable
    table = PrettyTable()
    table.field_names = ["Asset", "Symbol", "Base Asset", "Free Balance", "Bid Price", "Value (USDT)", 
                        "Price Precision", "Quantity Precision", "Meets Threshold"]

    for row in rows:
        asset, symbol, base_asset, balance, bid_price, value, price_precision, quantity_precision, meets_threshold = row
        bid_display = f"{bid_price:,.8f}" if bid_price else "N/A"
        value_display = f"{value:,.1f}" if bid_price else "N/A"  # Format Value (USDT) to 1 decimal place
        table.add_row([asset, symbol, base_asset, f"{balance:,.8f}", bid_display, value_display, 
                    price_precision, quantity_precision, meets_threshold])

    print("\nAssets with Value > 0.1 USDT (Sorted by Value):")
    print(table)

def place_market_orders(api_key, secret_key, symbol_name, total_volume, side, order_type, order_count):
    """Place multiple spot market orders."""
    full_url = BASE_URL + REQUEST_PATH
    per_order_volume = total_volume / order_count

    for i in range(order_count):
        print(f"Placing order {i + 1} of {order_count}")
        timestamp = int(time.time() * 1000)

        params = {
            'symbolName': symbol_name,  # Use symbolName with slash
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
            print(f"Request Body: {body}")  # Log request body for debugging
            response = requests.post(full_url, headers=headers, data=body)
            if response.status_code == 200:
                data = response.json()
                print(f"Order {i + 1} placed successfully: {data}")
            else:
                print(f"Failed to place order {i + 1}. Status Code: {response.status_code}, Response: {response.text}")
        except requests.RequestException as e:
            print(f"An error occurred while placing order {i + 1}: {e}")

        time.sleep(1)  # Optional delay between orders to avoid rate limit issues

def fetch_tradable_symbols():
    """Fetch trading pairs and format them into tradable symbol names."""
    url = 'https://openapi.gaiaex.com/sapi/v1/symbols'  # URL to fetch pairs list
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        tradable_symbols = [
            f"{symbol['baseAsset']}/{symbol['quoteAsset']}"  # Convert to 'Base/Quote' format
            for symbol in symbols
            if symbol['quoteAsset'].upper() == 'USDT'  # Filter for USDT pairs only (if needed)
        ]
        return tradable_symbols
    else:
        print("Failed to fetch pairs list:", response.text)
        return []

def get_assets_dict(api_key, secret_key):
    """Generate a dictionary of assets with symbolName, volume, and quantityPrecision."""
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        return {}

    precision_data, _ = fetch_pairs_precision()

    # Account balances
    account_dict = {balance['asset']: float(balance['free']) for balance in account_info.get('balances', [])}

    # Generate symbolName, volume, and precision
    assets_dict = {}
    for asset, balance in account_dict.items():
        if balance > 0:  # Only include assets with non-zero balance
            base_asset = asset[:-4]
            symbol_name = f"{base_asset}/USDT"
            # Retrieve quantity precision from precision data
            precision = precision_data.get(f"{base_asset.lower()}usdt1802", {}).get('quantityPrecision', 8)
            # Add to dictionary
            assets_dict[symbol_name] = {'volume': balance, 'quantityPrecision': precision}

    return assets_dict

def truncate(value, precision):
    """Truncate a number to a specific number of decimal places."""
    factor = 10 ** precision
    return int(value * factor) / factor

def sell_asset(api_key, secret_key, symbol_name, volume, quantity_precision):
    """Place a market sell order for a specific asset."""
    url = f"{BASE_URL}{REQUEST_PATH}"
    timestamp = int(time.time() * 1000)

    # Truncate the volume to match the quantity precision
    volume = truncate(volume, quantity_precision)

    params = json.dumps({
        'symbolName': symbol_name,  # Asset in Base/Quote format
        'volume': volume,           # Truncated volume
        'side': 'SELL',             # Side of the order
        'type': 'MARKET',           # Order type
        'timestamp': timestamp,
        'recvWindow': 5000
    })

    headers = create_headers(api_key, secret_key, timestamp, params)

    try:
        print(f"Placing SELL order for {symbol_name}: {volume}")
        response = requests.post(url, headers=headers, data=params)
        if response.status_code == 200:
            print(f"Order placed successfully: {response.json()}")
        else:
            print(f"Failed to place order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while placing the order: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    # Print initial asset list
    print("Initial Asset List:")
    balance_snapshot(api_key, secret_key)
    
    # Generate and display assets dictionary
    assets_dict = get_assets_dict(api_key, secret_key)
    print("Assets Dictionary:")
    print(json.dumps(assets_dict, indent=4))

    # Choose an asset to sell (example: BTC/USDT)
    if assets_dict:
        symbol_to_sell = list(assets_dict.keys())[0]  # Select the first asset in the dictionary
        asset_info = assets_dict[symbol_to_sell]
        volume_to_sell = asset_info['volume']
        quantity_precision = asset_info['quantityPrecision']
        
        # Place a sell order
        sell_asset(api_key, secret_key, symbol_to_sell, volume_to_sell, quantity_precision)
    else:
        print("No assets available to sell.")
