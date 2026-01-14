#pragma once

#include <functional>

#include "socket_utils.h"

#include "logging.h"

/*
 * MULTICAST SOCKET - UDP MULTICAST FOR MARKET DATA DISTRIBUTION
 * ==============================================================
 * 
 * PURPOSE:
 * Implements UDP multicast sockets for one-to-many market data distribution in trading systems.
 * Multicast is THE standard protocol for exchanges to broadcast market data to thousands of
 * subscribers simultaneously with minimal latency.
 * 
 * WHAT IS MULTICAST?
 * - UDP multicast: One sender broadcasts to many receivers
 * - IP range: 224.0.0.0 to 239.255.255.255 (Class D addresses)
 * - Efficient: Network switches replicate packets (not sender)
 * - Low latency: No TCP handshake, no retransmissions, no flow control
 * 
 * WHY MULTICAST FOR MARKET DATA?
 * - Scalability: One stream serves 1000+ subscribers (no 1000 TCP connections)
 * - Latency: 5-50 microseconds (vs 50-200 us for TCP)
 * - Efficiency: Network does replication (vs sender sending 1000 copies)
 * - Fair: All subscribers receive data simultaneously (no timing advantage)
 * 
 * REAL-WORLD USAGE IN TRADING:
 * - Exchange -> All traders: Order book updates, trade ticks, reference data
 * - Internal: Matching engine -> Risk/Strategy/Gateway components
 * - Market data vendors: Bloomberg, Reuters distribute via multicast
 * - Exchanges: NYSE, NASDAQ, CME all use multicast for market data
 * 
 * MULTICAST ARCHITECTURE:
 * ```
 * Exchange Matching Engine
 *         |
 *    [Multicast]  239.0.0.1:12345
 *      /  |  \
 *     /   |   \
 * Trader1 Trader2 Trader3 ... Trader1000
 * (All receive same stream simultaneously)
 * ```
 * 
 * vs. UNICAST (TCP):
 * ```
 * Exchange
 *  [TCP]> Trader1
 *  [TCP]> Trader2
 *  [TCP]> Trader3
 *  [TCP]> ... 1000 connections!
 * (Server sends 1000 copies, 1000x network traffic)
 * ```
 * 
 * PROTOCOL CHARACTERISTICS:
 * - Transport: UDP (connectionless, unreliable)
 * - Delivery: Best effort (packets may be lost)
 * - Ordering: Not guaranteed (packets may arrive out of order)
 * - Flow control: None (receiver must keep up)
 * - Latency: 5-50 microseconds (minimal overhead)
 * 
 * RELIABILITY HANDLING:
 * - Multicast is unreliable (UDP) - packets can be lost
 * - Solution: Sequence numbers in application protocol
 * - Gap detection: Compare sequence numbers
 * - Gap recovery: Request retransmission via separate channel
 * - Typical loss: <0.01% on good network (LAN)
 * 
 * BUFFER SIZING:
 * - Default: 64 MB (very large to handle bursts)
 * - Why so large? Market data comes in bursts (e.g., 1000 updates in 1 microsecond)
 * - OS buffer: Must be large to prevent packet drops
 * - Application buffer: Large enough to process bursts without overflow
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Latency: 5-50 microseconds (publisher to subscriber)
 * - Throughput: 1-10 Gbps (limited by network, not protocol)
 * - CPU: Very low (kernel handles most work)
 * - Network: Efficient (packets replicated by switches)
 */

namespace Common {
  /*
   * BUFFER SIZE CONSTANT
   * ====================
   * Size of send and receive buffers: 64 MB (67,108,864 bytes)
   * 
   * WHY 64 MB?
   * - Market data bursts: NYSE can send 100,000 updates in <1 millisecond
   * - Each update: ~100-500 bytes average
   * - Burst size: 100,000 * 500 = 50 MB
   * - Buffer must hold entire burst while application processes
   * 
   * TRADE-OFFS:
   * - Too small: Packet drops, data loss, need retransmissions
   * - Too large: Wasted memory, more cache misses
   * - 64 MB: Industry standard, handles worst-case bursts
   * 
   * MEMORY IMPACT:
   * - Per socket: 2 * 64 MB = 128 MB (send + receive buffers)
   * - Typical system: 5-10 multicast sockets = 640 MB - 1.28 GB
   * - Acceptable: Modern servers have 128+ GB RAM
   */
  constexpr size_t McastBufferSize = 64 * 1024 * 1024;  // 64 MB = 64 * 1024 KB * 1024 bytes
                                                        // Large buffer to handle market data bursts
                                                        // Prevents packet loss during processing

