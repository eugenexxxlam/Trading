import requests

def fetch_24hr_ticker(symbol):
    url = 'https://openapi.gaiaex.com/sapi/v1/ticker'  # Updated URL for the 24hr ticker endpoint
    params = {'symbol': symbol}
    
    response = requests.get(url, params=params)
    
    if response.status_code == 200:
        data = response.json()
        print("24 Hour Price Change Statistics:")
        print(f"Symbol: {symbol}")
        print(f"High Price: {data.get('high')}")
        print(f"Low Price: {data.get('low')}")
        print(f"Open Price: {data.get('open')}")
        print(f"Last Price: {data.get('last')}")
        print(f"Volume: {data.get('vol')}")
        print(f"Price Increase: {data.get('rose')}")
        print(f"Timestamp: {data.get('time')}")
    else:
        print("Failed to fetch ticker data:", response.text)

if __name__ == '__main__':
    symbol = 'ETHUSDT'  # Example symbol
    fetch_24hr_ticker(symbol)
