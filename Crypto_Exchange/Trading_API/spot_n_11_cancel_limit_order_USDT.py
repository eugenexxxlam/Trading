import requests
import json
import hashlib
import hmac
import time
import logging

# Constants
BASE_URL = "https://openapi.gaiaex.com"
CONFIG_FILE = 'UID32751170.json' 
# CONFIG_FILE = "UID32937591.json"
CANCEL_ORDER_PATH = "/sapi/v1/cancel"

# Logging configuration
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")


def load_config(file_path):
    """Load API credentials from a JSON file."""
    try:
        with open(file_path, "r") as file:
            return json.load(file)
    except FileNotFoundError:
        logging.error("Configuration file not found: %s", file_path)
        return None
    except json.JSONDecodeError:
        logging.error("Error decoding JSON from configuration file: %s", file_path)
        return None


def generate_signature(secret_key, payload):
    """Generate HMAC SHA256 signature."""
    return hmac.new(secret_key.encode(), payload.encode("utf-8"), hashlib.sha256).hexdigest()


def create_headers(api_key, secret_key, payload):
    """Create request headers with the correct signature."""
    timestamp = str(int(time.time() * 1000))
    signature_payload = f"{timestamp}POST{CANCEL_ORDER_PATH}{payload}"
    signature = generate_signature(secret_key, signature_payload)

    return {
        "X-CH-APIKEY": api_key,
        "X-CH-SIGN": signature,
        "X-CH-TS": timestamp,
        "Content-Type": "application/json",
    }


def cancel_order(api_key, secret_key, order_id, symbol):
    """Cancel a specific order using the symbol field."""
    timestamp = str(int(time.time() * 1000))
    payload = json.dumps(
        {
            "orderId": order_id,
            "symbol": symbol,
            "timestamp": timestamp,
            "recvWindow": 5000,
        },
        indent=4,
    )

    headers = create_headers(api_key, secret_key, payload)
    url = f"{BASE_URL}{CANCEL_ORDER_PATH}"

    logging.info("Sending request to %s", url)
    logging.info("Request Headers: %s", json.dumps(headers, indent=4))
    logging.info("JSON Payload:\n%s", payload)

    response = requests.post(url, headers=headers, data=payload)

    try:
        response_data = response.json()
    except json.JSONDecodeError:
        response_data = {"error": "Invalid JSON response", "raw_response": response.text}

    logging.info("API Response:\n%s", json.dumps(response_data, indent=4))

    if response.status_code == 200:
        logging.info("Order %s cancelled successfully.", order_id)
    else:
        logging.error("Failed to cancel order %s. API response: %s", order_id, response.text)


if __name__ == "__main__":
    credentials = load_config(CONFIG_FILE)
    if not credentials:
        exit(1)

    api_key = credentials.get("GAIAEX_API_KEY", "").strip()
    secret_key = credentials.get("GAIAEX_SECRET_KEY", "").strip()

    if not api_key or not secret_key:
        logging.error("API Key or Secret Key is missing. Check your configuration file.")
        exit(1)

    order_ids = [
        ("1111", "usdc1802usdt1802"),
        # ("2625041525564385985", "usdc1802usdt1802"),
    ]

    for order_id, symbol in order_ids:
        logging.info("Cancelling order %s for symbol %s...", order_id, symbol)
        cancel_order(api_key, secret_key, order_id, symbol)
