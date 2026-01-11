import requests
import time

# Constants for user customization
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
SYMBOL = 'btcusdt'  # Updated symbol from the pairs list


def fetch_and_print_bid_ask_spread(symbol, interval=1):
    depth_url = f"{BASE_URL}/sapi/v1/depth"
    params = {'symbol': symbol, 'limit': 5}
    
    while True:
        try:
            response = requests.get(depth_url, params=params)
            if response.status_code == 200:
                data = response.json()
                if data.get('bids') and data.get('asks'):
                    best_bid = float(data['bids'][0][0])
                    best_ask = float(data['asks'][0][0])
                    spread = best_ask - best_bid
                    timestamp = time.strftime("[%Y-%m-%d %H:%M:%S]")
                    print(f"{timestamp} Best Bid: {best_bid}, Best Ask: {best_ask}, Spread: {spread}")
                else:
                    timestamp = time.strftime("[%Y-%m-%d %H:%M:%S]")
                    print(f"{timestamp} No bids or asks available")
            else:
                timestamp = time.strftime("[%Y-%m-%d %H:%M:%S]")
                print(f"{timestamp} Failed to fetch market depth. Status Code: {response.status_code}, Response: {response.text}")
        except requests.RequestException as e:
            timestamp = time.strftime("[%Y-%m-%d %H:%M:%S]")
            print(f"{timestamp} An error occurred while fetching market depth: {e}")
        
        time.sleep(interval)


if __name__ == '__main__':
    # Fetch and print bid, ask, and spread every second
    fetch_and_print_bid_ask_spread(SYMBOL)
