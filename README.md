# Algorithmic Trading Systems Portfolio

A comprehensive collection of algorithmic trading systems and tools covering exchange infrastructure, FIX protocol implementation, low-latency trading, reinforcement learning agents, and technical analysis.

## Projects Overview

### 1. Crypto Exchange Trading API
Python-based trading automation for cryptocurrency exchanges with REST API and WebSocket support.

**Features:**
- Spot & Futures trading (market orders, limit orders, position management)
- Real-time price updates via WebSocket
- Account balance monitoring and liquidation management
- Automated trading algorithms

**Tech Stack:** Python, Requests, WebSocket, HMAC Authentication

---

### 2. FIX Protocol Implementation (QuickFIX)
Multi-language implementation of the FIX protocol for institutional trading.

**Components:**
- **C++:** Order matching engine, executor, trade client
- **Java:** Banzai trading GUI with Swing UI
- **Python:** Lightweight executor

**Tech Stack:** QuickFIX, C++, Java Swing, Python

---

### 3. Low-Latency Trading System
High-performance C++20 trading infrastructure optimized for microsecond latency.

**Architecture:**
- **Exchange Side:**
  - Matching engine with lock-free order books
  - TCP order server
  - Multicast market data publisher (snapshot + incremental)
  
- **Trading Side:**
  - Trade engine with multiple algorithm support (Market Maker, Liquidity Taker, Random)
  - Order gateway
  - Market data consumer

**Performance Features:**
- Lock-free queues for inter-thread communication
- Memory pools for zero-allocation paths
- Custom logging with microsecond timestamps
- TCP and multicast socket utilities

**Tech Stack:** C++20, CMake, Lock-Free Data Structures

---

### 4. Reinforcement Learning Trading Agent
Deep RL agent trained to trade on margin with multi-timeframe technical analysis.

**Features:**
- Custom OpenAI Gym environment for margin trading simulation
- IMPALA algorithm with LSTM (512 hidden units, 20 sequence length)
- Multi-timeframe signal generation (1min to 1day)
- Risk management: leverage control, stop-out, drawdown limits
- Reward function: Sortino ratio optimization with goal bonuses

**Training Infrastructure:**
- Ray RLlib distributed training (195 workers)
- TensorFlow 2 with eager execution
- Discrete action space: 201 actions (position sizing from -100% to +100%)

**Tech Stack:** Python, Ray RLlib, TensorFlow 2, OpenAI Gym, Pandas

---

### 5. Trading Signals Dashboard (MetaTrader 4)
Enterprise-grade multi-timeframe technical analysis system with probabilistic scoring.

**Key Features:**
- 23 technical indicators across 8 timeframes (M1 to Monthly)
- Ichimoku Kinko Hyo comprehensive analysis
- Weighted signal aggregation with sigmoid transformation
- Asymmetric ATR% volatility filter
- Multi-tier alert system with higher timeframe confirmation
- Real-time price ladder visualization

**Indicators:**
- Ichimoku: Cloud, Tenkan, Kijun, Chikou, Span A/B
- Momentum: MACD, RSI, ADX, SAR, ATR

**Tech Stack:** MQL4, MetaTrader 4

---

### 6. Trading Strategy Backtesting
Systematic backtesting framework with detailed performance reports.

**Strategies:**
- ASB_m15EH1S_v2026: Multi-asset strategy
- KSB_H4_01_v10: H4 timeframe strategy

**Instruments:** BTC, ETH, EURUSD, GBPUSD, USDJPY, OIL

**Tech Stack:** MQL4, MetaTrader 4 Strategy Tester

---

## Technical Stack Summary

| Category | Technologies |
|----------|-------------|
| Languages | Python, C++20, Java, MQL4 |
| Frameworks | Ray RLlib, TensorFlow, QuickFIX |
| Concepts | Lock-Free Programming, Multicast Networking, Deep RL |
| Performance | Microsecond latency, Lock-free queues, Memory pools |
| ML/RL | IMPALA, LSTM, Custom Gym Environments |
| Trading | FIX Protocol, REST APIs, WebSocket, Technical Analysis |

---

## Key Achievements

- **Low Latency:** Sub-microsecond order book operations with lock-free data structures
- **Scalability:** Distributed RL training with 195 parallel workers
- **Comprehensive Analysis:** 184 technical signals per symbol (23 indicators × 8 timeframes)
- **Risk Management:** Multi-layer position sizing with margin controls and stop-outs
- **Production Ready:** FIX protocol integration for institutional connectivity

---

## Getting Started

### Prerequisites
- Python 3.8+
- C++20 compiler (GCC 10+ or Clang 12+)
- Java 11+
- CMake 3.5+
- MetaTrader 4 (for trading signals dashboard)

### Low-Latency Trading System
```bash
cd Low_Latency_concept/cpp_simple_example
./scripts/build.sh
./scripts/run_exchange_and_clients.sh
```

### RL Trading Agent
```bash
cd Reinforcement_Learning/Trading_Agent
pip install -r Training/requirements.txt
python Training/Ray_Rllib_impala_LSTM.py
```

### Crypto Exchange API
```bash
cd Crypto_Exchange/Trading_API
# Configure your API keys in config file
python spot_22_trading_algo.py
```

---

## Project Structure

```
├── Crypto_Exchange/          # Crypto trading APIs and WebSocket
├── FIX_protocol_quickfix/    # FIX protocol implementations
├── Low_Latency_concept/      # High-performance C++ trading system
├── Reinforcement_Learning/   # RL trading agents and environments
├── Trading_Signals_Dashboard/ # MT4 technical analysis system
└── Trading_Strategy_Backtesting/ # Strategy backtests and reports
```

---

## Disclaimer

This software is for **educational and research purposes only**. Trading financial instruments carries significant risk of loss. Past performance does not guarantee future results. Always test thoroughly on demo accounts before considering live deployment.

---

## License

MIT License - see individual project files for details.

---

## Author

**Eugene Lam**

Quantitative Trader | Software Engineer | ML Researcher

---

## Related Topics

`#AlgorithmicTrading` `#HighFrequencyTrading` `#ReinforcementLearning` `#LowLatency` `#FIXProtocol` `#C++` `#Python` `#MachineLearning` `#QuantFinance`
