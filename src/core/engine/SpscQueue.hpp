#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace vd {

// Lock-free single-producer / single-consumer queue.
// T must be trivially copyable; capacity must be a power of two.
template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue requires trivially copyable elements");

public:
    explicit SpscQueue(std::size_t capacityPow2)
        : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    // Any thread (only when no producer/consumer is running).
    void reset() {
        readPos_.store(0, std::memory_order_relaxed);
        writePos_.store(0, std::memory_order_relaxed);
    }

    std::size_t capacity() const { return mask_ + 1; }

    // Consumer thread.
    std::size_t available() const {
        return writePos_.load(std::memory_order_acquire) - readPos_.load(std::memory_order_acquire);
    }

    // Producer thread.
    std::size_t space() const { return capacity() - available(); }

    // Producer thread. Returns how many elements were actually written.
    std::size_t write(const T* src, std::size_t count) {
        const std::size_t w = writePos_.load(std::memory_order_relaxed);
        const std::size_t free = capacity() - (w - readPos_.load(std::memory_order_acquire));
        if (count > free) count = free;
        const std::size_t i = w & mask_;
        const std::size_t first = (capacity() - i < count) ? (capacity() - i) : count;
        std::memcpy(&buf_[i], src, first * sizeof(T));
        if (count > first) std::memcpy(&buf_[0], src + first, (count - first) * sizeof(T));
        writePos_.store(w + count, std::memory_order_release);
        return count;
    }

    // Consumer thread. Returns how many elements were actually read.
    std::size_t read(T* dst, std::size_t count) {
        const std::size_t r = readPos_.load(std::memory_order_relaxed);
        const std::size_t avail = writePos_.load(std::memory_order_acquire) - r;
        if (count > avail) count = avail;
        const std::size_t i = r & mask_;
        const std::size_t first = (capacity() - i < count) ? (capacity() - i) : count;
        std::memcpy(dst, &buf_[i], first * sizeof(T));
        if (count > first) std::memcpy(dst + first, &buf_[0], (count - first) * sizeof(T));
        readPos_.store(r + count, std::memory_order_release);
        return count;
    }

    // Consumer thread.
    std::size_t discard(std::size_t count) {
        const std::size_t r = readPos_.load(std::memory_order_relaxed);
        const std::size_t avail = writePos_.load(std::memory_order_acquire) - r;
        if (count > avail) count = avail;
        readPos_.store(r + count, std::memory_order_release);
        return count;
    }

    // Producer thread (single element).
    bool push(const T& v) { return write(&v, 1) == 1; }

    // Consumer thread (single element).
    bool pop(T& v) { return read(&v, 1) == 1; }

private:
    std::vector<T> buf_;
    std::size_t mask_;
    alignas(64) std::atomic<std::size_t> readPos_{0};
    alignas(64) std::atomic<std::size_t> writePos_{0};
};

// Interleaved audio samples between device callbacks.
using RingBuffer = SpscQueue<float>;

} // namespace vd
