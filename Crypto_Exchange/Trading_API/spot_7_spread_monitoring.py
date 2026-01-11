import requests
import time

# Constants for user customization
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API


def fetch_pairs_list():
    url = f"{BASE_URL}/sapi/v1/symbols"  # URL to fetch pairs list
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        return [symbol['symbol'] for symbol in symbols]
    else:
        print("Failed to fetch pairs list:", response.text)
        return []


def fetch_and_print_bid_ask_spread(symbol, interval=1):
    depth_url = f"{BASE_URL}/sapi/v1/depth"
    params = {'symbol': symbol, 'limit': 5}
    
    try:
        response = requests.get(depth_url, params=params)
        if response.status_code == 200:
            data = response.json()
            if data.get('bids') and data.get('asks'):
                best_bid = float(data['bids'][0][0])
                best_ask = float(data['asks'][0][0])
                spread = best_ask - best_bid
                print(f"Symbol: {symbol}, Best Bid: {best_bid}, Best Ask: {best_ask}, Spread: {spread}")
            else:
                print(f"Symbol: {symbol}, No bids or asks available")
        else:
            print(f"Failed to fetch market depth for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while fetching market depth for {symbol}: {e}")


if __name__ == '__main__':
    symbols = fetch_pairs_list()
    while True:
        for symbol in symbols:
            fetch_and_print_bid_ask_spread(symbol)
        time.sleep(1)  # Delay between each cycle to avoid rate limit issues
