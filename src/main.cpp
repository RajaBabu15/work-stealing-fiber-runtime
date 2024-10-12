#include "fiber.hpp"
#include "scheduler.hpp"

#include <sys/resource.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(__aarch64__) || defined(__arm64__)
static constexpr const char* kArch    = "arm64";
static constexpr const char* kAsmDesc = "6xstp + sp-swap + 6xldp + ret (16 insns)";
#elif defined(__x86_64__)
static constexpr const char* kArch    = "x86_64";
static constexpr const char* kAsmDesc = "6xpush + rsp-swap + 6xpop + ret (15 insns)";
#else
static constexpr const char* kArch    = "unknown";
static constexpr const char* kAsmDesc = "unknown";
#endif

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "  --workers      N   OS worker threads     (default: hardware_concurrency)\n"
        "  --fibers       N   total fibers          (default: 10000)\n"
        "  --yield-every  N   iterations per yield  (default: 1000)\n"
        "  --rounds       N   yields per fiber      (default: 10)\n"
        "  --stack-kb     N   stack per fiber in KB (default: 512)\n"
        "  --latency-probe    measure raw ctx-switch latency\n",
        prog);
}

static void sep(char c, int n) {
    for (int i = 0; i < n; i++) std::putchar(c);
    std::putchar('\n');
}

static void run_latency_probe() {
    sep('=', 60);
    std::printf("  Latency probe — 1 worker, 100 fibers, 10 000 rounds\n");
    std::printf("  arch: %s  asm: %s\n", kArch, kAsmDesc);
    sep('-', 60);
    std::fflush(stdout);

    SchedulerConfig cfg;
    cfg.num_workers = 1;
    cfg.num_fibers  = 100;
    cfg.yield_every = 1;
    cfg.rounds      = 10000;

    struct rusage ru0{}, ru1{};
    getrusage(RUSAGE_SELF, &ru0);
    auto t0 = std::chrono::steady_clock::now();

    Scheduler sched(cfg);
    for (int i = 0; i < cfg.num_fibers; i++) {
        Fiber* f = alloc_fiber();
        f->id    = (uint64_t)i;
        const int ro = cfg.rounds;
        init_fiber_stack(f, [ro]() {
            for (int r = 0; r < ro; ++r) fiber_yield();
        });
        sched.workers_[0]->deque.push(f);
    }
    sched.run();

    auto t1 = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &ru1);

    double wall      = std::chrono::duration<double>(t1 - t0).count();
    auto   stats     = sched.collect_stats();
    double lat_ns    = stats.total_yield_samples
                     ? (double)stats.total_yield_ns / stats.total_yield_samples / 2.0
                     : 0.0;

    std::printf("\n");
    sep('=', 60);
    std::printf("  ctx switches   : %llu\n",
                (unsigned long long)stats.total_ctx_switches);
    std::printf("  latency (1-way): %.1f ns\n", lat_ns);
    std::printf("  throughput     : %.2f M yields/sec\n",
                (double)cfg.num_fibers * cfg.rounds / wall / 1e6);
    std::printf("  OS vol. csw    : %ld\n", ru1.ru_nvcsw - ru0.ru_nvcsw);
    sep('=', 60);
    std::printf("\n");
}

