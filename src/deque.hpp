#pragma once
// Work-stealing deque — Chase-Lev algorithm with dynamic growth.
// Ref: Lê et al., "Correct and Efficient Work-Stealing for Weak Memory Models",
//      PPoPP 2013.
//
// push/pop are owner-only (bottom end, LIFO).
// steal is available to any thread (top end, FIFO).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

template <typename T>
class WorkStealingDeque {
    struct Buffer {
        std::atomic<T*>* slots{nullptr};
        size_t           capacity{0};
    };

    static size_t next_pow2(size_t n) {
        if (n < 2) return 2;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        if constexpr (sizeof(size_t) > 4)
            n |= n >> 32;
        return n + 1;
    }

    static bool is_pow2(size_t n) { return n >= 2 && (n & (n - 1)) == 0; }

    alignas(64) std::atomic<size_t>   bottom_{0};
    alignas(64) std::atomic<size_t>   top_{0};
    alignas(64) std::atomic<Buffer*>  buf_{nullptr};
    alignas(64) std::atomic<size_t>   capacity_{0};
    alignas(64) std::atomic<uint64_t> version_{0};

    // Buffers replaced by grow() are kept until destructor. Stealers may still
    // be reading a retired buffer's slots[] after buf_ is swapped.
    std::vector<Buffer*> retired_buffers_;

    static Buffer* alloc_buffer(size_t cap) {
        auto* b   = new Buffer;
        b->capacity = cap;
        b->slots    = new std::atomic<T*>[cap];
        for (size_t i = 0; i < cap; ++i)
            b->slots[i].store(nullptr, std::memory_order_relaxed);
        return b;
    }

    static void free_buffer(Buffer* b) {
        if (!b) return;
        delete[] b->slots;
        delete b;
    }

    void retire_buffer(Buffer* b) {
        if (b) retired_buffers_.push_back(b);
    }

    void grow_locked(size_t t, size_t b, Buffer* old, size_t old_cap) {
        size_t new_cap = old_cap * 2;
        Buffer* fresh  = alloc_buffer(new_cap);
        for (size_t i = t; i < b; ++i) {
            T* item = old->slots[i & (old_cap - 1)].load(std::memory_order_relaxed);
            fresh->slots[i & (new_cap - 1)].store(item, std::memory_order_relaxed);
        }
        version_.fetch_add(1, std::memory_order_release);
        capacity_.store(new_cap, std::memory_order_release);
        buf_.store(fresh, std::memory_order_release);
        version_.fetch_add(1, std::memory_order_release);
        retire_buffer(old);
    }

public:
    explicit WorkStealingDeque(size_t initial_capacity = 65536) {
        size_t cap = next_pow2(initial_capacity < 4096 ? 4096 : initial_capacity);
        Buffer* b  = alloc_buffer(cap);
        buf_.store(b, std::memory_order_relaxed);
        capacity_.store(cap, std::memory_order_relaxed);
    }

    ~WorkStealingDeque() {
        free_buffer(buf_.load(std::memory_order_relaxed));
        for (Buffer* b : retired_buffers_)
            free_buffer(b);
    }

    WorkStealingDeque(const WorkStealingDeque&)            = delete;
    WorkStealingDeque& operator=(const WorkStealingDeque&) = delete;

    void push(T* item) {
        for (;;) {
            uint64_t ver0 = version_.load(std::memory_order_acquire);
            Buffer*  buf  = buf_.load(std::memory_order_acquire);
            size_t   cap  = buf->capacity;
            size_t   b    = bottom_.load(std::memory_order_relaxed);
            size_t   t    = top_.load(std::memory_order_relaxed);

            if (b - t >= cap) {
                grow_locked(t, b, buf, cap);
                continue;
            }

            buf->slots[b & (cap - 1)].store(item, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            bottom_.store(b + 1, std::memory_order_relaxed);

            if (version_.load(std::memory_order_acquire) == ver0)
                return;
        }
    }

    T* pop() {
        for (;;) {
            uint64_t ver0 = version_.load(std::memory_order_acquire);
            Buffer*  buf  = buf_.load(std::memory_order_acquire);
            size_t   cap  = buf->capacity;

            size_t b0 = bottom_.load(std::memory_order_relaxed);
            if (b0 == top_.load(std::memory_order_relaxed))
                return nullptr;

            size_t b = b0 - 1;
            bottom_.store(b, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            size_t t = top_.load(std::memory_order_relaxed);

            if (t > b) {
                bottom_.store(b0, std::memory_order_relaxed);
                return nullptr;
            }

            T* item = buf->slots[b & (cap - 1)].load(std::memory_order_relaxed);

            if (t < b) {
                if (version_.load(std::memory_order_acquire) == ver0)
                    return item;
                bottom_.store(b0, std::memory_order_relaxed);
                continue;
            }

            // t == b: race with steal for the last element
            if (top_.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
                bottom_.store(b0, std::memory_order_relaxed);
                return item;
            }

            bottom_.store(b0, std::memory_order_relaxed);
            if (version_.load(std::memory_order_acquire) == ver0)
                return nullptr;
        }
    }

    T* steal() {
        for (;;) {
            uint64_t ver0 = version_.load(std::memory_order_acquire);
            Buffer*  buf  = buf_.load(std::memory_order_acquire);
            size_t   cap  = buf->capacity;

            size_t t = top_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            size_t b = bottom_.load(std::memory_order_acquire);
            if (t >= b) return nullptr;

            T* item = buf->slots[t & (cap - 1)].load(std::memory_order_relaxed);
            if (top_.compare_exchange_strong(
                    t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                return item;

            if (version_.load(std::memory_order_acquire) == ver0)
                return nullptr;
        }
    }

    size_t size() const {
        size_t b = bottom_.load(std::memory_order_relaxed);
        size_t t = top_.load(std::memory_order_relaxed);
        return b > t ? b - t : 0;
    }

    bool empty() const { return size() == 0; }
};