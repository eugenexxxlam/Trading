# Crypto Exchange Trading System

Python-based cryptocurrency trading automation system for GAIA Exchange with REST API and WebSocket support for spot and futures trading.

## Overview

This system provides comprehensive trading tools for cryptocurrency exchanges, featuring real-time market data monitoring, automated trading strategies, position management, and portfolio liquidation capabilities.

## Exchange Details

**Platform:** GAIA Exchange (gaiaex.com)  
**API Documentation:** [Official GAIA Exchange API Docs](https://gaia-4.gitbook.io/gaiaex_api/)  
**Authentication:** HMAC SHA256 signature-based authentication  
**Supported Markets:** Spot Trading, Futures Trading

## Project Structure

```
Crypto_Exchange/
├── Trading_API/                     # REST API trading scripts
│   ├── 1_connectivity.py            # Test API connectivity
│   ├── 2_timestamp.py               # Check server time
│   ├── 3_pair_list.py               # Fetch trading pairs
│   ├── spot_*.py                    # Spot trading scripts (60+ files)
│   ├── futures_*.py                 # Futures trading scripts (17 files)
│   └── Spot_API_testing_development/# Advanced trading algorithms
│       ├── 1_fetch_account_info.py  # Account data fetching
│       ├── 2_random_trading.py      # Random asset trading
│       ├── 3_liquidation.py         # Position liquidation
│       ├── 4*_spot_trading_loop.py  # Trading loop variations
│       ├── 5_liquidation_20250427.py# Production liquidation
│       ├── 6_bulk_buy_20250427.py   # Bulk buying strategy
│       ├── 7_bulk_buy.py            # Enhanced bulk buy
│       ├── 8_liquidation.py         # Advanced liquidation
│       └── log/                     # Trading execution logs
└── websocket/                       # WebSocket real-time data
    ├── 1_testing_volume.py          # Real-time volume monitoring
    └── 2_price_update.py            # Real-time price updates
```

## Features

### 1. Basic API Operations

**Connectivity Testing**
- `1_connectivity.py` - Test connection to exchange API
- `2_timestamp.py` - Verify server time synchronization
- `3_pair_list.py` - Retrieve available trading pairs

### 2. Spot Trading

**Market Orders**
- `spot_1_market_order.py` - Place single market order
- `spot_3_market_order_gun.py` - Rapid-fire multiple market orders

**Limit Orders**
- `spot_4_limit_order.py` - Place limit orders with custom price
- `spot_12_cancel_all_limit_order.py` - Cancel all open limit orders

**Account Management**
- `spot_13_account_balance_USDT.py` - Check USDT balance
- `spot_17_fetch_accounts_info.py` - Fetch complete account information
- `spot_9_fetch_assts.py` to `spot_9i_fetch_assets_balance_dict.py` - Various asset fetching methods

**Market Data**
- `spot_5_market_depth.py` - Get order book depth
- `spot_6_bid_ask.py` - Fetch current bid/ask prices
- `spot_7_spread_monitoring.py` - Monitor bid-ask spreads across pairs
- `spot_8_fetch_trades.py` - Retrieve recent trade history

**Liquidation**
- `spot_19_liquidation.py` - Automatically sell all assets above threshold
- `spot_11_buy_liquadation.py` - Liquidate with buy orders

### 3. Futures Trading

**Position Management**
- `futures_1_market_order.py` - Open futures position with market order
- `futures_4_limit_order.py` - Open position with limit order
- `futures_12_close_position.py` - Close all open positions
- `futures_13_multiple_discount_limit_order.py` - Place multiple discounted limit orders

**Market Data**
- `futures_9_current_bid_ask.py` - Real-time futures bid/ask prices
- `futures_10_market_depth.py` - Futures order book depth
- `futures_11_depth_limit.py` - Order book with custom depth limit

**Order Management**
- `futures_5_fetching_order.py` - Check order status
- `futures_6_fetch_limit_order_list.py` - List all open limit orders
- `futures_7_cancel_order.py` - Cancel specific order
- `futures_8_cancel_all_limit_order.py` - Cancel all limit orders

**Account Information**
- `futures_14_fetch_order_history.py` - Historical orders
- `futures_15_fetch_profit_history.py` - P&L history
- `futures_16_fetch_trades.py` - Trade history
- `futures_17_fetch_accounts_info.py` - Complete account details

### 4. Automated Trading Algorithms

**Random Trading**
- `spot_18_random_trade_loop.py` - Continuous random asset trading loop
- `spot_21_dummy_trade.py` - Test trading without real execution

**Bulk Operations**
- `spot_20_chunk_trading.py` - Execute large orders in chunks
- `Spot_API_testing_development/6_bulk_buy_20250427.py` - Buy all assets in loop with randomized volumes
- `Spot_API_testing_development/7_bulk_buy.py` - Enhanced bulk buying with logging

**Advanced Strategies**
- `spot_22_trading_algo.py` - Main trading algorithm with balance monitoring
- `Spot_API_testing_development/4d_spot_trading_loop.py` - Full trading loop with automatic buying and periodic liquidation
- `Spot_API_testing_development/2_random_trading.py` - Random asset selection and trading

### 5. WebSocket Real-Time Data

**Volume Monitoring**
- `websocket/1_testing_volume.py` - Real-time trade volume tracking
- Subscribes to trade ticker channel
- Handles Gzip-compressed messages
- Automatic ping/pong for connection keep-alive

**Price Updates**
- `websocket/2_price_update.py` - Real-time bid/ask price updates
- Order book depth monitoring
- Spread calculation
- Configurable symbol selection

## Configuration

### API Credentials

All scripts require a JSON configuration file containing API credentials:

**Format (e.g., `UID32937591.json`):**
```json
{
  "GAIAEX_API_KEY": "your_api_key_here",
  "GAIAEX_SECRET_KEY": "your_secret_key_here"
}
```

### Authentication

HMAC SHA256 signature generation:
```python
signature_payload = f"{timestamp}POST{request_path}{body}"
signature = hmac.new(secret_key.encode(), signature_payload.encode('utf-8'), hashlib.sha256).hexdigest()
```

**Headers:**
- `X-CH-APIKEY`: Your API key
- `X-CH-SIGN`: HMAC SHA256 signature
- `X-CH-TS`: Request timestamp in milliseconds
- `Content-Type`: application/json

## Dependencies

```
requests
websocket-client
prettytable
pandas
logging
json
hashlib
hmac
time
threading
gzip
```

Install dependencies:
```bash
pip install requests websocket-client prettytable pandas
```

## Usage Examples

### 1. Test Connectivity
```bash
python Trading_API/1_connectivity.py
```

### 2. Place Spot Market Order
```bash
python Trading_API/spot_1_market_order.py
```

### 3. Monitor Real-Time Prices
```bash
python websocket/2_price_update.py
```

### 4. Liquidate All Assets
```bash
python Trading_API/spot_19_liquidation.py
```

### 5. Run Automated Trading Loop
```bash
python Trading_API/Spot_API_testing_development/4d_spot_trading_loop.py
```

### 6. Fetch Account Information
```bash
python Trading_API/Spot_API_testing_development/1_fetch_account_info.py
```

### 7. Execute Bulk Buying Strategy
```bash
python Trading_API/Spot_API_testing_development/7_bulk_buy.py
```

## Key Parameters

### Common Configuration Constants

**Spot Trading:**
- `BASE_URL`: 'https://openapi.gaiaex.com'
- `ORDER_TYPE`: 'MARKET' or 'LIMIT'
- `SIDE`: 'BUY' or 'SELL'
- `VOLUME`: Order size
- `THRESHOLD`: Minimum balance for operations (default: 1 USDT)

**Futures Trading:**
- `BASE_URL`: 'https://futuresopenapi.gaiaex.com'
- `CONTRACT_NAME`: e.g., 'USDT-TON-USDT'
- `OPEN_POSITION`: 'OPEN' or 'CLOSE'
- `POSITION_TYPE`: 1 (Cross) or 2 (Isolated)

**WebSocket:**
- `WS_URL`: 'wss://ws.gaiaex.com/kline-api/ws'
- Compression: Gzip

## API Endpoints

### Spot Trading
- **Connectivity:** `/sapi/v1/ping`
- **Server Time:** `/sapi/v1/time`
- **Symbols:** `/sapi/v1/symbols`
- **Account:** `/sapi/v1/account`
- **Order:** `/sapi/v1/order`
- **Depth:** `/sapi/v1/depth`
- **Trades:** `/sapi/v1/trades`
- **Kline:** `/sapi/v1/klines`

### Futures Trading
- **Account:** `/fapi/v1/account`
- **Order:** `/fapi/v1/order`
- **Contracts:** `/fapi/v1/contracts`
- **Depth:** `/fapi/v1/depth`

## Precision Handling

The system automatically fetches and applies precision rules for each trading pair:

```python
{
    'pricePrecision': 2,      # Decimal places for price
    'quantityPrecision': 8    # Decimal places for quantity
}
```

Volume truncation ensures compliance with exchange requirements:
```python
def truncate(volume, precision):
    factor = 10 ** precision
    return int(volume * factor) / factor
```

## Trading Strategies

### 1. Random Trading Loop
- Randomly selects assets from tradable pairs
- Places multiple market orders per asset
- Configurable volume and order count
- Automatic periodic liquidation in separate thread

### 2. Bulk Buying
- Iterates through all tradable assets
- Places randomized buy orders
- Continuous loop with configurable intervals
- Volume randomization (0.2x to 2.0x multiplier)

### 3. Spread Monitoring
- Fetches bid-ask spread for all pairs
- Identifies arbitrage opportunities
- Continuous monitoring loop

### 4. Automated Liquidation
- Monitors account balances
- Sells assets above threshold
- Respects precision requirements
- Symbol name mapping for exchange compatibility

## Logging

Advanced scripts include rotating log files:
```python
handler = RotatingFileHandler(
    f"{timestamp}_trading.log", 
    maxBytes=5*1024*1024,  # 5 MB
    backupCount=5
)
```

## Error Handling

**Common Error Codes:**
- `-1122`: Invalid symbol name (use symbol mapping)
- Rate limit errors: Implement sleep delays between requests
- Authentication errors: Verify timestamp and signature

**Symbol Name Mapping:**
```python
symbol_name_mapping = {
    "SOLANA/USDT": "SOL/USDT",
    "LTCBSC/USDT": "LTC/USDT",
    "POL/USDT": "MATIC/USDT"
}
```

## Safety Features

1. **Volume Truncation:** Automatic precision adjustment
2. **Rate Limiting:** Built-in delays between requests
3. **Error Logging:** Comprehensive error tracking
4. **Balance Checks:** Threshold-based operations
5. **Connection Keep-Alive:** Automatic ping/pong for WebSocket

## Performance Optimization

- **Threading:** Parallel execution for trading and liquidation
- **Batch Orders:** Multiple orders in single cycle
- **Connection Reuse:** Persistent WebSocket connections
- **Efficient Polling:** Configurable intervals to minimize API calls

## Development & Testing Folder

`Spot_API_testing_development/` contains advanced production-ready trading algorithms:

**Account Management:**
- `1_fetch_account_info.py` - Comprehensive account data retrieval
- `1b_fetch_account_info_symbol_name.py` - Symbol-specific account info

**Trading Strategies:**
- `2_random_trading.py` - Random asset selection and execution
- `4_spot_trading_loop.py` to `4d_spot_trading_loop.py` - Multiple trading loop implementations

**Liquidation Strategies:**
- `3_liquidation.py` - Basic position liquidation
- `5_liquidation_20250427.py` - Enhanced liquidation with date stamping
- `8_liquidation.py` - Advanced liquidation algorithm

**Bulk Operations:**
- `6_bulk_buy_20250427.py` - Production bulk buying (executed on 2025-04-27)
- `7_bulk_buy.py` - Enhanced bulk buying with comprehensive logging

**Execution Logs:**
- `log/` - Contains detailed trading execution logs with timestamps
  - `20250527_170847_bulk_buy.log` - 1.9M lines of bulk buy execution
  - `20250527_171103_liquidation.log` - 1.8M lines of liquidation execution
  - `20250619_054051_liquidation.log` - Recent liquidation logs
  - `20250619_054116_bulk_buy.log` - Recent bulk buy logs

## Notes

- Always test with small volumes on testnet first
- Monitor API rate limits
- Keep API credentials secure
- Review exchange fee structure
- Understand liquidation mechanisms before automated trading
- WebSocket connections require ping/pong to stay alive
- All timestamps must be in milliseconds

## Resources

**Official Documentation:**
- [GAIA Exchange API Documentation](https://gaia-4.gitbook.io/gaiaex_api/) - Complete API reference with endpoints, authentication, and examples
- [GAIA Exchange Website](https://www.gaiaex.com) - Trading platform

**API Endpoints:**
- Base URL (Spot): `https://openapi.gaiaex.com`
- Base URL (Futures): `https://futuresopenapi.gaiaex.com`
- WebSocket: `wss://ws.gaiaex.com/kline-api/ws`

**Key Documentation Sections:**
- [OpenAPI Basic Information](https://gaia-4.gitbook.io/gaiaex_api/) - Authentication and request signing
- [Spot Trading Endpoints](https://gaia-4.gitbook.io/gaiaex_api/spot-trade) - Order placement and management
- [Contract Trading Endpoints](https://gaia-4.gitbook.io/gaiaex_api/contract-trade) - Futures trading
- [WebSocket Streams](https://gaia-4.gitbook.io/gaiaex_api/websocket) - Real-time data feeds

---

## Disclaimer

This software is for educational and research purposes only. Cryptocurrency trading carries significant risk of loss. Always test thoroughly on demo accounts before live deployment. Past performance does not guarantee future results.

## License

MIT License
