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
SYMBOL_NAME = 'btc/USDT'  # Updated symbol name for the currency pair
TOTAL_VOLUME = 1.37  # Total amount to buy/sell
SIDE = 'SELL'  # BUY or SELL
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
    result_dict = {}  # Initialize an empty dictionary to store the result
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
            # Add entry to result_dict using Base Asset as the key
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

    return result_dict  # Return the result dictionary

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

def create_asset_summary(api_key, secret_key):
    """Create a dictionary summarizing key details for each Base Asset."""
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        print("Failed to fetch account information.")
        exit(1)

    # Fetch tradable symbols dynamically (now based on the actual available pairs)
    tradable_symbols = fetch_tradable_symbols()

    # Fetch precision data and map from symbol to base/quote pairs
    precision_data, asset_to_symbol_map = fetch_pairs_precision()

    account_dict = {balance['asset']: float(balance['free']) for balance in account_info.get('balances', [])}

    asset_summary = {}  # Dictionary to hold the summary for each Base Asset
    for asset, balance in account_dict.items():
        # Dynamically find the correct symbol for the asset (Base/Quote format)
        symbol = None
        for tradable_symbol in tradable_symbols:
            if asset.lower() in tradable_symbol.lower():
                symbol = tradable_symbol
                break
        
        if not symbol:
            continue  # If no matching symbol is found, skip this asset

        # Now that we have the correct symbol, we fetch the best bid price
        bid_price = fetch_best_bid(symbol)
        value = balance * bid_price if bid_price else 0

        # Adjust precision based on the dynamically fetched symbol
        precision = precision_data.get(symbol.lower(), asset_to_symbol_map.get(asset.lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))

        # Only include assets with a non-zero value
        if value > 0:
            base_asset = symbol.split('/')[0]  # Extract base asset (e.g., BTC from BTC/USDT)
            asset_summary[base_asset] = {
                "USDT Value": value,
                "Free Balance": balance,
                "Quantity Precision": precision.get('quantityPrecision', 'N/A'),
                "Price Precision": precision.get('pricePrecision', 'N/A')
            }

    return asset_summary

def truncate(volume, precision):
    # Ensure precision is an integer
    try:
        precision = int(precision)
    except ValueError:
        # Handle invalid precision (e.g., "N/A")
        print(f"Invalid precision: {precision}. Skipping truncation.")
        return volume

    factor = 10 ** precision
    return int(volume * factor) / factor

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

def sell_all_assets(api_key, secret_key, asset_summary):
    """
    Sell all holding assets with non-zero USDT Value by submitting market orders.
    
    :param api_key: API key for authentication.
    :param secret_key: Secret key for authentication.
    :param asset_summary: Dictionary of asset summaries.
    """
    for base_asset, details in asset_summary.items():
        # Skip if the asset's USDT Value is 0
        if details['USDT Value'] <= 0:
            continue
        
        # Prepare symbol name in Base/Quote format
        symbol_name = f"{base_asset}/USDT"
        volume = details['Free Balance']
        quantity_precision = details['Quantity Precision']

        # Submit the market sell order
        sell_asset(api_key, secret_key, symbol_name, volume, quantity_precision)

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    symbolsname = fetch_tradable_symbols()
    print(symbolsname)

    # Fetch the asset summary
    asset_summary = create_asset_summary(api_key, secret_key)
    print("Asset Summary:")
    print(json.dumps(asset_summary, indent=4))

    # Sell all assets
    sell_all_assets(api_key, secret_key, asset_summary)
