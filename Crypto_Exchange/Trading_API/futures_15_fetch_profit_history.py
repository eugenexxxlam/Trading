import requests
import json
import hashlib
import hmac
import time
import pandas as pd

BASE_URL = 'https://futuresopenapi.gaiaex.com'
CONFIG_FILE = 'UID32937591.json'
PROFIT_HISTORY_PATH = '/fapi/v1/profitHistorical'


def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)


def fetch_profit_history(api_key, secret_key, contract_name, limit=100, from_id=None):
    full_url = BASE_URL + PROFIT_HISTORY_PATH
    timestamp = int(time.time() * 1000)
    params = {
        'contractName': contract_name,
        'limit': limit
    }
    if from_id:
        params['fromId'] = from_id
    body = json.dumps(params)
    signature_payload = f"{timestamp}POST{PROFIT_HISTORY_PATH}{body}".encode('utf-8')
    signature = hmac.new(secret_key.encode(), signature_payload, hashlib.sha256).hexdigest()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-SIGN': signature,
        'X-CH-TS': str(timestamp),
        'Content-Type': 'application/json'
    }

    try:
        response = requests.post(full_url, headers=headers, data=body)
        if response.status_code == 200:
            data = response.json()
            print("Profit history fetched successfully")
            return data
        else:
            print(f"Failed to fetch profit history. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")


if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    secret_key = config['GAIAEX_SECRET_KEY']
    
    # Fetching profit history
    profit_history = fetch_profit_history(api_key, secret_key, contract_name='USDT1802-BTC-USDT')
    
    # Convert profit history to pandas DataFrame if data is available
    if profit_history:
        df = pd.DataFrame(profit_history)
        print(df)
        
        # Exporting DataFrame to Excel
        excel_file_path = 'profit_history.xlsx'
        df.to_excel(excel_file_path, index=False)
        print(f"Profit history exported to {excel_file_path}")
