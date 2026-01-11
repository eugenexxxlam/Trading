import requests
import json
import hashlib
import hmac
import time

# Constants for user customization
CONFIG_FILE = 'config_gaiaex_testing2.json'
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
REQUEST_PATH_ORDER = '/sapi/v1/order'  # Request path for placing a new spot order
REQUEST_PATH_SYMBOLS = '/sapi/v1/symbols'  # Request path for getting symbols precision
REQUEST_PATH_TICKER = '/sapi/v1/ticker/price'  # Request path for getting current price
REQUEST_PATH_DEPTH = '/sapi/v1/depth'  # Request path for getting market depth
SYMBOL = 'bnb1802usdt1802'  # Updated symbol from the pairs list
USDT_VALUE = 10  # Amount in USDT to buy/sell
SIDE = 'BUY'  # BUY or SELL
ORDER_TYPE = 'LIMIT'  # Order type
ORDER_COUNT = 1  # Number of limit orders to be placed

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

# Function to fetch symbol precision for volume
def fetch_symbol_precision(api_key, symbol):
    full_url = BASE_URL + REQUEST_PATH_SYMBOLS
    headers = {
        'X-CH-APIKEY': api_key
    }

    try:
        response = requests.get(full_url, headers=headers)
        if response.status_code == 200:
            symbols_data = response.json()
            for sym in symbols_data['symbols']:
                if sym['symbol'] == symbol:
                    return sym['quantityPrecision']
            print(f"Symbol {symbol} not found.")
        else:
            print(f"Failed to fetch symbols data. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Function to fetch the current price of a symbol
def fetch_current_price(api_key, symbol):
    full_url = BASE_URL + REQUEST_PATH_TICKER
    headers = {
        'X-CH-APIKEY': api_key
    }
    params = {
        'symbol': symbol
    }

    try:
        response = requests.get(full_url, headers=headers, params=params)
        if response.status_code == 200:
            data = response.json()
            # Print the response to understand its format
            print(f"Response data: {data}")
            
            # Check if 'price' is in the response
            if 'price' in data:
                return float(data['price'])
            else:
                print(f"'price' key not found in response. Response: {data}")
        else:
            print(f"Failed to fetch current price. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Function to fetch market depth
def fetch_market_depth(symbol, limit=100):
    full_url = BASE_URL + REQUEST_PATH_DEPTH
    params = {
        'symbol': symbol,
        'limit': limit
    }

    try:
        response = requests.get(full_url, params=params)
        if response.status_code == 200:
            data = response.json()
            print(f"Current Timestamp: {data.get('time')}")
            print("Bids:")
            for bid in data.get('bids', []):
                print(f"Price: {bid[0]}, Volume: {bid[1]}")
            print("Asks:")
            for ask in data.get('asks', []):
                print(f"Price: {ask[0]}, Volume: {ask[1]}")
            return data
        else:
            print("Failed to fetch market depth:", response.text)
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

# Function to place the limit order
def place_spot_limit_order(api_key, secret_key, symbol, price, volume, side, recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH_ORDER
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': symbol,
        'price': price,
        'volume': volume,
        'side': side,
        'type': 'LIMIT',
        'timestamp': timestamp,
        'recvWindow': recv_window
    }
    body = json.dumps(params)

    # Creating the signature based on the documentation
    signature_payload = f"{timestamp}POST{REQUEST_PATH_ORDER}{body}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()
    
    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.post(full_url, headers=headers, data=body)
        if response.status_code == 200:
            data = response.json()
            print(f"Spot limit order placed successfully: {data}")
        else:
            print(f"Failed to place spot limit order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetch the precision for the symbol
    quantity_precision = fetch_symbol_precision(api_key, SYMBOL)

    if quantity_precision is None:
        print("Could not fetch symbol precision. Exiting...")
    else:
        # Fetch the market depth to determine a price very high in the order book
        market_depth = fetch_market_depth(SYMBOL)
        if market_depth is None:
            print("Could not fetch market depth. Exiting...")
        else:
            # Get the highest bid price and place the limit order slightly above it
            if 'bids' in market_depth and len(market_depth['bids']) > 0:
                highest_bid_price = float(market_depth['bids'][0][0])
                limit_price = highest_bid_price * 1.01  # Set the limit price slightly above the highest bid

                # Calculate the volume to buy/sell in base asset
                current_price = fetch_current_price(api_key, SYMBOL)
                if current_price is None:
                    print("Failed to get current price, skipping order.")
                else:
                    volume = USDT_VALUE / current_price
                    
                    # Round the volume to the allowed precision
                    volume = round(volume, quantity_precision)

                    # Place the limit order with the calculated price and v