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


def fetch_and_print_recent_trades(symbol, limit=5):
    trades_url = f"{BASE_URL}/api/v1/trades"
    params = {'symbol': symbol, 'limit': limit}
    
    try:
        response = requests.get(trades_url, params=params)
        if response.status_code == 200:
            trades = response.json()
            for trade in trades:
                price = float(trade['price'])
                qty = float(trade['qty'])
                trade_time = trade['time']
                side = trade['side']
                print(f"Symbol: {symbol}, Price: {price}, Quantity: {qty}, Time: {trade_time}, Side: {side}")
        else:
            print(f"Failed to fetch recent trades for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred while fetching recent trades for {symbol}: {e}")


if __name__ == '__main__':
    symbols = fetch_pairs_list()
    while True:
        for symbol in symbols:
            fetch_and_print_recent_trades(symbol)
        time.sleep(1)  # Delay between each cycle to avoid rate limit issues
