# work-stealing-fiber-runtime

Minimal M:N cooperative fiber scheduler in C++20. Hand-written ARM64/x86_64 assembly for context switching, lock-free Chase-Lev work-stealing deques per worker thread. No `ucontext`, no third-party libraries.

## Numbers (Apple M4)

```
./fiber_runtime --latency-probe
  latency (1-way): 36.5 ns
  throughput     : 4.74 M yields/sec
  OS vol. csw    : 0

./fiber_runtime --workers 4 --fibers 10000 --yield-every 1000 --rounds 10
  ctx switches   : 220000 (user-space)
  steals         : 7265
  OS vol. csw    : 0
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

## Usage

```bash
./fiber_runtime                                                  # default
./fiber_runtime --workers 4 --fibers 10000 --yield-every 1000   # custom
./fiber_runtime --latency-probe                                  # raw latency
./fiber_runtime --help
```

## How it works

Each OS thread owns a Chase-Lev deque. All fibers start on worker 0; idle workers steal from the back of busy workers' deques. Context switches save/restore only the callee-saved registers defined by the platform ABI (x19–x30 on ARM64, rbx/rbp/r12–r15 on x86_64) — no SIMD state, no syscall.

The deque memory ordering follows Lê et al., *"Correct and Efficient Work-Stealing for Weak Memory Models"*, PPoPP 2013, which is necessary for correctness on ARM64's weak memory model.

## Verifying zero OS context switches

```bash
/usr/bin/time -l ./fiber_runtime --latency-probe 2>&1 | grep -i "context switch"
otool -tV build/fiber_runtime | grep -A 20 "_fiber_switch:"
```

## References

- Lê, Pop, Cohen, Nardelli — PPoPP 2013 (deque memory ordering)
- AAPCS64 — ARM64 register conventions
- System V AMD64 ABI — x86_64 register conventions
