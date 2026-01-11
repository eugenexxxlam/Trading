import requests
import json
import hashlib
import hmac
import time
from prettytable import PrettyTable

# Constants
BASE_URL = 'https://openapi.gaiaex.com'
CONFIG_FILE = 'UID32937591.json'
ACCOUNT_PATH = '/sapi/v1/account'
DEPTH_PATH = '/sapi/v1/depth'
SYMBOLS_PATH = '/sapi/v1/symbols'
THRESHOLD = 1  # Minimum balance threshold

# Load configuration
def load_config(file_path):
    with open(file_path, 'r') as file:
        return json.load(file)

# Generate API signature
def generate_signature(secret_key, payload):
    return hmac.new(secret_key.encode(), payload.encode('utf-8'), hashlib.sha256).hexdigest()

# Fetch account balances
def fetch_account_info(api_key, secret_key):
    timestamp = int(time.time() * 1000)
    signature_payload = f"{timestamp}GET{ACCOUNT_PATH}"
    signature = generate_signature(secret_key, signature_payload)
    headers = {'X-CH-APIKEY': api_key, 'X-CH-SIGN': signature, 'X-CH-TS': str(timestamp)}
    response = requests.get(f"{BASE_URL}{ACCOUNT_PATH}", headers=headers)
    return response.json() if response.status_code == 200 else None

# Fetch precision and mappings for pairs
def fetch_pairs_precision():
    response = requests.get(f"{BASE_URL}{SYMBOLS_PATH}")
    if response.status_code != 200:
        return {}, {}
    data = response.json().get('symbols', [])
    precision_data = {s['symbol'].lower(): {'pricePrecision': s['pricePrecision'], 'quantityPrecision': s['quantityPrecision']} for s in data}
    asset_to_symbol_map = {s['baseAsset'].lower(): {'pricePrecision': s['pricePrecision'], 'quantityPrecision': s['quantityPrecision']} for s in data}
    return precision_data, asset_to_symbol_map

# Fetch best bid price
def fetch_best_bid(asset):
    response = requests.get(f"{BASE_URL}{DEPTH_PATH}", params={'symbol': f"{asset.lower()}usdt1802", 'limit': 1})
    bids = response.json().get('bids', []) if response.status_code == 200 else []
    return float(bids[0][0]) if bids else None

# Create a snapshot of balances
def balance_snapshot(api_key, secret_key):
    account_info = fetch_account_info(api_key, secret_key)
    if not account_info:
        return

    precision_data, asset_to_symbol_map = fetch_pairs_precision()
    account_balances = {b['asset']: float(b['free']) for b in account_info.get('balances', [])}

    rows = []
    for asset, balance in account_balances.items():
        if not asset.endswith("1802") or balance <= THRESHOLD:
            continue
        bid_price = fetch_best_bid(asset) if asset != "USDT1802" else 1.0
        value = balance * bid_price if bid_price else 0
        precision = precision_data.get(f"{asset.lower()}usdt1802", asset_to_symbol_map.get(asset[:-4].lower(), {'pricePrecision': 'N/A', 'quantityPrecision': 'N/A'}))
        rows.append({
            "Asset": asset,
            "Symbol": f"{asset.lower()}usdt1802",
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

# Main execution
if __name__ == '__main__':
    credentials = load_config(CONFIG_FILE)
    api_key = credentials['GAIAEX_API_KEY']
    secret_key = credentials['GAIAEX_SECRET_KEY']
    balance_snapshot(api_key, secret_key)
