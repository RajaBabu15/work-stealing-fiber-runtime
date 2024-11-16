#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "fiber.hpp"
#include "deque.hpp"

struct WorkerContext {
    int   id{0};
    void* scheduler_sp{nullptr};

    WorkStealingDeque<Fiber> deque;

    std::atomic<uint64_t> ctx_switch_count{0};
    std::atomic<uint64_t> steal_count{0};
    std::atomic<uint64_t> yield_ns_sum{0};
    std::atomic<uint64_t> yield_sample_count{0};
    std::atomic<uint64_t> switch_ns_sum{0};
    std::atomic<uint64_t> switch_sample_count{0};

    explicit WorkerContext(size_t deque_capacity) : deque(deque_capacity) {}

    WorkerContext(const WorkerContext&) = delete;
    WorkerContext& operator=(const WorkerContext&) = delete;
};

struct SchedulerConfig {
    int num_workers{4};
    int num_fibers{10000};
    int yield_every{1000};
    int rounds{10};
};

class Scheduler {
public:
    explicit Scheduler(SchedulerConfig cfg);
    ~Scheduler() = default;

    void run();
    void enqueue_initial(Fiber* f);
    size_t queue_size(int worker_id) const;

    struct Stats {
        uint64_t total_ctx_switches{0};
        uint64_t total_steals{0};
        uint64_t total_yield_ns{0};
        uint64_t total_yield_samples{0};
        uint64_t total_switch_ns{0};
        uint64_t total_switch_samples{0};
        std::vector<uint64_t> per_worker_ctx_switches;
        std::vector<uint64_t> per_worker_steals;
        std::vector<size_t>   final_queue_sizes;
    };
    Stats collect_stats() const;

private:
    SchedulerConfig cfg_;
    std::vector<std::unique_ptr<WorkerContext>> workers_;
    std::vector<std::thread> threads_;
    std::atomic<uint64_t>    fibers_done_{0};
    std::atomic<bool>        shutdown_{false};
    uint64_t                 start_ns_{0};

    void   worker_entry(int worker_id);
    Fiber* steal_from_others(int self_id);
    void   run_reporter();
};