import requests
import json
import hashlib
import hmac
import time

BASE_URL = 'https://futuresopenapi.gaiaex.com'
MARKET_DEPTH_PATH = '/fapi/v1/depth'
REQUEST_PATH = '/fapi/v1/order'
CONFIG_FILE = 'config_gaiaex_testing2.json'
CONTRACT_NAME = 'USDT-BTC-USDT'
VOLUME = 1
SIDE = 'BUY'
OPEN_POSITION = 'OPEN'
POSITION_TYPE = 1
RECV_WINDOW = 5000
ORDER_COUNT = 10


def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)


def fetch_bid(contract_name, limit=100):
    url = BASE_URL + MARKET_DEPTH_PATH
    params = {'contractName': contract_name, 'limit': limit}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        if 'bids' in data:
            best_bid = data['bids'][0] if data['bids'] else None
            return float(best_bid[0]) if best_bid else None
    print("Failed to fetch market depth or no bid data available:", response.text)
    return None


def get_price_precision(contract_name):
    # Mock function to determine price precision, should be replaced with actual API call if available
    price_precisions = {
        'USDT1802-BTC-USDT': 1  # Adjusted precision to 1 decimal place
    }
    return price_precisions.get(contract_name, 1)


def place_limit_order(api_key, secret_key, contract_name, volume, price, side, open_position='OPEN', position_type=1, recv_window=5000):
    full_url = BASE_URL + REQUEST_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'volume': volume,
        'side': side,
        'type': 'LIMIT',
        'price': price,
        'open': open_position,
        'positionType': position_type,
        'recvWindow': recv_window,
        'timestamp': timestamp
    }
    body = json.dumps(params)
    signature_payload = f"{timestamp}POST{REQUEST_PATH}{body}".encode('utf-8')
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
            print(f"Limit order placed successfully: {data}")
        else:
            print(f"Failed to place limit order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")


if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    best_bid_price = fetch_bid(CONTRACT_NAME)
    price_precision = get_price_precision(CONTRACT_NAME)
    if best_bid_price:
        for discount in range(1, 11):
            limit_price = round(best_bid_price * (1 - discount / 100), price_precision)
            print(f"Placing limit order at {discount}% discount: Price = {limit_price}")
            place_limit_order(api_key, secret_key, contract_name=CONTRACT_NAME, volume=VOLUME, price=limit_price, side=SIDE, open_position=OPEN_POSITION, position_type=POSITION_TYPE, recv_window=RECV_WINDOW)
            time.sleep(1)  # Optional delay between orders to avoid rate limit issues