  /*
   * MULTICAST SOCKET STRUCTURE
   * ===========================
   * Manages UDP multicast socket for sending and/or receiving market data.
   * 
   * TYPICAL USAGE PATTERNS:
   * 
   * Publisher (Exchange/Matching Engine):
   * - init() with is_listening=false (sender mode)
   * - send() to buffer outgoing data
   * - sendAndRecv() to flush buffer to network
   * 
   * Subscriber (Trading Strategy/Gateway):
   * - init() with is_listening=true (receiver mode)
   * - join() to subscribe to multicast group
   * - sendAndRecv() to receive and process incoming data
   * - recv_callback_() called when data arrives
   * 
   * DESIGN NOTES:
   * - Struct (not class): All members public for performance
   * - Non-blocking: All operations use MSG_DONTWAIT
   * - Batching: send() buffers, sendAndRecv() flushes
   * - Zero-copy: Direct memcpy to/from buffers
   */
  struct McastSocket {
    /*
     * CONSTRUCTOR
     * ===========
     * Creates multicast socket with pre-allocated buffers.
     * 
     * WHAT IT DOES:
     * - Stores logger reference (for logging send/receive events)
     * - Pre-allocates 64 MB outbound buffer
     * - Pre-allocates 64 MB inbound buffer
     * - Does NOT create socket yet (call init())
     * - Does NOT join multicast group yet (call join())
     * 
     * PARAMETERS:
     * - logger: Reference to logger for debugging/monitoring
     * 
     * MEMORY ALLOCATION:
     * - Allocates 128 MB total (64 MB * 2 buffers)
     * - Happens once at construction (not in hot path)
     * - Buffers reused for lifetime of socket
     * 
     * PERFORMANCE:
     * - One-time cost at initialization
     * - Pre-allocation ensures no allocations in hot path
     */
    McastSocket(Logger &logger)  // Constructor takes logger reference
        : logger_(logger) {      // Initialize logger reference (member initializer list)
      outbound_data_.resize(McastBufferSize);  // Pre-allocate 64 MB send buffer
                                               // resize() allocates memory and default-initializes
      inbound_data_.resize(McastBufferSize);   // Pre-allocate 64 MB receive buffer
                                               // Both buffers ready for immediate use
    }

