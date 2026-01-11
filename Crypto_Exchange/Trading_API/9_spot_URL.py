import requests
import json

def fetch_recent_trades(symbol, limit=1):
    url = 'https://openapi.gaiaex.com/sapi/v1/trades'  # API endpoint for recent trades
    params = {
        'symbol': symbol,  # Trading pair (e.g., BTCUSDT)
        'limit': limit     # Number of recent trades to fetch
    }
    
    response = requests.get(url, params=params)
    
    if response.status_code == 200:
        print(json.dumps(response.json(), indent=4))  # Print raw JSON response with formatting
    else:
        print("Failed to fetch recent trades:", response.text)  # Print error response

if __name__ == '__main__':
    symbol = 'BTCUSDT'  # Example trading pair
    fetch_recent_trades(symbol)
