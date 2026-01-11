import requests
import datetime

def fetch_recent_trades(symbol, limit=100):
    url = 'https://openapi.gaiaex.com/sapi/v1/trades'  # URL for recent trades endpoint
    params = {
        'symbol': symbol,
        'limit': limit
    }

    try:
        response = requests.get(url, params=params)
        if response.status_code == 200:
            data = response.json().get('list', [])
            print(f"Recent Trades for {symbol}:")
            for trade in data:
                price = trade.get('price')
                qty = trade.get('qty')
                timestamp = trade.get('time')
                side = trade.get('side')
                trade_time = datetime.datetime.fromtimestamp(timestamp / 1000.0)
                print(f"Time: {trade_time}, Side: {side}, Price: {price}, Quantity: {qty}")
        else:
            print(f"Failed to fetch recent trades. Status Code: {response.status_code}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    symbol = 'XRPUSDT'  # Example symbol
    fetch_recent_trades(symbol)
