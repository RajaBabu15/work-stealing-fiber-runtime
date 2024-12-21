#include "fiber.hpp"
#include "scheduler.hpp"

#include <sys/mman.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

thread_local WorkerContext* tl_worker        = nullptr;
thread_local Fiber*         tl_current_fiber = nullptr;

// Switch-latency accounting is done once per resumption in the scheduler
// (worker_entry), NOT per yield — keeping the fiber_yield hot path free of
// clock reads. The scheduler hands the measured span here.
void record_switch_ns(uint64_t ns) {
    if (!tl_worker) return;
    tl_worker->switch_ns_sum.fetch_add(ns, std::memory_order_relaxed);
    tl_worker->switch_sample_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void fiber_trampoline() {
    Fiber* f = tl_current_fiber;
    try {
        f->func();
    } catch (...) {
        f->state = Fiber::State::Done;
        fiber_switch(&f->saved_sp, tl_worker->scheduler_sp);
        __builtin_unreachable();
    }
    f->state = Fiber::State::Done;
    fiber_switch(&f->saved_sp, tl_worker->scheduler_sp);
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
    static_assert(kCtxFrameBytes == 208, "must match context_switch_x86_64.S frame layout");
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
    fiber_switch(&f->saved_sp, tl_worker->scheduler_sp);
}