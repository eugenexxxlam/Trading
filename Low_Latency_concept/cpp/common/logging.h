#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <string_view>
#include <cstring>

#include "macros.h"
#include "lf_queue.h"
#include "time_utils.h"

namespace Common {
  constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

  enum class LogType : int8_t {
    CHAR = 0,
    INTEGER = 1,
    LONG_INTEGER = 2,
    LONG_LONG_INTEGER = 3,
    UNSIGNED_INTEGER = 4,
    UNSIGNED_LONG_INTEGER = 5,
    UNSIGNED_LONG_LONG_INTEGER = 6,
    FLOAT = 7,
    DOUBLE = 8
  };

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
    } u_;
  };

  class Logger final {
  public:
    explicit Logger(const std::string& file_name)
        : file_name_(file_name)
        , queue_(LOG_QUEUE_SIZE)
        , running_(true)
    {
      file_.open(file_name);
      if (!file_.is_open()) {
        FATAL("Could not open log file: " + file_name);
      }
      
      logger_thread_ = std::make_unique<std::thread>(
          [this]() { flushQueue(); }
      );
    }

    ~Logger() {
      std::string time_str;
      std::cerr << getCurrentTimeStr(&time_str) 
                << " Flushing and closing Logger for " << file_name_ 
                << std::endl;

      running_.store(false, std::memory_order_release);
      
      if (logger_thread_ && logger_thread_->joinable()) {
        logger_thread_->join();
      }

      file_.close();
      std::cerr << getCurrentTimeStr(&time_str) 
                << " Logger for " << file_name_ << " exiting." 
                << std::endl;
    }

    void flushQueue() noexcept {
      while (running_.load(std::memory_order_acquire)) {
        bool wrote_data = false;
        
        while (auto elem = queue_.getNextToRead()) {
          writeElement(elem);
          queue_.updateReadIndex();
          wrote_data = true;
        }

        if (wrote_data) {
          file_.flush();
        } else {
          using namespace std::literals::chrono_literals;
          std::this_thread::sleep_for(10ms);
        }
      }

      // Final drain - write all remaining entries
      while (auto elem = queue_.getNextToRead()) {
        writeElement(elem); 
        queue_.updateReadIndex();
      }
      file_.flush();
    }

    // pushValue methods - keep nodiscard for internal use
    void pushValue(const LogElement& log_element) noexcept {
      if (queue_.isFull()) {
        std::cerr << "WARNING: Logger queue full\n";
        return;
      }
      
      *queue_.getNextToWriteTo() = log_element;
      queue_.updateWriteIndex();
    }

    void pushValue(char value) noexcept {
      pushValue(LogElement{LogType::CHAR, {.c = value}});
    }

    void pushValue(int value) noexcept {
      pushValue(LogElement{LogType::INTEGER, {.i = value}});
    }

    void pushValue(long value) noexcept {
      pushValue(LogElement{LogType::LONG_INTEGER, {.l = value}});
    }

    void pushValue(long long value) noexcept {
      pushValue(LogElement{LogType::LONG_LONG_INTEGER, {.ll = value}});
    }

    void pushValue(unsigned value) noexcept {
      pushValue(LogElement{LogType::UNSIGNED_INTEGER, {.u = value}});
    }

    void pushValue(unsigned long value) noexcept {
      pushValue(LogElement{LogType::UNSIGNED_LONG_INTEGER, {.ul = value}});
    }

    void pushValue(unsigned long long value) noexcept {
      pushValue(LogElement{
          LogType::UNSIGNED_LONG_LONG_INTEGER, {.ull = value}});
    }

    void pushValue(float value) noexcept {
      pushValue(LogElement{LogType::FLOAT, {.f = value}});
    }

    void pushValue(double value) noexcept {
      pushValue(LogElement{LogType::DOUBLE, {.d = value}});
    }

    void pushValue(std::string_view value) noexcept {
      if (value.empty()) {
        return;
      }
      
      for (char c : value) {
        pushValue(c);
      }
    }

    void pushValue(const char* value) noexcept {
      if (!value) {
        pushValue("(null)"); 
        return;
      }
      pushValue(std::string_view(value));
    }

    void pushValue(const std::string& value) noexcept {
      pushValue(std::string_view(value));
    }

    // log methods - NO nodiscard to maintain API compatibility
    template<typename T, typename... Args>
    void log(std::string_view format, const T& value, Args... args) noexcept {
      while (!format.empty()) {
        if (format[0] == '%') {
          if (format.size() > 1 && format[1] == '%') {
            pushValue('%');
            format = format.substr(2);
            continue;
          } else {
            pushValue(value);
            log(format.substr(1), args...);  // Skip only the '%'
            return;
          }
        }
        
        pushValue(format[0]);
        format = format.substr(1);
      }
      
      if constexpr (sizeof...(args) > 0) {
        FATAL("Logger: extra arguments provided to log()");
      }
    }

    void log(std::string_view format) noexcept {
      while (!format.empty()) {
        if (format[0] == '%') {
          if (format.size() > 1 && format[1] == '%') {
            pushValue('%');
            format = format.substr(2);
          } else {
            FATAL("Logger: missing arguments to log()");
            return;
          }
        } else {
          pushValue(format[0]);
          format = format.substr(1);
        }
      }
    }

    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;  
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

  private:
    void writeElement(const LogElement* elem) noexcept {
      if (!elem) return;
      
      switch (elem->type_) {
        case LogType::CHAR:
          file_ << elem->u_.c;
          break;
        case LogType::INTEGER:
          file_ << elem->u_.i;
          break;
        case LogType::LONG_INTEGER:
          file_ << elem->u_.l;
          break;
        case LogType::LONG_LONG_INTEGER:
          file_ << elem->u_.ll;
          break;
        case LogType::UNSIGNED_INTEGER:
          file_ << elem->u_.u;
          break;
        case LogType::UNSIGNED_LONG_INTEGER:
          file_ << elem->u_.ul;
          break;
        case LogType::UNSIGNED_LONG_LONG_INTEGER:
          file_ << elem->u_.ull;
          break;
        case LogType::FLOAT:
          file_ << elem->u_.f;
          break;
        case LogType::DOUBLE:
          file_ << elem->u_.d;
          break;
      }
    }

    const std::string file_name_;
    std::ofstream file_;
    LFQueue<LogElement> queue_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> logger_thread_;  
  };
}