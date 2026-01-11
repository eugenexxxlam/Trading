import requests
import json
import hashlib
import hmac
import time
from urllib.parse import urlencode

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def place_limit_order(api_key, secret_key, symbol, volume, price, side, order_type='LIMIT', recv_window=5000):
    url = '/sapi/v1/order'  # Request path for placing a new order
    base_url = 'https://openapi.gaiaex.com'  # Base URL for API
    full_url = base_url + url
    timestamp = int(time.time() * 1000)
    params = {
        'symbol': symbol,
        'volume': volume,
        'side': side,
        'type': order_type,
        'price': price,
        'recvwindow': recv_window,
        'timestamp': timestamp
    }
    body = json.dumps(params)  # Body for the POST request

    # Creating the signature
    signature_payload = f"{timestamp}POST{url}{body}"
    signature = hmac.new(secret_key.encode(), signature_payload.encode(), hashlib.sha256).hexdigest()
    
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
            print(f"Order placed successfully: {data}")
        else:
            print(f"Failed to place order. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config('config_gaiaex_testing.json')
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Example limit order placement
    place_limit_order(api_key, secret_key, symbol='E-BTC-USDT', volume=0.01, price=72000, side='BUY')
