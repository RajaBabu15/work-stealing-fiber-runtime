#include "fiber.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

TEST_CASE("FP callee-saved registers survive yields", "[abi]") {
    SchedulerConfig cfg;
    cfg.num_workers = 1;
    cfg.num_fibers  = 2;
    cfg.yield_every = 1;
    cfg.rounds      = 5000;

    Scheduler sched(cfg);

    for (int i = 0; i < cfg.num_fibers; i++) {
        Fiber* f = alloc_fiber();
        f->id    = (uint64_t)i;
        const int ro = cfg.rounds;
        const int id = i;
        init_fiber_stack(f, [ro, id]() {
            double dacc = 1.0 + 0.001 * id;
            float  facc = 2.0f + 0.001f * id;
            for (int r = 0; r < ro; ++r) {
                dacc = std::fma(dacc, 1.0000001, 0.0000001);
                facc = facc * 1.0000001f + 0.0000001f;
                fiber_yield();
            }
            double check = 1.0 + 0.001 * id;
            float  fchk  = 2.0f + 0.001f * id;
            for (int r = 0; r < ro; ++r) {
                check = std::fma(check, 1.0000001, 0.0000001);
                fchk  = fchk * 1.0000001f + 0.0000001f;
            }
            REQUIRE(dacc == check);
            REQUIRE(facc == fchk);
        });
        sched.enqueue_initial(f);
    }

    sched.run();
}