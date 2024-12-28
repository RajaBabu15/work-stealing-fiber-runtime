# work-stealing-fiber-runtime

Minimal M:N cooperative fiber scheduler in C++20. Hand-written ARM64/x86_64 assembly for context switching, Chase-Lev work-stealing deques per worker thread (steal path lock-free; owner push/pop single-threaded). No `ucontext`, no runtime third-party libraries.

Context switch layout follows [Boost.Context](https://github.com/boostorg/context) AAPCS64/SysV conventions as a reference — callee-saved **GPR and FP** state (d8–d15 / xmm6–xmm15) is saved across yields.

## Numbers (Apple M4, performance core)

Switch cost is core-dependent on Apple Silicon: ~12 ns on a performance core,
~23 ns on an efficiency core (the microbench is single-threaded and unpinned).

```bash
./fiber_runtime --bench-switch          # isolated one-way fiber_switch
  one-way switch latency : 12 ns
  switch throughput      : 88 M switches/sec

./fiber_runtime --latency-probe         # full round-trip through the scheduler
  scheduler round-trip   : 38 ns  (2 switches)
  throughput             : 9.5 M yields/sec
  OS vol. csw            : 0  (voluntary only)

./fiber_runtime --workers 4 --fibers 10000 --yield-every 1000 --rounds 10
  ctx switches           : 220000 (user-space)
  steals                 : ~7000
  OS vol. csw            : 0  (voluntary only)

./fiber_runtime --workers 8 --fibers 100000 --yield-every 100 --rounds 5
  completes cleanly (deque pre-sized to fiber count)
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

### Tests (Catch2 fetched at configure time — test-only)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(sysctl -n hw.ncpu)
ctest --output-on-failure   # or: make check

# Thread Sanitizer (deque stress only)
cmake .. -DFIBER_TSAN=ON
cmake --build . --target deque_stress_test
./deque_stress_test
```

## Usage

```bash
./fiber_runtime                                                  # default
./fiber_runtime --workers 4 --fibers 10000 --yield-every 1000   # custom
./fiber_runtime --bench-switch                                   # isolated 1-way switch latency
./fiber_runtime --latency-probe                                  # scheduler round-trip latency
./fiber_runtime --help
```

## How it works

Each OS thread owns a Chase-Lev deque (pre-sized to the fiber count, with a lock-free growth fallback). All fibers start on worker 0; idle workers steal from the top of busy workers' deques. Context switches save/restore callee-saved GPR and FP registers per the platform ABI — no syscalls on the hot path.

The deque memory ordering follows Lê et al., *"Correct and Efficient Work-Stealing for Weak Memory Models"*, PPoPP 2013.

The default benchmark reports **round-trip yield (incl. work)**. For switch cost, `--bench-switch` isolates a single `fiber_switch` — two contexts ping-ponging with no work and no scheduler, one clock pair around the whole loop — while `--latency-probe` measures the full round-trip through the scheduler.

## Verifying OS context switch counts

```bash
/usr/bin/time -l ./fiber_runtime --latency-probe 2>&1 | grep -i "context switch"
otool -tV build/fiber_runtime | grep -A 30 "_fiber_switch:"
```

Voluntary OS context switches are zero on the hot path for these workloads; involuntary preemption still occurs and is printed.

## References

- Lê, Pop, Cohen, Nardelli — PPoPP 2013 (deque memory ordering)
- Boost.Context — ARM64/x86_64 context frame layout reference
- AAPCS64 — ARM64 register conventions
- System V AMD64 ABI — x86_64 register conventions