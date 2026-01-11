import requests

def fetch_market_depth(symbol, limit=100):
    url = 'https://openapi.gaiaex.com/sapi/v1/depth'  # URL for the market depth endpoint
    params = {
        'symbol': symbol,
        'limit': limit
    }
    
    response = requests.get(url, params=params)
    
    if response.status_code == 200:
        data = response.json()
        print(f"Current Timestamp: {data.get('time')}")
        print("Bids:")
        for bid in data.get('bids', []):
            print(f"Price: {bid[0]}, Volume: {bid[1]}")
        print("Asks:")
        for ask in data.get('asks', []):
            print(f"Price: {ask[0]}, Volume: {ask[1]}")
    else:
        print("Failed to fetch market depth:", response.text)

if __name__ == '__main__':
    symbol = 'BTCUSDT'  # Example symbol
    fetch_market_depth(symbol)