    /*
     * INITIALIZE SOCKET
     * =================
     * Creates and configures multicast socket for sending or receiving.
     * 
     * WHAT IT DOES:
     * 1. Creates UDP socket
     * 2. Sets socket options (reuse address, multicast, non-blocking)
     * 3. Binds to interface and port (for receiver)
     * 4. Does NOT join multicast group yet (call join() after)
     * 
     * PARAMETERS:
     * - ip: Multicast group IP (224.0.0.0 - 239.255.255.255)
     * - iface: Network interface IP (e.g., "192.168.1.100")
     * - port: UDP port number (e.g., 12345)
     * - is_listening: true=receiver, false=sender
     * 
     * RETURN VALUE:
     * - Socket file descriptor (>=0 on success, -1 on failure)
     * 
     * SENDER vs RECEIVER:
     * - Sender: Binds to 0.0.0.0:0 (any address, any port)
     * - Receiver: Binds to 0.0.0.0:port (receive on specific port)
     * 
     * EXAMPLE:
     * ```cpp
     * // Publisher
     * socket.init("239.0.0.1", "192.168.1.100", 12345, false);
     * 
     * // Subscriber
     * socket.init("239.0.0.1", "192.168.1.100", 12345, true);
     * socket.join("239.0.0.1");
     * ```
     */
    auto init(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int;

    /*
     * JOIN MULTICAST GROUP
     * ====================
     * Subscribes to multicast group to receive data (receiver only).
     * 
     * WHAT IT DOES:
     * - Tells OS: "Send me packets destined for this multicast group"
     * - OS/Network: Configures to receive multicast traffic
     * - IGMP: Sends Internet Group Management Protocol message
     * 
     * PARAMETERS:
     * - ip: Multicast group IP to join (e.g., "239.0.0.1")
     * 
     * RETURN VALUE:
     * - true: Successfully joined group
     * - false: Failed to join (check permissions, network config)
     * 
     * WHEN TO CALL:
     * - After init() for receivers
     * - Not needed for senders (they just send to multicast address)
     * 
     * IGMP (Internet Group Management Protocol):
     * - Protocol for managing multicast group membership
     * - join(): Sends IGMP "join group" message
     * - leave(): Sends IGMP "leave group" message
     * - Network switches learn which ports need multicast traffic
     * 
     * EXAMPLE:
     * ```cpp
     * socket.init("239.0.0.1", "192.168.1.100", 12345, true);
     * if (!socket.join("239.0.0.1")) {
     *   FATAL("Failed to join multicast group");
     * }
     * ```
     */
    auto join(const std::string &ip) -> bool;

    /*
     * LEAVE MULTICAST GROUP
     * =====================
     * Unsubscribes from multicast group and closes socket.
     * 
     * WHAT IT DOES:
     * - Closes socket file descriptor
     * - OS automatically sends IGMP "leave group" message
     * - Network stops sending multicast traffic to this host
     * 
     * PARAMETERS:
     * - ip: Multicast group IP (unused in current implementation)
     * - port: Port number (unused in current implementation)
     * 
     * WHEN TO CALL:
     * - During shutdown
     * - When changing multicast groups
     * - On error conditions
     * 
     * CLEANUP:
     * - Closes socket (close() syscall)
     * - Sets socket_fd_ to -1 (invalid)
     * - Buffers remain allocated (for reuse if init() called again)
     */
    auto leave(const std::string &ip, int port) -> void;

    /*
     * SEND AND RECEIVE DATA
     * =====================
     * Non-blocking send and receive in single call (hot path operation).
     * 
     * WHAT IT DOES:
     * 1. Attempts to receive data (non-blocking)
     * 2. If data received: Calls recv_callback_ with received data
     * 3. If send buffer has data: Flushes to network
     * 4. Returns whether data was received
     * 
     * RECEIVE PATH:
     * - recv() with MSG_DONTWAIT (non-blocking)
     * - If data available: Read into inbound_data_ buffer
     * - Invoke recv_callback_(this) to process data
     * - Callback processes data from inbound_data_[0..next_rcv_valid_index_]
     * 
     * SEND PATH:
     * - If next_send_valid_index_ > 0 (have data to send)
     * - send() with MSG_DONTWAIT | MSG_NOSIGNAL
     * - MSG_NOSIGNAL: Don't raise SIGPIPE if peer disconnected
     * - Reset next_send_valid_index_ to 0 (buffer now empty)
     * 
     * RETURN VALUE:
     * - true: Data was received
     * - false: No data received (may have sent data)
     * 
     * PERFORMANCE:
     * - Non-blocking: Returns immediately if no data
     * - Batching: Multiple send() calls batched before flush
     * - Low overhead: ~100-500 ns per call when no data
     * 
     * TYPICAL USAGE:
     * ```cpp
     * while (running) {
     *   socket.sendAndRecv();  // Busy-poll for data
     * }
     * ```
     */
    auto sendAndRecv() noexcept -> bool;  // noexcept = critical for hot path performance

    /*
     * BUFFER OUTGOING DATA
     * ====================
     * Copies data to send buffer (does not actually send yet).
     * 
     * WHAT IT DOES:
     * - memcpy data into outbound_data_ buffer
     * - Advances next_send_valid_index_ by len
     * - Validates buffer not overflowed (ASSERT)
     * - Actual send happens in sendAndRecv()
     * 
     * PARAMETERS:
     * - data: Pointer to data to send
     * - len: Number of bytes to send
     * 
     * WHY BUFFER INSTEAD OF IMMEDIATE SEND?
     * - Batching: Combine multiple messages into fewer UDP packets
     * - Efficiency: One sendAndRecv() flushes all buffered data
     * - Nagle-like: Reduce syscall overhead
     * 
     * BUFFER OVERFLOW:
     * - ASSERT if buffer full (next_send_valid_index_ + len > McastBufferSize)
     * - Application must call sendAndRecv() regularly to flush
     * - In production: Monitor buffer usage, increase size if needed
     * 
     * PERFORMANCE:
     * - memcpy: ~5-20 ns per 100 bytes (very fast)
     * - No syscall: Just memory copy
     * - Hot path acceptable: Batching amortizes cost
     * 
     * USAGE PATTERN:
     * ```cpp
     * MarketUpdate update = ...;
     * socket.send(&update, sizeof(update));  // Buffer
     * socket.send(&update2, sizeof(update2)); // Buffer more
     * socket.sendAndRecv();                   // Flush all
     * ```
     */
    auto send(const void *data, size_t len) noexcept -> void;  // noexcept = hot path

    /*
     * MEMBER VARIABLES
     * ================
     */
    
    // SOCKET FILE DESCRIPTOR
    // - OS handle to UDP socket
    // - -1 = uninitialized, >=0 = valid socket
    // - Used in all send/recv syscalls
    int socket_fd_ = -1;

    // OUTBOUND BUFFER - Data waiting to be sent
    // - Pre-allocated 64 MB buffer
    // - send() writes to this buffer
    // - sendAndRecv() flushes to network
    // - Only used by publishers (senders)
    std::vector<char> outbound_data_;
    
    // OUTBOUND INDEX - Number of valid bytes in outbound buffer
    // - Points to next free position in outbound_data_
    // - send() increments this
    // - sendAndRecv() resets to 0 after flush
    size_t next_send_valid_index_ = 0;
    
    // INBOUND BUFFER - Data received from network
    // - Pre-allocated 64 MB buffer
    // - recv() writes to this buffer
    // - recv_callback_ processes this data
    // - Only used by subscribers (receivers)
    std::vector<char> inbound_data_;
    
    // INBOUND INDEX - Number of valid bytes in inbound buffer
    // - Points to next free position in inbound_data_
    // - Incremented by recv() when data arrives
    // - Callback should reset after processing
    size_t next_rcv_valid_index_ = 0;

    // RECEIVE CALLBACK - Function called when data arrives
    // - Type: std::function<void(McastSocket* s)>
    // - Called by sendAndRecv() when data received
    // - Processes data from inbound_data_[0..next_rcv_valid_index_-1]
    // - Must be set before calling sendAndRecv() as receiver
    // - Example: Parse market updates, update order book, execute strategy
    std::function<void(McastSocket *s)> recv_callback_ = nullptr;

    // TIME STRING - Temporary for timestamp formatting
    // - Reused buffer for getCurrentTimeStr()
    // - Avoids repeated allocations in logging
    std::string time_str_;
    
    // LOGGER - For debugging and monitoring
    // - Logs send/receive events
    // - Logs errors and warnings
    // - Reference (not owned by socket)
    Logger &logger_;
  };
}

/*
 * MULTICAST BEST PRACTICES IN HFT
 * ================================
 * 
 * 1. NETWORK CONFIGURATION:
 *    - Use dedicated network interface for market data
 *    - 10 GbE or faster (market data is high bandwidth)
 *    - Low-latency switches (cut-through, not store-and-forward)
 *    - Enable IGMP snooping on switches
 *    - Tune OS receive buffers (sysctl net.core.rmem_max)
 * 
 * 2. RELIABLE MULTICAST:
 *    - Add sequence numbers to every message
 *    - Detect gaps: if (seq != expected) { request_retransmit(); }
 *    - Separate retransmission channel (unicast or different multicast)
 *    - Timeout for gap recovery (don't wait forever)
 * 
 * 3. BUFFER SIZING:
 *    - Monitor buffer usage (track next_rcv_valid_index_ high water mark)
 *    - Increase if approaching full (prevent drops)
 *    - OS buffer: sysctl net.core.rmem_max = 134217728 (128 MB)
 *    - Application buffer: Match or exceed OS buffer
 * 
 * 4. PROCESSING STRATEGY:
 *    - Process in callback: Fast parsing, defer heavy work
 *    - Batch processing: Process multiple messages per callback
 *    - Reset buffer index: next_rcv_valid_index_ = 0 after processing
 *    - Don't block: Callback must return quickly (<1 microsecond ideal)
 * 
 * 5. MONITORING:
 *    - Packet loss: Track sequence number gaps
 *    - Latency: Timestamp at source, measure at receiver
 *    - Buffer usage: Log high water marks
 *    - Network stats: ethtool -S <interface>
 * 
 * 6. FAILOVER:
 *    - Primary and backup multicast groups
 *    - Subscribe to both, use sequence numbers to deduplicate
 *    - Automatic switchover if primary fails
 * 
 * EXAMPLE IMPLEMENTATION:
 * See mcast_socket.cpp for implementation details.
 */
