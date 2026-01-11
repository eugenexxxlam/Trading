import requests
from prettytable import PrettyTable

# Constants for user customization
BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API

def fetch_pairs_list():
    url = 'https://openapi.gaiaex.com/sapi/v1/symbols'  # URL to fetch pairs list
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        pairs_dict = {}
        for symbol in symbols:
            pairs_dict[symbol['symbol']] = {
                'Base Asset': symbol['baseAsset'],
                'Quote Asset': symbol['quoteAsset'],
                'Price Precision': symbol['pricePrecision'],
                'Quantity Precision': symbol['quantityPrecision'],
                'Spread': 'N/A'  # Dummy spread for now
            }
        return pairs_dict
    else:
        print("Failed to fetch pairs list:", response.text)
        return {}

def fetch_spread(symbol):
    depth_url = f"{BASE_URL}/sapi/v1/depth"
    params = {'symbol': symbol, 'limit': 5}
    
    try:
        response = requests.get(depth_url, params=params)
        if response.status_code == 200:
            data = response.json()
            if data.get('bids') and data.get('asks'):
                best_bid = float(data['bids'][0][0])
                best_ask = float(data['asks'][0][0])
                spread = best_ask - best_bid
                return spread
            else:
                print(f"Symbol: {symbol}, No bids or asks available")
                return 'N/A'
        else:
            print(f"Failed to fetch market depth for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
            return 'N/A'
    except requests.RequestException as e:
        print(f"An error occurred while fetching market depth for {symbol}: {e}")
        return 'N/A'

if __name__ == '__main__':
    pairs_dict = fetch_pairs_list()
    
    # Update spreads for each pair
    for symbol in pairs_dict.keys():
        spread = fetch_spread(symbol)
        pairs_dict[symbol]['Spread'] = spread
    
    # Sort pairs by spread in descending order (numeric spreads first, then 'N/A' at the bottom)
    sorted_pairs = dict(sorted(pairs_dict.items(), key=lambda item: (float('-inf') if item[1]['Spread'] == 'N/A' else item[1]['Spread']), reverse=True))
    
    # Create a PrettyTable to display the symbols and their details
    table = PrettyTable()
    table.field_names = ["Symbol", "Base Asset", "Quote Asset", "Price Precision", "Quantity Precision", "Spread"]
    
    for symbol, details in sorted_pairs.items():
        table.add_row([symbol, details['Base Asset'], details['Quote Asset'], details['Price Precision'], details['Quantity Precision'], details['Spread']])
    
    print(table)