/*
 * MULTICAST SOCKET IMPLEMENTATION
 * ================================
 * Implementation of UDP multicast socket for market data distribution.
 * See mcast_socket.h for detailed design documentation.
 */

#include "mcast_socket.h"

namespace Common {
  /*
   * INITIALIZE MULTICAST SOCKET
   * ===========================
   * Creates and configures UDP multicast socket.
   */
  auto McastSocket::init(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int {
    // Create socket configuration structure
    const SocketCfg socket_cfg{ip,            // Multicast group IP (239.x.x.x)
                               iface,         // Network interface IP (e.g., 192.168.1.100)
                               port,          // UDP port number
                               true,          // is_udp = true (multicast uses UDP)
                               is_listening,  // true=receiver, false=sender
                               false};        // is_blocking = false (non-blocking I/O)
    
    // Create socket with configuration (delegates to socket_utils.h)
    socket_fd_ = createSocket(logger_, socket_cfg);  // Creates UDP socket, sets options, binds
                                                      // Returns file descriptor or -1 on error
    
    return socket_fd_;  // Return socket FD for error checking
  }

  /*
   * JOIN MULTICAST GROUP
   * ====================
   * Subscribe to multicast group (receiver only).
   */
  bool McastSocket::join(const std::string &ip) {
    return Common::join(socket_fd_, ip);  // Delegates to socket_utils.h join() function
                                          // Sends IGMP join message
                                          // Returns true on success, false on failure
  }

  /*
   * LEAVE MULTICAST GROUP
   * =====================
   * Unsubscribe and close socket.
   */
  auto McastSocket::leave(const std::string &, int) -> void {  // Parameters unused (could be removed)
    close(socket_fd_);  // Close socket file descriptor (OS sends IGMP leave automatically)
    socket_fd_ = -1;    // Mark as invalid
  }

  /*
   * SEND AND RECEIVE DATA (HOT PATH)
   * =================================
   * Non-blocking send and receive in single call.
   * This is called continuously in tight loop.
   */
  auto McastSocket::sendAndRecv() noexcept -> bool {
    
    /*
     * RECEIVE PATH - Read incoming data
     */
    // Attempt to receive data from socket (non-blocking)
    const ssize_t n_rcv = recv(socket_fd_,                                  // Socket file descriptor
                                inbound_data_.data() + next_rcv_valid_index_,  // Write to end of buffer
                                McastBufferSize - next_rcv_valid_index_,     // Space remaining in buffer
                                MSG_DONTWAIT);                              // Non-blocking flag
    // recv() syscall:
    // - Returns number of bytes read (>0) if data available
    // - Returns 0 if connection closed (not applicable for UDP)
    // - Returns -1 if no data available (EAGAIN/EWOULDBLOCK with MSG_DONTWAIT)
    
    if (n_rcv > 0) {  // Data was received
      // Update buffer index to reflect new data
      next_rcv_valid_index_ += n_rcv;  // Now have n_rcv more bytes in buffer
                                       // Buffer contains: inbound_data_[0..next_rcv_valid_index_-1]
      
      // Log receive event (debugging/monitoring)
      logger_.log("%:% %() % read socket:% len:%\n",   // Format string with placeholders
                  __FILE__,                             // Source file name
                  __LINE__,                             // Line number
                  __FUNCTION__,                         // Function name
                  Common::getCurrentTimeStr(&time_str_),  // Human-readable timestamp
                  socket_fd_,                           // Socket FD
                  next_rcv_valid_index_);               // Total bytes in buffer
      
      // Invoke callback to process received data
      recv_callback_(this);  // Pass this socket to callback
                            // Callback accesses: inbound_data_[0..next_rcv_valid_index_-1]
                            // Callback should reset next_rcv_valid_index_ = 0 after processing
                            // Or implement sliding window if partial processing
    }

    /*
     * SEND PATH - Flush outgoing data
     */
    if (next_send_valid_index_ > 0) {  // Have data to send (buffered by send() calls)
      // Send buffered data to multicast group
      ssize_t n = ::send(socket_fd_,                  // Socket file descriptor
                         outbound_data_.data(),       // Pointer to send buffer
                         next_send_valid_index_,      // Number of bytes to send
                         MSG_DONTWAIT | MSG_NOSIGNAL);  // Flags
      // ::send() (global namespace) vs send() (member function)
      // MSG_DONTWAIT: Non-blocking (return immediately if can't send)
      // MSG_NOSIGNAL: Don't raise SIGPIPE if connection broken
      
      // ssize_t n = bytes sent (or -1 on error)
      // For UDP multicast: Either sends all or returns error (no partial sends typically)

      // Log send event
      logger_.log("%:% %() % send socket:% len:%\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  socket_fd_, n);  // Log actual bytes sent
    }
    
    // Reset send buffer (whether send succeeded or not)
    next_send_valid_index_ = 0;  // Buffer now empty
                                 // Next send() calls will write from beginning

    // Return whether data was received (used by caller to know if callback was invoked)
    return (n_rcv > 0);
  }

  /*
   * BUFFER OUTGOING DATA (HOT PATH)
   * ===============================
   * Copy data to send buffer (actual send happens in sendAndRecv()).
   */
  auto McastSocket::send(const void *data, size_t len) noexcept -> void {
    // Copy data into send buffer at next available position
    memcpy(outbound_data_.data() + next_send_valid_index_,  // Destination: end of buffer
           data,                                             // Source: caller's data
           len);                                             // Number of bytes to copy
    // memcpy: Fast memory copy (~5-20 ns per 100 bytes)
    // Direct memory-to-memory transfer (no syscalls)
    
    // Update index to reflect new data
    next_send_valid_index_ += len;  // Advance by len bytes
                                    // Next send() call will append after this
    
    // Validate buffer not overflowed
    ASSERT(next_send_valid_index_ < McastBufferSize,  // Check: have we exceeded buffer size?
           "Mcast socket buffer filled up and sendAndRecv() not called.");
    // If this fires: Application is calling send() faster than sendAndRecv()
    // Solution: Call sendAndRecv() more frequently, or increase buffer size
    // In production: This indicates a design problem (buffer too small or not flushing enough)
  }
}

/*
 * IMPLEMENTATION NOTES
 * ====================
 * 
 * 1. NON-BLOCKING I/O:
 *    - All operations use MSG_DONTWAIT
 *    - Returns immediately if no data available
 *    - Allows busy-polling in tight loop
 *    - No threads blocked waiting for I/O
 * 
 * 2. BATCHING:
 *    - send() just copies to buffer (fast)
 *    - sendAndRecv() flushes all buffered data at once
 *    - Reduces syscall overhead (fewer send() syscalls)
 *    - Typical: 10-100 send() calls per sendAndRecv()
 * 
 * 3. ERROR HANDLING:
 *    - recv() < 0: No data available (normal with MSG_DONTWAIT)
 *    - send() < 0: Socket error or would block
 *    - No retries: Multicast is best-effort (application handles)
 * 
 * 4. BUFFER MANAGEMENT:
 *    - Receive: Callback must reset next_rcv_valid_index_
 *    - Send: Automatically reset to 0 after flush
 *    - No circular buffer: Linear buffer reset after processing
 * 
 * 5. THREAD SAFETY:
 *    - NOT thread-safe (single-threaded design)
 *    - One thread calls send()/sendAndRecv()
 *    - Typical: Dedicated thread per socket
 * 
 * 6. PERFORMANCE:
 *    - send(): ~10-20 ns (just memcpy)
 *    - sendAndRecv(): ~100-500 ns (syscalls)
 *    - Hot path acceptable for market data
 */
