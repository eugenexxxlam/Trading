import requests
import json

def fetch_market_depth(symbol, limit=2):
    url = 'https://openapi.gaiaex.com/sapi/v1/depth'  # URL for the market depth endpoint
    params = {
        'symbol': symbol,
        'limit': limit
    }
    
    response = requests.get(url, params=params)
    
    if response.status_code == 200:
        print(json.dumps(response.json(), indent=4))  # Print raw JSON with indentation
    else:
        print(response.text)  # Print the raw error response

if __name__ == '__main__':
    symbol = 'BTCUSDT'  # Example symbol
    fetch_market_depth(symbol)
