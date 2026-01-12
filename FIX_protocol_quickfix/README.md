# FIX Protocol Implementation (QuickFIX)

Multi-language implementation of the Financial Information eXchange (FIX) protocol for institutional electronic trading using the QuickFIX engine.

## Overview

This project demonstrates comprehensive FIX protocol implementations across four programming languages (C++, Java, Python, Rust), showcasing order matching engines, execution simulators, and trading clients for institutional trading systems.

## FIX Protocol

The Financial Information eXchange (FIX) protocol is an industry-standard messaging protocol for real-time electronic exchange of securities transaction information. It is widely used by institutional traders, brokers, exchanges, and other market participants.

**Supported FIX Versions:**
- FIX 4.0
- FIX 4.1
- FIX 4.2
- FIX 4.3
- FIX 4.4
- FIX 5.0 (FIXT.1.1)

## Project Structure

```
FIX_protocol_quickfix/
├── Cpp_example/               # C++ Implementation
│   ├── ordermatch/            # Order matching engine
│   ├── executor/              # Order executor (fills orders)
│   └── tradeclient/           # Interactive trading client
├── Java_Banzai/               # Java Implementation
│   ├── banzai/                # GUI trading application
│   ├── executor/              # Order execution engine
│   └── ordermatch/            # Order matching system
├── Python_example/            # Python Implementation
│   └── executor.py            # Simple order executor
└── Rust_example/              # Rust Implementation
    ├── demo_config.rs         # Programmatic configuration example
    ├── fix_getting_started.rs # Basic FIX acceptor
    └── fix_repl/              # Interactive FIX REPL
        ├── main.rs            # REPL entry point
        ├── fix_app.rs         # FIX application callbacks
        ├── command_parser.rs  # Command parsing
        └── command_exec.rs    # Command execution
```

## Components

### 1. C++ Implementation

High-performance FIX implementation with three main components:

#### OrderMatch (Order Matching Engine)
A simulated exchange with intelligent order matching.

**Features:**
- Multi-market support with symbol-based routing
- Price-time priority matching algorithm
- Limit order book with bid/ask separation
- Automatic order matching and execution
- Support for NewOrderSingle, OrderCancelRequest, MarketDataRequest

**Core Classes:**
- `Market` - Single market with bid/ask order books using std::multimap
- `OrderMatcher` - Multi-market manager
- `Order` - Order representation with execution tracking
- `IDGenerator` - Unique execution ID generation

**Order Book Structure:**
```cpp
BidOrders: multimap<double, Order, greater<double>>  // Highest bid first
AskOrders: multimap<double, Order, less<double>>     // Lowest ask first
```

**Build:**
```bash
cd Cpp_example/ordermatch
mkdir build && cd build
cmake ..
make
./ordermatch config.cfg
```

#### Executor (Order Execution Simulator)
Simulates order execution by immediately filling all limit orders.

**Features:**
- Accepts NewOrderSingle messages
- Instant fill execution for limit orders
- ExecutionReport generation
- Multi-version FIX support (4.0 through 5.0)
- Account field preservation

**Behavior:**
- Receives order from client
- Validates order type (limit only)
- Immediately fills order at requested price
- Sends ExecutionReport with FILLED status

**Build:**
```bash
cd Cpp_example/executor
mkdir build && cd build
cmake ..
make
./executor config.cfg
```

#### TradeClient (Interactive Trading Client)
Command-line interface for manual FIX order entry and testing.

**Features:**
- Interactive menu-driven interface
- Support for all FIX versions (4.0-5.0)
- Multiple order types (Market, Limit, Stop, Stop-Limit)
- Order lifecycle management (New, Cancel, Replace)
- Market data requests
- Execution and cancel reject handling

**Actions:**
1. Enter Order (NewOrderSingle)
2. Cancel Order (OrderCancelRequest)
3. Replace Order (OrderCancelReplaceRequest)
4. Market Data Request
5. Quit

**Build:**
```bash
cd Cpp_example/tradeclient
mkdir build && cd build
cmake ..
make
./tradeclient config.cfg
```

### 2. Java Implementation (Banzai)

Professional-grade GUI trading application with full order management.

#### Banzai (Trading GUI Application)

**Features:**
- Swing-based graphical user interface
- Real-time order entry and management
- Execution monitoring and display
- Order and execution table models
- Session management with logon events
- JMX monitoring support

**UI Components:**
- `BanzaiFrame` - Main application window
- `OrderPanel` - Order entry interface
- `OrderTable` - Live order book display
- `ExecutionPanel` - Execution monitoring
- `CancelReplacePanel` - Order modification

**Order Management:**
- `Order` - Order object with state tracking
- `OrderTableModel` - Order book data model
- `Execution` - Trade execution records
- `ExecutionTableModel` - Execution history model

