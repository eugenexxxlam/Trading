import requests

def check_server_time():
    url = 'https://openapi.gaiaex.com/sapi/v1/time'  # URL for checking server time
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        print(f"Server Time: {data['serverTime']} (Timezone: {data['timezone']})")
    else:
        print("Failed to fetch server time:", response.text)

if __name__ == '__main__':
    check_server_time()
