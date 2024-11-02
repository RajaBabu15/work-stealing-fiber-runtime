#include "fiber.hpp"
#include "scheduler.hpp"

#include <sys/mman.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>

thread_local WorkerContext* tl_worker        = nullptr;
thread_local Fiber*         tl_current_fiber = nullptr;

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

void record_switch_ns(uint64_t ns) {
    if (!tl_worker) return;
    tl_worker->switch_ns_sum.fetch_add(ns, std::memory_order_relaxed);
    tl_worker->switch_sample_count.fetch_add(1, std::memory_order_relaxed);
}

static void timed_switch(void** from_sp, void* to_sp) {
    uint64_t t0 = now_ns();
    fiber_switch(from_sp, to_sp);
    record_switch_ns(now_ns() - t0);
}

extern "C" void fiber_trampoline() {
    Fiber* f = tl_current_fiber;
    try {
        f->func();
    } catch (...) {
        f->state = Fiber::State::Done;
        timed_switch(&f->saved_sp, tl_worker->scheduler_sp);
        __builtin_unreachable();
    }
    f->state = Fiber::State::Done;
    timed_switch(&f->saved_sp, tl_worker->scheduler_sp);
    __builtin_unreachable();
}

Fiber* alloc_fiber(size_t stack_size) {
    Fiber* f = new Fiber{};
    size_t total = kGuardPageSize + stack_size;
    void* base = mmap(nullptr, total,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON,
                      -1, 0);
    if (base == MAP_FAILED) {
        delete f;
        throw std::runtime_error("mmap failed");
    }
    mprotect(base, kGuardPageSize, PROT_NONE);
    f->stack_base = base;
    f->stack_size = stack_size;
    return f;
}

void free_fiber(Fiber* f) {
    if (f->stack_base)
        munmap(f->stack_base, kGuardPageSize + f->stack_size);
    delete f;
}

void init_fiber_stack(Fiber* f, std::function<void()> fn) {
    f->func = std::move(fn);
    char* stack_top =
        static_cast<char*>(f->stack_base) + kGuardPageSize + f->stack_size;

#if defined(__aarch64__) || defined(__arm64__)
    char* frame = stack_top - kCtxFrameBytes;
    std::memset(frame, 0, kCtxFrameBytes);
    // stp x29, x30 @ 0x90 — x30 (LR) holds the trampoline target
    reinterpret_cast<uint64_t*>(frame)[0x98 / 8] =
        reinterpret_cast<uint64_t>(&fiber_trampoline);
    f->saved_sp = frame;

#elif defined(__x86_64__)
    char* frame = stack_top - kCtxFrameBytes;
    std::memset(frame, 0, kCtxFrameBytes);
    // ret target after 6 GPR pops (offset 160) + 160 xmm bytes
    reinterpret_cast<uint64_t*>(frame)[160 / 8 + 6] =
        reinterpret_cast<uint64_t>(&fiber_trampoline);
    f->saved_sp = frame;

#else
    #error "Unsupported architecture"
#endif
}

void fiber_yield() {
    Fiber* f = tl_current_fiber;
    f->state = Fiber::State::Ready;
    timed_switch(&f->saved_sp, tl_worker->scheduler_sp);
}