**Data Structures:**
- `OrderSide` - BUY, SELL, SHORT_SELL
- `OrderType` - MARKET, LIMIT, STOP, STOP_LIMIT
- `OrderTIF` - DAY, GTC, IOC, FOK, GTX
- `TwoWayMap` - Bidirectional mapping utility

**Run:**
```bash
cd Java_Banzai
java -cp quickfixj.jar:. quickfix.examples.banzai.Banzai [config.cfg]
```

#### Executor (Java)
Server-side order execution simulator with dynamic session support.

**Features:**
- Socket acceptor for client connections
- Dynamic session provisioning
- Market data provider simulation
- Instant order fills
- JMX monitoring and management

**Configuration:**
- Static sessions from configuration file
- Dynamic session templates
- Multiple socket accept addresses

**Run:**
```bash
cd Java_Banzai/executor
java -cp quickfixj.jar:. quickfix.examples.executor.Executor config.cfg
```

#### OrderMatch (Java)
Complete order matching engine with market simulation.

**Features:**
- Multi-market order routing
- Order book management
- Price-time priority matching
- Partial fill support
- Market data snapshots

**Components:**
- `Market` - Individual market with order book
- `OrderMatcher` - Multi-market coordinator
- `Order` - Order with execution tracking
- `IdGenerator` - Unique ID generation

**Run:**
```bash
cd Java_Banzai/ordermatch
java -cp quickfixj.jar:. quickfix.examples.ordermatch.Main config.cfg
```

### 3. Rust Implementation

Modern, safe, and performant FIX implementation using the quickfix-rs library.

#### Demo Config (demo_config.rs)
Demonstrates programmatic FIX acceptor configuration without external config files.

**Features:**
- Build SessionSettings in code (no config file needed)
- Demonstrates all configuration options
- Memory-based message store
- Screen logging
- Custom application callbacks

**Key Concepts:**
- SessionSettings builder pattern
- Acceptor initialization
- Session lifecycle management
- Programmatic configuration vs file-based

**Build & Run:**
```bash
cd Rust_example
cargo build --release
cargo run --bin demo_config
```

**Configuration Example:**
```rust
let mut settings = SessionSettings::new();
settings.set(SessionID::default(), "ConnectionType", "acceptor")?;
settings.set(SessionID::default(), "SocketAcceptPort", "5001")?;
settings.set(SessionID::default(), "StartTime", "00:00:00")?;
```

#### Getting Started (fix_getting_started.rs)
Basic FIX acceptor that loads configuration from file.

**Features:**
- File-based configuration
- Simple acceptor setup
- Message store and logger initialization
- Minimal application callbacks
- Clean shutdown handling

**Use Cases:**
- Learning FIX protocol basics
- Testing FIX connectivity
- Template for new projects
- Configuration file examples

**Build & Run:**
```bash
cd Rust_example
cargo run --bin fix_getting_started
```

**Configuration File:**
```ini
[DEFAULT]
ConnectionType=acceptor
SocketAcceptPort=5001
FileStorePath=target/store
FileLogPath=target/log

[SESSION]
BeginString=FIX.4.2
SenderCompID=ACCEPTOR
TargetCompID=INITIATOR
```

#### FIX REPL (Interactive Shell)
Interactive command-line tool for manual FIX testing and debugging.

**Features:**
- Interactive command shell
- Send arbitrary FIX messages
- Session management (logon/logout)
- Message inspection
- Command history
- Both initiator and acceptor modes

**Commands:**
- `send_to <SessionID> <FIXMessage>` - Send FIX message
- `sessions` - List active sessions
- `quit` - Exit REPL

**Architecture:**
- `main.rs` - Entry point and server loop
- `fix_app.rs` - FIX application callbacks
- `command_parser.rs` - Command parsing logic
- `command_exec.rs` - Command execution

**Build & Run (Acceptor Mode):**
```bash
cd Rust_example
cargo run --bin fix_repl -- acceptor configs/acceptor.cfg
```

**Build & Run (Initiator Mode):**
```bash
cargo run --bin fix_repl -- initiator configs/initiator.cfg
```

**Example Usage:**
```
FIX REPL> sessions
Active sessions:
  - FIX.4.2:CLIENT->SERVER

FIX REPL> send_to FIX.4.2:CLIENT->SERVER 35=D|55=AAPL|54=1|38=100|40=2|44=150.50
Sent: NewOrderSingle for AAPL

FIX REPL> quit
```

**Key Rust Features:**
- Type-safe FIX message handling
- Zero-copy message parsing where possible
- Thread-safe session management
- Memory safety without garbage collection
- Pattern matching for message routing
- Result-based error handling

