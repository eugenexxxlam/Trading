import requests
import json

def load_config(config_file):
    with open(config_file, 'r') as file:
        return json.load(file)

def get_contract_list():
    url = 'https://futuresopenapi.gaiaex.com/fapi/v1/contracts'  # URL for getting contract list
    try:
        response = requests.get(url)
        if response.status_code == 200:
            data = response.json()
            print("Contract List:")
            for contract in data:
                print(f"Symbol: {contract['symbol']}, Status: {contract['status']}, Type: {contract['type']}, Min Order Volume: {contract['minOrderVolume']}, Max Market Volume: {contract['maxMarketVolume']}")
        else:
            print(f"Failed to fetch contract list. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    # Fetch and print all contract list
    get_contract_list()
