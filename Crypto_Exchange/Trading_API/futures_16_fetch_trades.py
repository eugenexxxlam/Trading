import requests
import json
import hashlib
import hmac
import time
import pandas as pd

BASE_URL = 'https://futuresopenapi.gaiaex.com'
CONFIG_FILE = 'UID32937591.json'
MY_TRADES_PATH = '/fapi/v1/myTrades'

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def fetch_my_trades(api_key, secret_key, contract_name, limit=100, from_id=None):
    full_url = BASE_URL + MY_TRADES_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'limit': limit
    }
    if from_id:
        params['fromId'] = from_id
    query_string = '&'.join([f"{key}={value}" for key, value in params.items()])
    signature_payload = f"{timestamp}GET{MY_TRADES_PATH}?{query_string}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp)
    }

    try:
        response = requests.get(full_url, headers=headers, params=params)
        if response.status_code == 200:
            data = response.json()
            print("My trades fetched successfully")
            return data
        else:
            print(f"Failed to fetch my trades. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetching my trades
    my_trades = fetch_my_trades(api_key, secret_key, contract_name='USDT1802-BTC-USDT')
    
    # Convert my trades to pandas DataFrame if data is available
    if my_trades:
        df = pd.DataFrame(my_trades)
        print(df)
        
        # Exporting DataFrame to Excel
        excel_file_path = 'my_trades.xlsx'
        df.to_excel(excel_file_path, index=False)
        print(f"My trades exported to {excel_file_path}")
