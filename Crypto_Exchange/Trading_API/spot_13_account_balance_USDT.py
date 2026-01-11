import requests
import json
import hashlib
import hmac
import time
import base64
from prettytable import PrettyTable
import threading
import pandas as pd
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization

BASE_URL = 'https://openapi.gaiaex.com'  # Base URL for Spot API
CONFIG_FILE = 'admin.json'
POSITION_PATH = '/v1/inner/get_position_list'

# Load API credentials and private key from a configuration file
def load_config(config_file):
    with open(config_file, 'r') as file:
        config = json.load(file)
        return config

def load_private_key(pem_file):
    with open(pem_file, 'rb') as key_file:
        private_key = serialization.load_pem_private_key(
            key_file.read(),
            password=None
        )
    return private_key

# Generate RSA signature
def generate_rsa_signature(data, private_key):
    signature = private_key.sign(
        data.encode(),
        padding.PKCS1v15(),
        hashes.SHA256()
    )
    return signature

# Fetch position list for a merchant-level account
def fetch_position_list(api_key, private_key, broker_id, uid):
    full_url = BASE_URL + POSITION_PATH
    timestamp = str(int(time.time() * 1000))
    body = {
        "brokerId": broker_id,
        "uid": uid,
        "originUid": uid,  # Assuming originUid is the same as uid
        "pageNum": 1,
        "pageSize": 10
    }
    data_to_sign = json.dumps(body) + timestamp
    rsa_signature = generate_rsa_signature(data_to_sign, private_key)
    signature = base64.b64encode(rsa_signature).decode()

    headers = {
        'X-CH-APIKEY': api_key,
        'X-CH-TS': timestamp,
        'Ex-sign': signature,
        'Ex-ts': timestamp
    }

    try:
        response = requests.post(full_url, json=body, headers=headers)
        if response.status_code == 200:
            data = response.json()
            print("Position List:", data)
            return data
        else:
            print(f"Failed to fetch position list. Status Code: {response.status_code}, Response: {response.text}")
    except requests.RequestException as e:
        print(f"An error occurred: {e}")

    return None

if __name__ == '__main__':
    config = load_config(CONFIG_FILE)
    api_key = config['GAIAEX_API_KEY']
    private_key = load_private_key('admin.pem')
    broker_id = "1802"  # Replace with actual broker ID
    uid = "3275110"  # Replace with actual UID

    # Fetching position list
    fetch_position_list(api_key, private_key, broker_id, uid)
