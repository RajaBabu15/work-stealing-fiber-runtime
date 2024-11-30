#include "deque.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("WorkStealingDeque owner push and stealers drain", "[deque]") {
    constexpr int kItems = 200000;
    WorkStealingDeque<int> deque(4096);

    std::vector<int> storage(kItems);
    for (int i = 0; i < kItems; ++i)
        storage[i] = i;

    std::atomic<int> stolen{0};
    std::atomic<bool> done_pushing{false};

    std::thread owner([&] {
        for (int i = 0; i < kItems; ++i)
            deque.push(&storage[static_cast<size_t>(i)]);
        done_pushing.store(true, std::memory_order_release);
    });

    const int num_stealers = 3;
    std::vector<std::thread> stealers;
    stealers.reserve(num_stealers);
    for (int s = 0; s < num_stealers; ++s) {
        stealers.emplace_back([&] {
            for (;;) {
                int* p = deque.steal();
                if (p) {
                    stolen.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                if (done_pushing.load(std::memory_order_acquire)
                        && deque.empty()
                        && stolen.load(std::memory_order_relaxed) >= kItems)
                    break;
            }
        });
    }

    owner.join();
    for (auto& t : stealers) t.join();

    REQUIRE(stolen.load() == kItems);
    REQUIRE(deque.empty());
}

TEST_CASE("WorkStealingDeque grows beyond initial capacity", "[deque]") {
    WorkStealingDeque<int> deque(4);
    std::vector<int> values(100);
    for (int i = 0; i < 100; ++i)
        values[static_cast<size_t>(i)] = i;

    for (int i = 0; i < 100; ++i)
        deque.push(&values[static_cast<size_t>(i)]);

    REQUIRE(deque.size() == 100);

    int drained = 0;
    int expect  = 99;
    while (int* p = deque.pop()) {
        REQUIRE(*p == expect);
        --expect;
        ++drained;
    }
    REQUIRE(drained == 100);
}

TEST_CASE("WorkStealingDeque grows while stealers are active", "[deque]") {
    constexpr int kItems = 50000;
    WorkStealingDeque<int> deque(8);

    std::vector<int> storage(kItems);
    for (int i = 0; i < kItems; ++i)
        storage[i] = i;

    std::atomic<int> stolen{0};
    std::atomic<bool> done_pushing{false};

    std::thread owner([&] {
        for (int i = 0; i < kItems; ++i)
            deque.push(&storage[static_cast<size_t>(i)]);
        done_pushing.store(true, std::memory_order_release);
    });

    const int num_stealers = 4;
    std::vector<std::thread> stealers;
    stealers.reserve(num_stealers);
    for (int s = 0; s < num_stealers; ++s) {
        stealers.emplace_back([&] {
            for (;;) {
                int* p = deque.steal();
                if (p) {
                    stolen.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                if (done_pushing.load(std::memory_order_acquire)
                        && deque.empty()
                        && stolen.load(std::memory_order_relaxed) >= kItems)
                    break;
            }
        });
    }

    owner.join();
    for (auto& t : stealers) t.join();

    REQUIRE(stolen.load() == kItems);
    REQUIRE(deque.empty());
}