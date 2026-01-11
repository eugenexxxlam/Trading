import requests

def fetch_pairs_list():
    url = 'https://openapi.gaiaex.com/sapi/v1/symbols'  # URL to fetch pairs list
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        for symbol in symbols:
            print(f"Symbol: {symbol['symbol']}, Base Asset: {symbol['baseAsset']}, Quote Asset: {symbol['quoteAsset']}, "
                  f"Price Precision: {symbol['pricePrecision']}, Quantity Precision: {symbol['quantityPrecision']}")
    else:
        print("Failed to fetch pairs list:", response.text)

if __name__ == '__main__':
    fetch_pairs_list()
