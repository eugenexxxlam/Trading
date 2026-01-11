import requests
import datetime

def fetch_recent_trades(symbol, limit=100):
    url = 'https://openapi.gaiaex.com/sapi/v1/trades'  # Updated URL for recent trades endpoint
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

def fetch_kline_data(symbol, interval, limit=100, start_time=None, end_time=None):
    url = 'https://openapi.gaiaex.com/sapi/v1/klines'  # Updated URL for kline/candlestick data endpoint
    params = {
        'symbol': symbol,
        'interval': interval,
        'limit': limit
    }
    if start_time:
        params['startTime'] = start_time
    if end_time:
        params['endTime'] = end_time

    try:
        response = requests.get(url, params=params)
        if response.status_code == 200:
            data = response.json()
            print(f"Kline/Candlestick Data for {symbol} ({interval} interval):")
            for kline in data:
                open_time = datetime.datetime.fromtimestamp(kline['idx']) if 'idx' in kline else datetime.datetime.now()
                open_price = float(kline['open'])
                high_price = float(kline['high'])
                low_price = float(kline['low'])
                close_price = float(kline['close'])
                volume = float(kline['vol'])
                print(f"Time: {open_time}, Open: {open_price}, High: {high_price}, Low: {low_price}, Close: {close_price}, Volume: {volume}")
        else:
            print(f"Failed to fetch kline data. Status Code: {response.status_code}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")
    except ValueError as ve:
        print(f"Value error occurred while processing kline data: {ve}")

if __name__ == '__main__':
    symbol = 'BTCUSDT'  # Example symbol
    fetch_recent_trades(symbol)
    intervals = ['1min', '5min', '15min', '30min', '60min', '1day']
    for interval in intervals:
        fetch_kline_data(symbol, interval)  # Fetch kline data for all intervals
