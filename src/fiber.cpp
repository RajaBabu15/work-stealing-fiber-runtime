#include "fiber.hpp"
#include "scheduler.hpp"

#include <sys/mman.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

thread_local WorkerContext* tl_worker        = nullptr;
thread_local Fiber*         tl_current_fiber = nullptr;

// Entered via 'ret' on the first switch-in. The initial stack frame is set up
// by init_fiber_stack() so that fiber_switch()'s final 'ret' lands here.
extern "C" void fiber_trampoline() {
    Fiber* f = tl_current_fiber;
    f->func();
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
    mprotect(base, kGuardPageSize, PROT_NONE);  // guard page at low address
    f->stack_base = base;
    f->stack_size = stack_size;
    return f;
}

void free_fiber(Fiber* f) {
    if (f->stack_base)
        munmap(f->stack_base, kGuardPageSize + f->stack_size);
    delete f;
}

// Set up a synthetic stack frame so that the first fiber_switch() into this
// fiber restores zeroed callee-saved registers and jumps to fiber_trampoline.
//
// ARM64 (AAPCS64): 6 stp pairs consume 96 bytes below stack_top.
// The last stp stores [x29, x30], so saved_sp[0]=x29=0, saved_sp[1]=x30=trampoline.
// After 6 ldp pairs sp returns to stack_top (16-byte aligned).
//
// x86_64 (SysV): 6 pushes consume 48 bytes; the call-pushed return address slot
// holds the trampoline. Layout at saved_sp:
//   [+0..+40] r15,r14,r13,r12,rbp,rbx = 0
//   [+48]     fiber_trampoline  (ret target)
//   [+56]     0  (sentinel; rsp after ret = stack_top-8, so (rsp+8)%16 = 0)
void init_fiber_stack(Fiber* f, std::function<void()> fn) {
    f->func = std::move(fn);
    char* stack_top =
        static_cast<char*>(f->stack_base) + kGuardPageSize + f->stack_size;

#if defined(__aarch64__) || defined(__arm64__)
    uint64_t* sp = reinterpret_cast<uint64_t*>(stack_top - 96);
    std::memset(sp, 0, 96);
    sp[1] = reinterpret_cast<uint64_t>(&fiber_trampoline);  // x30 / lr
    f->saved_sp = sp;

#elif defined(__x86_64__)
    uint64_t* sp = reinterpret_cast<uint64_t*>(stack_top - 64);
    std::memset(sp, 0, 64);
    sp[6] = reinterpret_cast<uint64_t>(&fiber_trampoline);
    f->saved_sp = sp;

#else
    #error "Unsupported architecture"
#endif
}

void fiber_yield() {
    Fiber* f = tl_current_fiber;
    f->state = Fiber::State::Ready;
    fiber_switch(&f->saved_sp, tl_worker->scheduler_sp);
}
