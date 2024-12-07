#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

struct WorkerContext;

extern "C" void fiber_switch(void** from_sp, void* to_sp);
extern "C" void fiber_trampoline();

extern thread_local WorkerContext* tl_worker;
extern thread_local struct Fiber*  tl_current_fiber;

static constexpr size_t kFiberStackSize = 512 * 1024;
static constexpr size_t kGuardPageSize  = 4096;

#if defined(__aarch64__) || defined(__arm64__)
static constexpr size_t kCtxFrameBytes = 0xb0;
#elif defined(__x86_64__)
static constexpr size_t kCtxFrameBytes = 208;  // 160 xmm + 48 gpr (6 pushes) + 8 ret
#else
#error "Unsupported architecture"
#endif

struct Fiber {
    void*    saved_sp{nullptr};
    void*    stack_base{nullptr};
    size_t   stack_size{0};
    enum class State : uint8_t { Ready, Running, Done } state{State::Ready};
    uint64_t id{0};
    std::function<void()> func;
};

Fiber* alloc_fiber(size_t stack_size = kFiberStackSize);
void   free_fiber(Fiber* f);
void   init_fiber_stack(Fiber* f, std::function<void()> fn);
void   fiber_yield();

void record_switch_ns(uint64_t ns);