**Rust-Specific Advantages:**
- Compile-time guarantees for memory safety
- No runtime overhead (zero-cost abstractions)
- Fearless concurrency
- Modern tooling (Cargo, rustfmt, clippy)
- Excellent performance (comparable to C++)

### 4. Python Implementation

Lightweight executor for rapid prototyping and testing.

#### Executor (Python)

**Features:**
- Simple FIX server implementation
- Immediate order execution
- Multi-version FIX support
- Minimal dependencies

**Behavior:**
```python
1. Receives NewOrderSingle
2. Validates limit order type
3. Extracts: Symbol, Side, OrderQty, Price, ClOrdID
4. Generates ExecutionReport (FILLED)
5. Sends response to client
```

**Run:**
```bash
cd Python_example
python executor.py config.cfg
```

## Message Flow

### New Order Flow
```
Client                  Executor/OrderMatch
  |                            |
  |--NewOrderSingle----------->|
  |    (ClOrdID, Symbol,       |
  |     Side, Qty, Price)      |
  |                            |
  |                    [Match/Execute]
  |                            |
  |<--ExecutionReport----------|
  |    (OrderID, ExecID,       |
  |     Status=FILLED)         |
```

### Order Cancel Flow
```
Client                  OrderMatch
  |                            |
  |--OrderCancelRequest------->|
  |    (OrigClOrdID)           |
  |                            |
  |                    [Find & Cancel]
  |                            |
  |<--ExecutionReport----------|
  |    (Status=CANCELED)       |
```

### Order Replace Flow
```
Client                  OrderMatch
  |                            |
  |--CancelReplaceRequest----->|
  |    (OrigClOrdID,           |
  |     New Price/Qty)         |
  |                            |
  |                [Cancel Old & Insert New]
  |                            |
  |<--ExecutionReport----------|
  |    (New OrderID)           |
```

## Configuration

### QuickFIX Configuration File Format

```ini
[DEFAULT]
ConnectionType=initiator
ReconnectInterval=60
FileStorePath=store
FileLogPath=log
StartTime=00:00:00
EndTime=00:00:00
UseDataDictionary=Y
DataDictionary=FIX42.xml

[SESSION]
BeginString=FIX.4.2
SenderCompID=CLIENT
TargetCompID=EXECUTOR
HeartBtInt=30
SocketConnectHost=localhost
SocketConnectPort=5001
```

### Common Settings

**Session Settings:**
- `BeginString` - FIX version (FIX.4.0 through FIXT.1.1)
- `SenderCompID` - Identifier for sender
- `TargetCompID` - Identifier for target
- `HeartBtInt` - Heartbeat interval in seconds

**Connection Settings:**
- `ConnectionType` - initiator or acceptor
- `SocketConnectHost` - Target host for initiator
- `SocketConnectPort` - Target port
- `SocketAcceptPort` - Listen port for acceptor

**Storage Settings:**
- `FileStorePath` - Message store directory
- `FileLogPath` - Log file directory
- `DataDictionary` - FIX data dictionary XML

## Key FIX Message Types

### NewOrderSingle (MsgType=D)
Place a new order.

**Required Fields:**
- ClOrdID (11) - Client order ID
- HandlInst (21) - Handling instructions
- Symbol (55) - Trading symbol
- Side (54) - Buy/Sell
- TransactTime (60) - Transaction timestamp
- OrdType (40) - Order type
- OrderQty (38) - Quantity
- Price (44) - Price (for limit orders)

### ExecutionReport (MsgType=8)
Order status update.

**Key Fields:**
- OrderID (37) - Exchange order ID
- ExecID (17) - Execution ID
- ExecType (150) - Execution type
- OrdStatus (39) - Order status
- Symbol (55) - Trading symbol
- Side (54) - Buy/Sell
- LeavesQty (151) - Remaining quantity
- CumQty (14) - Cumulative filled quantity
- AvgPx (6) - Average fill price

### OrderCancelRequest (MsgType=F)
Cancel an existing order.

**Required Fields:**
- OrigClOrdID (41) - Original client order ID
- ClOrdID (11) - New client order ID
- Symbol (55) - Trading symbol
- Side (54) - Buy/Sell

### OrderCancelReplaceRequest (MsgType=G)
Modify an existing order.

**Required Fields:**
- OrigClOrdID (41) - Original client order ID
- ClOrdID (11) - New client order ID
- Symbol (55) - Trading symbol
- Side (54) - Buy/Sell
- OrderQty (38) - New quantity
- Price (44) - New price

## Order Matching Algorithm

**Price-Time Priority:**
1. Orders sorted by price (best price first)
2. Within same price, sorted by time (FIFO)
3. Matching occurs when bid >= ask
4. Executes at aggressor price
5. Partial fills supported

