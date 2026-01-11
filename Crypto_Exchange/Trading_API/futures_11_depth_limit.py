import requests

BASE_URL = 'https://futuresopenapi.gaiaex.com'
MARKET_DEPTH_PATH = '/fapi/v1/depth'


def fetch_market_depth(contract_name, limit=100):
    url = BASE_URL + MARKET_DEPTH_PATH
    params = {'contractName': contract_name, 'limit': limit}
    response = requests.get(url, params=params)
    if response.status_code == 200:
        data = response.json()
        print("Bids:")
        for bid in data.get('bids', []):
            print(f"Price: {bid[0]}, Volume: {bid[1]}")
        print("Asks:")
        for ask in data.get('asks', []):
            print(f"Price: {ask[0]}, Volume: {ask[1]}")
    else:
        print("Failed to fetch market depth:", response.text)


if __name__ == '__main__':
    fetch_market_depth('USDT-BTC-USDT')
