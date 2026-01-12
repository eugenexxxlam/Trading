# QuickFIX Rust Examples

This directory contains Rust implementations demonstrating the use of the QuickFIX library for FIX protocol communication. These examples showcase various patterns for building FIX-compliant trading systems.

## Overview

The Financial Information eXchange (FIX) protocol is an industry-standard electronic communications protocol for real-time exchange of securities transaction information. These examples demonstrate how to use the Rust bindings for QuickFIX.

## Examples

### 1. demo_config.rs - Programmatic Configuration
Demonstrates how to build a FIX acceptor with programmatic configuration (no config file needed).

**Key Concepts:**
- Building SessionSettings in code
- Creating a FIX acceptor server
- Implementing ApplicationCallback trait
- Session lifecycle management

**Run:**
```bash
cargo run --example demo_config
```

### 2. fix_getting_started.rs - File-Based Configuration
A simple FIX acceptor that loads configuration from an external file.

**Key Concepts:**
- Loading settings from configuration files
- File-based message store
- Basic acceptor setup
- User input handling

**Run:**
```bash
cargo run --example fix_getting_started -- <config_file>
```

### 3. fix_repl - Interactive FIX Shell
A full-featured interactive REPL (Read-Eval-Print Loop) for testing FIX connections.

**Key Concepts:**
- Both initiator and acceptor modes
- Interactive command execution
- Real-time message sending
- Connection state management
- Advanced callback handling

**Run:**
```bash
# As acceptor
cargo run --example fix_repl -- acceptor <config_file>

# As initiator
cargo run --example fix_repl -- initiator <config_file>
```

**Available Commands:**
- `help` or `?` - Show available commands
- `status` - Display connection status
- `start` - Start the connection handler
- `stop` - Stop the connection handler
- `block` - Block until messages arrive
- `poll` - Poll for messages
- `send_to K1=V1|K2=V2 sender target` - Send a FIX message
- `quit` or `q` - Exit the program

## Architecture

### Application Callback Pattern
All examples implement the `ApplicationCallback` trait which provides hooks into the FIX engine lifecycle:

- `on_create()` - Called when a session is created
- `on_logon()` - Called when a successful logon occurs
- `on_logout()` - Called when a logout occurs
- `on_msg_to_admin()` - Intercept admin messages before sending
- `on_msg_to_app()` - Intercept application messages before sending
- `on_msg_from_admin()` - Process incoming admin messages
- `on_msg_from_app()` - Process incoming application messages

### Components

1. **SessionSettings**: Configuration for FIX sessions
2. **MessageStoreFactory**: Handles message persistence
3. **LogFactory**: Manages logging output
4. **Application**: Wraps your callback implementation
5. **Acceptor/Initiator**: Connection handlers for server/client modes

## Configuration Example

```ini
[DEFAULT]
ConnectionType=acceptor
ReconnectInterval=60
FileStorePath=store

[SESSION]
BeginString=FIX.4.4
SenderCompID=ME
TargetCompID=THEIR
StartTime=12:30:00
EndTime=23:30:00
HeartBtInt=20
SocketAcceptPort=4000
DataDictionary=spec/FIX44.xml
```

## Requirements

- Rust 1.70 or higher
- quickfix-rs library
- QuickFIX C++ library (installed via FFI bindings)

## Use Cases

These examples are useful for:
- Building trading gateways
- Testing FIX connectivity
- Developing market data feeds
- Creating order management systems
- Prototyping FIX-based applications

## License

These examples are based on the quickfix-rs library which is dual-licensed under Apache-2.0 and MIT licenses.

## References

- [FIX Protocol Official Site](https://www.fixtrading.org/)
- [QuickFIX Documentation](https://www.quickfixengine.org/)
- [quickfix-rs Repository](https://github.com/arthurlm/quickfix-rs)