int main(int argc, char** argv) {
    SchedulerConfig cfg;
    cfg.num_workers = static_cast<int>(std::thread::hardware_concurrency());
    size_t stack_kb      = 512;
    bool   latency_probe = false;

    for (int i = 1; i < argc; i++) {
        if      (!std::strcmp(argv[i], "--workers")      && i+1 < argc) cfg.num_workers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--fibers")       && i+1 < argc) cfg.num_fibers  = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--yield-every")  && i+1 < argc) cfg.yield_every = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--rounds")       && i+1 < argc) cfg.rounds      = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--stack-kb")     && i+1 < argc) stack_kb        = (size_t)std::atoll(argv[++i]);
        else if (!std::strcmp(argv[i], "--latency-probe"))               latency_probe   = true;
        else if (!std::strcmp(argv[i], "--help"))        { print_usage(argv[0]); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 1; }
    }

    if (latency_probe) { run_latency_probe(); return 0; }

    const size_t   stack_size   = stack_kb * 1024;
    const uint64_t total_yields = (uint64_t)cfg.num_fibers * cfg.rounds;

    sep('=', 60);
    std::printf("  work-stealing fiber runtime\n");
    sep('=', 60);
    std::printf("  arch        : %s\n", kArch);
    std::printf("  asm         : %s\n", kAsmDesc);
    sep('-', 60);
    std::printf("  workers     : %d\n",  cfg.num_workers);
    std::printf("  fibers      : %d\n",  cfg.num_fibers);
    std::printf("  yield every : %d iters\n", cfg.yield_every);
    std::printf("  rounds      : %d\n",  cfg.rounds);
    std::printf("  stack       : %zu KB + 4 KB guard\n", stack_kb);
    std::printf("  total yields: %llu\n", (unsigned long long)total_yields);
    std::printf("  start       : all %d fibers on worker 0\n", cfg.num_fibers);
    sep('-', 60);
    std::fflush(stdout);

    struct rusage ru0{}, ru1{};
    getrusage(RUSAGE_SELF, &ru0);
    auto wall_start = std::chrono::steady_clock::now();

    Scheduler sched(cfg);
    const int ye = cfg.yield_every, ro = cfg.rounds;
    for (int i = 0; i < cfg.num_fibers; i++) {
        Fiber* f = alloc_fiber(stack_size);
        f->id    = (uint64_t)i;
        init_fiber_stack(f, [ye, ro]() {
            volatile uint64_t acc = 0;
            for (int round = 0; round < ro; ++round) {
                for (int j = 0; j < ye; ++j) acc += (uint64_t)j * j;
                fiber_yield();
            }
            (void)acc;
        });
        sched.workers_[0]->deque.push(f);
    }

    std::printf("\n  worker 0 queue: %zu  (workers 1..%d will steal)\n\n",
                sched.workers_[0]->deque.size(), cfg.num_workers - 1);
    std::fflush(stdout);

    sched.run();

    auto wall_end = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &ru1);

    double wall  = std::chrono::duration<double>(wall_end - wall_start).count();
    auto   stats = sched.collect_stats();
    double lat   = stats.total_yield_samples
                 ? (double)stats.total_yield_ns / stats.total_yield_samples / 2.0
                 : 0.0;

    std::printf("\n");
    sep('=', 60);
    std::printf("  results\n");
    sep('=', 60);
    std::printf("  wall time      : %.3f s\n", wall);
    std::printf("  ctx switches   : %llu (user-space)\n",
                (unsigned long long)stats.total_ctx_switches);
    std::printf("  latency (1-way): %.1f ns\n", lat);
    std::printf("  throughput     : %.2f M yields/sec\n",
                (double)total_yields / wall / 1e6);
    std::printf("  steals         : %llu\n",
                (unsigned long long)stats.total_steals);
    std::printf("  OS vol. csw    : %ld\n",  ru1.ru_nvcsw  - ru0.ru_nvcsw);
    std::printf("  OS invol. csw  : %ld\n",  ru1.ru_nivcsw - ru0.ru_nivcsw);
    sep('-', 60);
    std::printf("  %-8s  %-12s  %-10s  %s\n",
                "worker", "ctx-switches", "steals", "queue");
    for (int i = 0; i < cfg.num_workers; i++) {
        std::printf("  %-8d  %-12llu  %-10llu  %zu\n",
                    i,
                    (unsigned long long)stats.per_worker_ctx_switches[i],
                    (unsigned long long)stats.per_worker_steals[i],
                    stats.final_queue_sizes[i]);
    }
    sep('=', 60);
    std::printf("\n");

    return 0;
}
