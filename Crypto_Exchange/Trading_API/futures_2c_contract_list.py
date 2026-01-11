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
            
            # Print the complete JSON response
            print("Complete Contract List JSON:")
            print(json.dumps(data, indent=4, ensure_ascii=False))
            
            # Optional: Also print a summary
            print(f"\nTotal contracts found: {len(data)}")
            
            # Optional: Print specific contract details in a readable format
            print("\nContract Summary:")
            for contract in data:
                print(f"Symbol: {contract['symbol']}")
                print(f"  Multiplier: {contract.get('multiplier', 'N/A')}")
                print(f"  Multiplier Coin: {contract.get('multiplierCoin', 'N/A')}")
                print(f"  Status: {contract['status']}")
                print(f"  Type: {contract['type']}")
                print(f"  Min Order Volume: {contract['minOrderVolume']}")
                print(f"  Max Market Volume: {contract['maxMarketVolume']}")
                print(f"  Price Precision: {contract.get('pricePrecision', 'N/A')}")
                print("-" * 50)
                
        else:
            print(f"Failed to fetch contract list. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

def save_contract_list_to_file(filename='contracts.json'):
    """Save the contract list to a JSON file for later use"""
    url = 'https://futuresopenapi.gaiaex.com/fapi/v1/contracts'
    try:
        response = requests.get(url)
        if response.status_code == 200:
            data = response.json()
            with open(filename, 'w') as f:
                json.dump(data, f, indent=4, ensure_ascii=False)
            print(f"Contract list saved to {filename}")
            return data
        else:
            print(f"Failed to fetch contract list. Status Code: {response.status_code}")
            return None
    except requests.RequestException as e:
        print(f"An error occurred: {e}")
        return None

if __name__ == '__main__':
    # Fetch and print complete contract list
    get_contract_list()
    
    # Optionally save to file
    # save_contract_list_to_file()
