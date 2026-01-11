import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

# ===============================
# Constants and Settings
# ===============================
# CONFIG_FILE = 'UID32937591.json'
CONFIG_FILE = 'UID32751170.json'
BASE_URL = 'https://openapi.gaiaex.com'
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
REQUEST_PATH = '/sapi/v1/order'
THRESHOLD = 10  # Only sell assets with balance > 10
SELL_INTERVAL = 120 # seconds between liquidation cycles

# Correct symbols that cause '-1122' errors
symbol_name_mapping = {
    "SOLANA/USDT":    "SOL/USDT",
    "LTCBSC/USDT":    "LTC/USDT",
    "POL/USDT":       "POL/USDT",
    "ORDIBRC20/USDT": "ORDI/USDT",
    "DAI1/USDT":      "DAI/USDT",
    "ETCBEP20/USDT":  "ETC/USDT",
    "WLD1/USDT":      "WLD/USDT",
    "BCHBSC/USDT":    "BCH/USDT"
}

# Global variables for precision
precision_data = {}
asset_to_symbol_map = {}

# ===============================
# Utility Functions
# ===============================
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

# ===============================
# API Communication Functions
# ===============================
def fetch_account_info(api_key, secret_key):
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
    response = requests.get(f"{BASE_URL}{DEPTH_PATH}", params={'symbol': f"{asset.lower()}usdt1802", 'limit': 1})
    if response.status_code == 200:
        bids = response.json().get('bids', [])
        return float(bids[0][0]) if bids else None
    return None

# ===============================
# Asset Handling Functions
# ===============================
def truncate(volume, precision):
    try:
        precision = int(precision)
    except ValueError:
        print(f"Invalid precision: {precision}. Skipping truncation.")
        return volume
    factor = 10 ** precision
    return int(volume * factor) / factor

def get_symbol_precision(symbol_name, precision_data):
    symbol_str = symbol_name.replace("/", "").lower()
    return precision_data.get(symbol_str, {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'})

def create_asset_summary(api_key, secret_key):
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
            asset_to_symbol_map.get(base_asset.lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'})
        )

        if value > 0:
            asset_summary[base_asset] = {
                "USDT Value": value,
                "Free Balance": balance,
                "Quantity Precision": precision.get('quantityPrecision', 'N/A'),
                "Price Precision": precision.get('pricePrecision', 'N/A')
            }

    return asset_summary

def sell_asset(api_key, secret_key, symbol_name, volume, old_quantity_precision):
    if symbol_name in symbol_name_mapping:
        print(f"Mapping {symbol_name} to {symbol_name_mapping[symbol_name]} to avoid '-1122' errors.")
        mapped_symbol = symbol_name_mapping[symbol_name]
    else:
        mapped_symbol = symbol_name

    global precision_data
    new_prec = get_symbol_precision(mapped_symbol, precision_data)
    final_quantity_precision = new_prec.get('quantityPrecision', 'N/A')

    if final_quantity_precision == 'N/A':
        final_quantity_precision = old_quantity_precision

    volume = truncate(volume, final_quantity_precision)

    url = f"{BASE_URL}{REQUEST_PATH}"
    timestamp = int(time.time() * 1000)

    params = json.dumps({
        'symbolName': mapped_symbol,
        'volume': volume,
        'side': 'SELL',
        'type': 'MARKET',
        'timestamp': timestamp,
        'recvWindow': 5000
    })
    headers = create_headers(api_key, secret_key, timestamp, params)

    try:
        print(f"\nPlacing SELL order for {mapped_symbol}: {volume}")
        response = requests.post(url, headers=headers, data=params)
        if response.status_code == 200:
            print(f"Order placed successfully: {response.json()}")
        else:
            print(f"Failed to place order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while placing the order: {e}")

# ===============================
# Main Program Loop
# ===============================
def main():
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']

    global precision_data, asset_to_symbol_map
    precision_data, asset_to_symbol_map = fetch_pairs_precision()

    while True:
        print("\nFetching latest asset summary...")
        asset_summary = create_asset_summary(api_key, secret_key)

        for base_asset, details in asset_summary.items():
            if details['USDT Value'] > 0:
                symbol_name = f"{base_asset}/USDT"
                volume = details['Free Balance']
                quantity_precision = details['Quantity Precision']
                sell_asset(api_key, secret_key, symbol_name, volume, quantity_precision)

        print(f"\nCompleted one sell cycle. Waiting {SELL_INTERVAL} seconds...")
        time.sleep(SELL_INTERVAL)

if __name__ == '__main__':
    main()
