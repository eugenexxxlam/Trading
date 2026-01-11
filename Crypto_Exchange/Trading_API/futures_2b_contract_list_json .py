import requests
import json

def get_contract_list():
    url = 'https://futuresopenapi.gaiaex.com/fapi/v1/contracts'  # URL for getting contract list
    try:
        response = requests.get(url)
        if response.status_code == 200:
            data = response.json()
            symbol_list = []
            for contract in data:
                symbol_list.append(contract['symbol'])

            # Save the symbols list to a JSON file
            with open('contract_symbols.json', 'w') as json_file:
                json.dump(symbol_list, json_file, indent=4)

            print("All symbols have been saved to 'contract_symbols.json'")
        else:
            print(f"Failed to fetch contract list. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    # Fetch and save all contract symbols into a JSON file
    get_contract_list()
