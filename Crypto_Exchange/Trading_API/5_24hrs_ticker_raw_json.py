import requests
import json

def fetch_24hr_ticker(symbol):
    url = 'https://openapi.gaiaex.com/sapi/v1/ticker'  # URL for the 24hr ticker endpoint
    params = {'symbol': symbol}
    
    response = requests.get(url, params=params)
    
    if response.status_code == 200:
        print(json.dumps(response.json(), indent=4))  # Print raw JSON with indentation
    else:
        print(response.text)  # Print the raw error response

if __name__ == '__main__':
    symbol = 'ETHUSDT'  # Example symbol
    fetch_24hr_ticker(symbol)
