import requests
import ccxt
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
                'Spread': 'N/A',  # Dummy spread for now
                'Standard Symbol': standardise_symbol(symbol['symbol']),  # Standardised symbol
                'Binance Spread': 'N/A',
                'OKX Spread': 'N/A',
                'Bybit Spread': 'N/A',
                'Binance Spread Ratio': 'N/A',
                'OKX Spread Ratio': 'N/A',
                'Bybit Spread Ratio': 'N/A'
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
                return round(spread, 4)
            else:
                print(f"Symbol: {symbol}, No bids or asks available")
                return 'N/A'
        else:
            print(f"Failed to fetch market depth for {symbol}. Status Code: {response.status_code}, Response: {response.text}")
            return 'N/A'
    except requests.RequestException as e:
        print(f"An error occurred while fetching market depth for {symbol}: {e}")
        return 'N/A'

def fetch_exchange_spread(exchange, symbol):
    try:
        order_book = exchange.fetch_order_book(symbol)
        if order_book['bids'] and order_book['asks']:
            best_bid = float(order_book['bids'][0][0])
            best_ask = float(order_book['asks'][0][0])
            spread = best_ask - best_bid
            return round(spread, 4)
        else:
            return 'N/A'
    except Exception as e:
        print(f"An error occurred while fetching spread for {symbol} on {exchange.name}: {e}")
        return 'N/A'

def standardise_symbol(symbol):
    # Remove '1802' from symbol and capitalise it
    standardised_symbol = symbol.replace('1802', '').upper()
    # Add '/' before 'USDT'
    if standardised_symbol.endswith('USDT'):
        standardised_symbol = standardised_symbol[:-4] + '/USDT'
    return standardised_symbol

def calculate_spread_ratio(gaiaex_spread, exchange_spread):
    if gaiaex_spread != 'N/A' and exchange_spread != 'N/A' and exchange_spread != 0:
        return round(gaiaex_spread / exchange_spread, 2)
    return 'N/A'

if __name__ == '__main__':
    pairs_dict = fetch_pairs_list()
    
    # Initialize exchanges
    binance = ccxt.binance()
    okx = ccxt.okx()
    bybit = ccxt.bybit()
    
    # Update spreads for each pair
    for symbol in pairs_dict.keys():
        spread = fetch_spread(symbol)
        pairs_dict[symbol]['Spread'] = spread
        
        standard_symbol = pairs_dict[symbol]['Standard Symbol']
        binance_spread = fetch_exchange_spread(binance, standard_symbol)
        okx_spread = fetch_exchange_spread(okx, standard_symbol)
        bybit_spread = fetch_exchange_spread(bybit, standard_symbol)
        
        pairs_dict[symbol]['Binance Spread'] = binance_spread
        pairs_dict[symbol]['OKX Spread'] = okx_spread
        pairs_dict[symbol]['Bybit Spread'] = bybit_spread
        
        # Calculate spread ratios
        pairs_dict[symbol]['Binance Spread Ratio'] = calculate_spread_ratio(spread, binance_spread)
        pairs_dict[symbol]['OKX Spread Ratio'] = calculate_spread_ratio(spread, okx_spread)
        pairs_dict[symbol]['Bybit Spread Ratio'] = calculate_spread_ratio(spread, bybit_spread)
    
    # Sort pairs by Binance Spread Ratio in descending order (numeric ratios first, then 'N/A' at the bottom)
    sorted_pairs = dict(sorted(pairs_dict.items(), key=lambda item: (float('-inf') if item[1]['Binance Spread Ratio'] == 'N/A' else item[1]['Binance Spread Ratio']), reverse=True))
    
    # Create a PrettyTable to display the symbols and their details
    table = PrettyTable()
    table.field_names = ["Symbol", "Base Asset", "Quote Asset", "Spread", "Standard Symbol", "Binance Spread", "OKX Spread", "Bybit Spread", "Binance Spread Ratio", "OKX Spread Ratio", "Bybit Spread Ratio"]
    
    for symbol, details in sorted_pairs.items():
        table.add_row([
            symbol, 
            details['Base Asset'], 
            details['Quote Asset'], 
            round(details['Spread'], 4) if details['Spread'] != 'N/A' else details['Spread'], 
            details['Standard Symbol'], 
            round(details['Binance Spread'], 4) if details['Binance Spread'] != 'N/A' else details['Binance Spread'], 
            round(details['OKX Spread'], 4) if details['OKX Spread'] != 'N/A' else details['OKX Spread'], 
            round(details['Bybit Spread'], 4) if details['Bybit Spread'] != 'N/A' else details['Bybit Spread'], 
            details['Binance Spread Ratio'], 
            details['OKX Spread Ratio'], 
            details['Bybit Spread Ratio']
        ])
    
    print(table)