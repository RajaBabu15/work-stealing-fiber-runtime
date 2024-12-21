#include "scheduler.hpp"
#include "fiber.hpp"

#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

static inline uint64_t now_ns() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

static size_t deque_capacity_for(int num_fibers) {
    size_t n = static_cast<size_t>(num_fibers < 4096 ? 4096 : num_fibers);
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

Scheduler::Scheduler(SchedulerConfig cfg) : cfg_(cfg) {
    const size_t cap = deque_capacity_for(cfg_.num_fibers);
    workers_.reserve(cfg_.num_workers);
    for (int i = 0; i < cfg_.num_workers; i++) {
        workers_.push_back(std::make_unique<WorkerContext>(cap));
        workers_.back()->id = i;
    }
}

void Scheduler::enqueue_initial(Fiber* f) {
    workers_[0]->deque.push(f);
}

size_t Scheduler::queue_size(int worker_id) const {
    return workers_[static_cast<size_t>(worker_id)]->deque.size();
}

void Scheduler::worker_entry(int worker_id) {
    WorkerContext& ctx = *workers_[worker_id];
    tl_worker        = &ctx;
    tl_current_fiber = nullptr;

    while (!shutdown_.load(std::memory_order_relaxed)) {
        Fiber* f = ctx.deque.pop();
        if (!f) f = steal_from_others(worker_id);

        if (!f) {
            if (fibers_done_.load(std::memory_order_acquire)
                    >= static_cast<uint64_t>(cfg_.num_fibers)) {
                shutdown_.store(true, std::memory_order_release);
                break;
            }
            std::this_thread::yield();
            continue;
        }

        f->state         = Fiber::State::Running;
        tl_current_fiber = f;

        uint64_t t0 = now_ns();
        fiber_switch(&ctx.scheduler_sp, f->saved_sp);
        uint64_t dt = now_ns() - t0;   // one clock pair per resumption

        record_switch_ns(dt);
        ctx.ctx_switch_count.fetch_add(2, std::memory_order_relaxed);
        ctx.yield_ns_sum.fetch_add(dt, std::memory_order_relaxed);
        ctx.yield_sample_count.fetch_add(1, std::memory_order_relaxed);

        if (f->state == Fiber::State::Done) {
            fibers_done_.fetch_add(1, std::memory_order_release);
            free_fiber(f);
        } else {
            ctx.deque.push(f);
        }
    }
}

Fiber* Scheduler::steal_from_others(int self_id) {
    int n = cfg_.num_workers;
    if (n <= 1) return nullptr;

    static thread_local std::mt19937_64 rng{std::random_device{}()};

    for (int tries = 0; tries < n - 1; ++tries) {
        int victim = static_cast<int>(rng() % static_cast<uint64_t>(n));
        if (victim == self_id) continue;
        Fiber* f = workers_[victim]->deque.steal();
        if (f) {
            workers_[self_id]->steal_count.fetch_add(1, std::memory_order_relaxed);
            return f;
        }
    }
    return nullptr;
}

void Scheduler::run_reporter() {
    std::printf("\n%-10s", "Time(ms)");
    for (int i = 0; i < cfg_.num_workers; i++)
        std::printf("  W%-2d(sw/s)", i);
    std::printf("  Queue-0  Done/Total\n");
    int w = 10 + cfg_.num_workers * 11 + 22;
    for (int i = 0; i < w; i++) std::putchar('-');
    std::putchar('\n');
    std::fflush(stdout);

    std::vector<uint64_t> prev_cs(cfg_.num_workers, 0);
    uint64_t prev_ns = start_ns_;

    while (!shutdown_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t cur_ns     = now_ns();
        uint64_t elapsed_ms = (cur_ns - start_ns_) / 1'000'000;
        uint64_t done       = fibers_done_.load(std::memory_order_relaxed);
        double   dt         = (cur_ns - prev_ns) / 1e9;
        prev_ns = cur_ns;

        std::printf("%-10llu", (unsigned long long)elapsed_ms);
        for (int i = 0; i < cfg_.num_workers; i++) {
            uint64_t cs    = workers_[i]->ctx_switch_count.load(std::memory_order_relaxed);
            double   ksps  = dt > 0 ? (cs - prev_cs[i]) / dt / 1000.0 : 0.0;
            prev_cs[i] = cs;
            std::printf("  %8.0f K", ksps);
        }
        std::printf("  %7zu  %llu/%d\n",
                     workers_[0]->deque.size(),
                     (unsigned long long)done, cfg_.num_fibers);
        std::fflush(stdout);

        if (done >= static_cast<uint64_t>(cfg_.num_fibers)) break;
    }
}

void Scheduler::run() {
    start_ns_ = now_ns();
    std::thread reporter([this]{ run_reporter(); });
    threads_.reserve(cfg_.num_workers);
    for (int i = 0; i < cfg_.num_workers; i++)
        threads_.emplace_back([this, i]{ worker_entry(i); });
    for (auto& t : threads_) t.join();
    shutdown_.store(true, std::memory_order_release);
    reporter.join();
}

Scheduler::Stats Scheduler::collect_stats() const {
    Stats s;
    s.per_worker_ctx_switches.resize(cfg_.num_workers);
    s.per_worker_steals.resize(cfg_.num_workers);
    s.final_queue_sizes.resize(cfg_.num_workers);
    for (int i = 0; i < cfg_.num_workers; i++) {
        uint64_t cs = workers_[i]->ctx_switch_count.load(std::memory_order_relaxed);
        uint64_t st = workers_[i]->steal_count.load(std::memory_order_relaxed);
        s.per_worker_ctx_switches[i] = cs;
        s.per_worker_steals[i]       = st;
        s.final_queue_sizes[i]       = workers_[i]->deque.size();
        s.total_ctx_switches        += cs;
        s.total_steals              += st;
        s.total_yield_ns            += workers_[i]->yield_ns_sum.load(std::memory_order_relaxed);
        s.total_yield_samples       += workers_[i]->yield_sample_count.load(std::memory_order_relaxed);
        s.total_switch_ns           += workers_[i]->switch_ns_sum.load(std::memory_order_relaxed);
        s.total_switch_samples      += workers_[i]->switch_sample_count.load(std::memory_order_relaxed);
    }
    return s;
}