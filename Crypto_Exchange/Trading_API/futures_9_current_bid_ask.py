import requests
import time

BASE_URL = 'https://futuresopenapi.gaiaex.com'
MARKET_DEPTH_PATH = '/fapi/v1/depth'
# MARKET_DEPTH_PATH = '/fapi/v1/klines'


def fetch_bid_ask(contract_name, limit=100):
    url = BASE_URL + MARKET_DEPTH_PATH
    params = {'contractName': contract_name, 'limit': limit}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        if 'bids' in data and 'asks' in data:
            best_bid = data['bids'][0] if data['bids'] else None
            best_ask = data['asks'][0] if data['asks'] else None
            print(f"Best Bid: Price = {best_bid[0]}, Volume = {best_bid[1]}") if best_bid else print("No bid data available")
            print(f"Best Ask: Price = {best_ask[0]}, Volume = {best_ask[1]}") if best_ask else print("No ask data available")
        else:
            print("No bid/ask data available")
    else:
        print("Failed to fetch market depth:", response.text)


if __name__ == '__main__':
    while True:
        fetch_bid_ask('USDT-TON-USDT')
        time.sleep(0.3)