**Example:**
```
Buy Orders (Bids):          Sell Orders (Asks):
Price | Qty | Time         Price | Qty | Time
100.5 | 200 | 10:00        100.3 | 150 | 10:02
100.3 | 100 | 10:01        100.4 | 200 | 10:01
100.2 | 150 | 09:59        100.6 | 100 | 09:58

Match: Bid@100.5 crosses Ask@100.3
Result: 150 shares @ 100.3 (ask price)
```

## Build Requirements

### C++ Implementation
- CMake 3.5+
- C++11 compatible compiler (GCC 4.8+, Clang 3.4+, MSVC 2013+)
- QuickFIX/C++ library
- Make or Ninja build system

### Java Implementation
- Java 8+
- QuickFIX/J library
- Maven or Ant (optional, for build automation)

### Rust Implementation
- Rust 1.70+ (2021 edition)
- Cargo (Rust package manager)
- quickfix-rs crate
- Install Rust: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`

### Python Implementation
- Python 2.7+ or Python 3.x
- quickfix Python module
- Install: `pip install quickfix`

## Running a Complete System

### Three-Component Setup

**Terminal 1 - OrderMatch Engine:**
```bash
cd Cpp_example/ordermatch/build
./ordermatch ../ordermatch.cfg
```

**Terminal 2 - Executor:**
```bash
cd Cpp_example/executor/build
./executor ../executor.cfg
```

**Terminal 3 - Trade Client:**
```bash
cd Cpp_example/tradeclient/build
./tradeclient ../tradeclient.cfg
```

### Two-Component Setup (Simple)

**Terminal 1 - Python Executor:**
```bash
cd Python_example
python executor.py executor.cfg
```

**Terminal 2 - Java Banzai GUI:**
```bash
cd Java_Banzai
java -jar banzai.jar banzai.cfg
```

## Testing

### Manual Testing with TradeClient
1. Start OrderMatch or Executor
2. Start TradeClient
3. Wait for logon confirmation
4. Select action (1-5)
5. Follow prompts to enter order details
6. Observe ExecutionReport responses

### Automated Testing
Create test scripts that:
1. Generate NewOrderSingle messages
2. Send via FIX session
3. Validate ExecutionReports
4. Test edge cases (invalid orders, cancels)

## Common Issues

### Connection Failures
- Verify ports are not in use
- Check firewall settings
- Ensure SenderCompID/TargetCompID match on both sides
- Verify BeginString versions match

### Order Rejections
- Check order type support (only LIMIT in some implementations)
- Verify all required fields are present
- Ensure prices and quantities are valid
- Check TimeInForce support (DAY only in OrderMatch)

### Session Issues
- Clear store files if sessions are out of sync
- Reset sequence numbers if needed
- Verify data dictionary paths
- Check log files for detailed errors

## Performance Characteristics

### C++ Implementation
- Order insertion: O(log n)
- Order matching: O(n) worst case, O(1) typical
- Memory efficient with STL containers
- Suitable for production use

### Java Implementation
- GUI responsive with Swing event dispatch
- Table models optimized for updates
- JMX monitoring overhead minimal
- Good for trading desk applications

### Rust Implementation
- Near C++ performance with memory safety
- Zero-cost abstractions
- Thread-safe by default (compiler enforced)
- Excellent for production high-frequency systems
- Modern development experience

### Python Implementation
- Quick prototyping and testing
- Not optimized for high frequency
- Suitable for development and simulation
- Easy integration with Python analytics

## FIX Protocol Resources

- **QuickFIX Official:** http://www.quickfixengine.org/
- **FIX Trading Community:** https://www.fixtrading.org/
- **FIX Protocol Specifications:** https://www.fixtrading.org/standards/
- **QuickFIX/J (Java):** http://www.quickfixj.org/
- **quickfix-rs (Rust):** https://github.com/arthurlm/quickfix-rs
- **FIX Data Dictionary:** Standard field definitions and message formats

## License

QuickFIX License (see individual source files)

This implementation follows the QuickFIX licensing terms as defined by quickfixengine.org.

## Notes

- All implementations are thread-safe where appropriate
- Session management is handled by QuickFIX engine
- Logging and message storage managed automatically
- Production deployments should include proper error handling and monitoring
- Consider using FIX session persistence for failover scenarios
- Implement proper security (TLS/SSL) for production systems

## Architectural Patterns

**Acceptor Pattern:** Server-side component that accepts FIX connections
**Initiator Pattern:** Client-side component that initiates FIX connections
**MessageCracker:** Design pattern for type-safe message handling
**Application Callbacks:** Event-driven architecture for FIX lifecycle events

## Future Enhancements

- Add support for more order types (market, stop, stop-limit)
- Implement market data distribution
- Add order book visualization
- Implement risk management checks
- Add session-level reject handling
- Support for FIX 5.0 SP2
- Multi-threaded order matching
- Database persistence for orders and executions
