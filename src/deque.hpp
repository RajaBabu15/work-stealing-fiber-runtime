#pragma once
// Work-stealing deque — Chase-Lev algorithm.
// Ref: Lê et al., "Correct and Efficient Work-Stealing for Weak Memory Models",
//      PPoPP 2013.
//
// push/pop are owner-only (bottom end, LIFO).
// steal is available to any thread (top end, FIFO).
// Fixed-size ring buffer; Capacity must be a power of two.

#include <atomic>
#include <cassert>
#include <cstddef>

template <typename T, size_t Capacity = 65536>
class WorkStealingDeque {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t MASK = Capacity - 1;

    alignas(64) std::atomic<size_t> bottom_{0};
    alignas(64) std::atomic<size_t> top_{0};
    std::atomic<T*> buf_[Capacity]{};

public:
    void push(T* item) {
        size_t b = bottom_.load(std::memory_order_relaxed);
        assert(b - top_.load(std::memory_order_relaxed) < Capacity);
        buf_[b & MASK].store(item, std::memory_order_relaxed);
        // Release fence: make the buf_ write visible before bottom_ increments.
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    T* pop() {
        size_t b = bottom_.load(std::memory_order_relaxed);
        if (b == top_.load(std::memory_order_relaxed))
            return nullptr;

        b--;
        bottom_.store(b, std::memory_order_relaxed);
        // seq_cst fence between the bottom decrement and the top load prevents
        // owner and a concurrent stealer from both claiming the last element.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        size_t t = top_.load(std::memory_order_relaxed);

        if (t < b)
            return buf_[b & MASK].load(std::memory_order_relaxed);

        if (t > b) {
            bottom_.store(b + 1, std::memory_order_relaxed);
            return nullptr;
        }

        // Last element — race the stealer.
        T* item = buf_[b & MASK].load(std::memory_order_relaxed);
        bool won = top_.compare_exchange_strong(
            t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
        bottom_.store(b + 1, std::memory_order_relaxed);
        return won ? item : nullptr;
    }

    T* steal() {
        size_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        size_t b = bottom_.load(std::memory_order_acquire);
        if (t >= b) return nullptr;

        T* item = buf_[t & MASK].load(std::memory_order_relaxed);
        if (!top_.compare_exchange_strong(
                t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
            return nullptr;
        return item;
    }

    size_t size() const {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }

    bool empty() const { return size() == 0; }
};
