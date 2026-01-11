import requests

def fetch_tradable_symbols():
    """Fetch trading pairs and format them into tradable symbol names."""
    url = 'https://openapi.gaiaex.com/sapi/v1/symbols'  # URL to fetch pairs list
    response = requests.get(url)
    
    if response.status_code == 200:
        data = response.json()
        symbols = data.get('symbols', [])
        tradable_symbols = [
            f"{symbol['baseAsset']}/{symbol['quoteAsset']}"  # Convert to 'Base/Quote' format
            for symbol in symbols
            if symbol['quoteAsset'].upper() == 'USDT'  # Filter for USDT pairs only (if needed)
        ]
        return tradable_symbols
    else:
        print("Failed to fetch pairs list:", response.text)
        return []

if __name__ == '__main__':
    tradable_list = fetch_tradable_symbols()
    print("Tradable Symbols:")
    for symbol_name in tradable_list:
        print(symbol_name)



# ETH/USDT
# BNB/USDT
# SOLANA/USDT
# DOT/USDT
# ADA/USDT
# AVAX/USDT
# DOGE/USDT
# LTCBSC/USDT
# XLM/USDT
# EOS/USDT
# XRP/USDT
# PEPE/USDT
# FIL/USDT
# TRX/USDT
# LINK/USDT
# ETCBEP20/USDT
# AAVE/USDT
# ARB/USDT
# TON/USDT
# SHIB/USDT
# ENS/USDT
# LDO/USDT
# WIF/USDT
# USDC/USDT
# ONDO/USDT
# CRV/USDT
# FTM/USDT
# BCHBSC/USDT
# FDUSD/USDT
# BONK/USDT
# ORDIBRC20/USDT
# FLOKI/USDT
# BOME/USDT
# JUP/USDT
# NOT/USDT
# WLD1/USDT
# POL/USDT
# DAI1/USDT
# BTC3L/USDT
# BTC3S/USDT
# ETH3L/USDT
# ETH3S/USDT
# USDT/USDT
# BTC/USDT