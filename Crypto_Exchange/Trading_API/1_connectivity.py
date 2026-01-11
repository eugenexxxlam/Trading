import requests

def test_spot_server_connectivity():
    url = 'https://openapi.gaiaex.com/sapi/v1/ping'  # Update this URL with the correct domain
    try:
        response = requests.get(url)
        if response.status_code == 200:
            print("Successfully connected to the spot server.")
        else:
            print(f"Failed to connect to the spot server. Status Code: {response.status_code}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    test_spot_server_connectivity()
