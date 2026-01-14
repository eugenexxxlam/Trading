#pragma once

#include <string>
#include <fstream>
#include <cstdio>

#include "macros.h"
#include "lf_queue.h"
#include "thread_utils.h"
#include "time_utils.h"

/*
 * OPTIMIZED LOGGER - ELEMENT-BASED ASYNC LOGGING FOR EXTREME PERFORMANCE
 * =======================================================================
 * 
 * PURPOSE:
 * An alternative async logger implementation that serializes each log element
 * (char, int, string) separately for potentially lower latency than string-based logging.
 * 
 * vs. REGULAR LOGGER (logging.h):
 * - Regular: Formats entire string in hot path, enqueues as string
 * - Optimized: Enqueues primitive elements, formats in background
 * - Trade-off: Lower hot path latency vs more complex implementation
 * 
 * PERFORMANCE CHARACTERISTICS:
 * - Hot path: ~5-15 ns per element (vs ~10-20 ns for string formatting)
 * - Background: Formatting happens in separate thread
 * - Memory: 8MB queue = ~128K-1M log elements (depends on type mix)
 * 
 * WHEN TO USE:
 * - Absolute minimum hot path latency required
 * - Simple log messages (primitives, not complex formats)
 * - High-frequency logging (millions/second)
 * 
 * WHEN NOT TO USE:
 * - Complex formatting needs
 * - Memory constrained (larger per-element overhead)
 * - Regular logger sufficient (10-20 ns usually acceptable)
 */

namespace OptCommon {
  // Queue size: 8 MB of log elements
  constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

  /*
   * LOG TYPE ENUMERATION
   * Identifies type of data in LogElement union
   */
  enum class LogType : int8_t {
    CHAR = 0,
    INTEGER = 1,
    LONG_INTEGER = 2,
    LONG_LONG_INTEGER = 3,
    UNSIGNED_INTEGER = 4,
    UNSIGNED_LONG_INTEGER = 5,
    UNSIGNED_LONG_LONG_INTEGER = 6,
    FLOAT = 7,
    DOUBLE = 8,
    STRING = 9
  };

  /*
   * LOG ELEMENT STRUCTURE
   * Single log entry with type tag and union of possible values
   * Size: 1 byte (type) + 256 bytes (union) = 257 bytes
   */
  struct LogElement {
    LogType type_ = LogType::CHAR;
    union {
      char c;
      int i;
      long l;
      long long ll;
      unsigned u;
      unsigned long ul;
      unsigned long long ull;
      float f;
      double d;
      char s[256];  // Fixed-size string buffer
    } u_;
  };

  class OptLogger final {
  public:
    /*
     * BACKGROUND FLUSH THREAD
     * Continuously reads from queue and writes to file
     */
    auto flushQueue() noexcept {
      while (running_) {
        for (auto next = queue_.getNextToRead(); queue_.size() && next; next = queue_.getNextToRead()) {
          // Switch on element type and write to file
          switch (next->type_) {
            case LogType::CHAR:
              file_ << next->u_.c;
              break;
            case LogType::INTEGER:
              file_ << next->u_.i;
              break;
            // ... (all other types)
            case LogType::STRING:
              file_ << next->u_.s;
              break;
          }
          queue_.updateReadIndex();  // Mark as consumed
        }
        file_.flush();  // Flush to disk
        
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(10ms);  // Sleep between flushes
      }
    }

    /*
     * CONSTRUCTOR
     * Opens file and starts background thread
     */
    explicit OptLogger(const std::string &file_name)
        : file_name_(file_name), queue_(LOG_QUEUE_SIZE) {
      file_.open(file_name);
      ASSERT(file_.is_open(), "Could not open log file:" + file_name);
      logger_thread_ = Common::createAndStartThread(-1, "Common/OptLogger " + file_name_, [this]() { flushQueue(); });
      ASSERT(logger_thread_ != nullptr, "Failed to start OptLogger thread.");
    }

    /*
     * DESTRUCTOR
     * Waits for queue to drain, stops thread, closes file
     */
    ~OptLogger() {
      std::string time_str;
      std::cerr << Common::getCurrentTimeStr(&time_str) << " Flushing and closing OptLogger for " << file_name_ << std::endl;

      while (queue_.size()) {  // Wait for queue to empty
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(1s);
      }
      running_ = false;  // Signal thread to stop
      logger_thread_->join();  // Wait for thread to finish

      file_.close();
      std::cerr << Common::getCurrentTimeStr(&time_str) << " OptLogger for " << file_name_ << " exiting." << std::endl;
    }

    /*
     * PUSH VALUE METHODS
     * Enqueue different types to the lock-free queue
     * Hot path: ~5-15 ns per call
     */
    auto pushValue(const LogElement &log_element) noexcept {
      *(queue_.getNextToWriteTo()) = log_element;
      queue_.updateWriteIndex();
    }

    auto pushValue(const char value) noexcept {
      pushValue(LogElement{LogType::CHAR, {.c = value}});
    }

    auto pushValue(const int value) noexcept {
      pushValue(LogElement{LogType::INTEGER, {.i = value}});
    }

    // ... (more overloads for each type)

    auto pushValue(const std::string &value) noexcept {
      pushValue(value.c_str());
    }

    /*
     * VARIADIC LOG METHOD
     * Parses format string and enqueues elements
     */
    template<typename T, typename... A>
    auto log(const char *s, const T &value, A... args) noexcept {
      while (*s) {
        if (*s == '%') {
          if (UNLIKELY(*(s + 1) == '%')) {
            ++s;
          } else {
            pushValue(value);  // Enqueue value
            log(s + 1, args...);  // Recursive call with remaining args
            return;
          }
        }
        pushValue(*s++);  // Enqueue character
      }
      FATAL("extra arguments provided to log()");
    }

    auto log(const char *s) noexcept {
      while (*s) {
        if (*s == '%') {
          if (UNLIKELY(*(s + 1) == '%')) {
            ++s;
          } else {
            FATAL("missing arguments to log()");
          }
        }
        pushValue(*s++);
      }
    }

    // Deleted constructors
    OptLogger() = delete;
    OptLogger(const OptLogger &) = delete;
    OptLogger(const OptLogger &&) = delete;
    OptLogger &operator=(const OptLogger &) = delete;
    OptLogger &operator=(const OptLogger &&) = delete;

  private:
    const std::string file_name_;
    std::ofstream file_;
    Common::LFQueue<LogElement> queue_;
    std::atomic<bool> running_ = {true};
    std::thread *logger_thread_ = nullptr;
  };
}
