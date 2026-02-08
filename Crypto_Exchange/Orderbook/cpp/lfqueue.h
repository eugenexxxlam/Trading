#pragma once

#include <vector>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <cstdlib>

// Helper macros
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

inline void FATAL(const std::string &msg) noexcept {
  std::cerr << "FATAL : " << msg << std::endl;
  exit(EXIT_FAILURE);
}

namespace Common {
  /**
   * Lock-Free Single-Producer Single-Consumer (SPSC) Queue
   * 
   * THREAD SAFETY: Exactly ONE producer and ONE consumer thread.
   * Multiple producers or consumers cause data corruption.
   * 
   * DESIGN: Indices grow monotonically (0→SIZE_MAX), masked ONLY when
   * accessing array. Size is trivial via unsigned arithmetic.
   */
  template<typename T>
  class LFQueue final {
  public:
    explicit LFQueue(std::size_t num_elems) {
      if (num_elems == 0) {
        FATAL("LFQueue: num_elems must be > 0");
      }
      
      capacity_ = roundUpToPowerOf2(num_elems);
      mask_ = capacity_ - 1;
      store_.resize(capacity_);
    }

    [[nodiscard]] auto getNextToWriteTo() noexcept -> T* {
      const auto idx = next_write_index_.load(std::memory_order_relaxed);
      return &store_[idx & mask_];
    }

    void updateWriteIndex() noexcept {
      next_write_index_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] auto getNextToRead() const noexcept -> const T* {
      const auto read_idx = next_read_index_.load(std::memory_order_relaxed);
      const auto write_idx = next_write_index_.load(std::memory_order_acquire);
      
      if (read_idx == write_idx) {
        return nullptr;
      }
      
      return &store_[read_idx & mask_];
    }

    void updateReadIndex() noexcept {
      next_read_index_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] auto size() const noexcept -> size_t {
      const auto write_idx = next_write_index_.load(std::memory_order_acquire);
      const auto read_idx = next_read_index_.load(std::memory_order_relaxed);
      return write_idx - read_idx;
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
      const auto read_idx = next_read_index_.load(std::memory_order_relaxed);
      const auto write_idx = next_write_index_.load(std::memory_order_acquire);
      return read_idx == write_idx;
    }

    [[nodiscard]] auto isFull() const noexcept -> bool {
      const auto write_idx = next_write_index_.load(std::memory_order_relaxed);
      const auto read_idx = next_read_index_.load(std::memory_order_acquire);
      
      // Queue is full when next write position would hit read position
      return ((write_idx + 1) & mask_) == (read_idx & mask_);
    }

    [[nodiscard]] auto capacity() const noexcept -> size_t {
      return capacity_ - 1;
    }

    ~LFQueue() = default;
    LFQueue() = delete;
    LFQueue(const LFQueue&) = delete;
    LFQueue(LFQueue&&) = delete;
    LFQueue& operator=(const LFQueue&) = delete;
    LFQueue& operator=(LFQueue&&) = delete;

  private:
    static constexpr size_t roundUpToPowerOf2(size_t n) noexcept {
      if (n == 0) return 1;
      if (n > (size_t(1) << (sizeof(size_t) * 8 - 2))) {
        return size_t(1) << (sizeof(size_t) * 8 - 1);
      }
      size_t power = 1;
      while (power < n) {
        power <<= 1;
      }
      return power;
    }

    static constexpr size_t CACHE_LINE = 64;

    size_t capacity_;
    size_t mask_;
    std::vector<T> store_;

    alignas(CACHE_LINE) std::atomic<size_t> next_write_index_{0};
    alignas(CACHE_LINE) std::atomic<size_t> next_read_index_{0};
  };
}
