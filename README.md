# Hypute

**Low-latency, cache-resident stream processing in C++.**

[![benchmark](https://github.com/keikurono7/hypute/actions/workflows/ci.yml/badge.svg)](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)

Processing high-rate event streams gets slow and expensive for two reasons: touching main memory (each RAM fetch costs ~60-100 ns vs ~1-10 ns in cache), and doing work for data you don't need. Hypute is a set of techniques and a working engine that attack both, and it backs every claim with a benchmark you can reproduce on a clean CI runner, not a tuned laptop.

Two demonstrations, both verified in CI:

1. **Latency, hardware-gated sparse processing** — skip the idle channels at hardware speed: **~8x lower latency** than a dense scan, on identical work.
2. **Memory, real-time top-K** — track the heavy hitters of a stream in cache: **45x less memory** and **~2x faster** than an exact approach, at 99.9% accuracy, over 100M real records.

> This is the kind of work I do. If your real-time or data-heavy systems are slower or costlier than they should be, see [Work with me](#work-with-me).

---

## Proof 1 - latency: hardware-gated sparse traversal

When events are sparse (most channels idle each tick), the naive loop still tests every channel. Hypute bit-scans only the active ones with a single CPU instruction (`TZCNT` via `__builtin_ctzll`, clear with `BLSR`), and keeps the working state in L1 cache. Same work, idle channels never touched.

`bench/gating_benchmark.cpp` (200k ticks, 4096 channels, 1.5% active, on an Intel i5-10300H):

| | ns / tick |
|---|--:|
| dense scan (test every channel) | 3,353 |
| **hardware-gated (bit-scan set bits)** | **397 (8.4x faster)** |

Both engines are checksum-verified to produce identical output, and a `volatile` sink prevents the compiler from optimizing the work away (so the number is real, not a dead-code illusion). Absolute ns are hardware-dependent; the benchmark prints the CPU and reproduces in CI.

## Proof 2 - memory: real-time top-K over massive streams

Finding "what's trending" by counting every distinct item in a hash map costs gigabytes of RAM, most of it for items nobody queries. Hypute keeps only the heavy hitters in a fixed, cache-resident structure and drops the long tail.

**Amazon Reviews '23, top 1,000 products over 100,000,000 reviews (8.78M distinct):**

| | exact (count + sort) | Hypute |
|---|--:|--:|
| Recall @ 1000 | ground truth | **99.9%** (999/1000) |
| Memory | 360 MB | **8 MB (45x less)** |
| Throughput | 9.6 M/s | **19.7 M/s (~2x faster)** |

This win scales with distinct-item count; below ~1M distinct items an exact map is already small and Hypute isn't needed. Reproduce it: the `benchmark` workflow runs MovieLens 32M on every push; `amazon-100m` runs the 100M test on demand.

---

## How it works

- **Gating** (`bench/gating_benchmark.cpp`): sparse events packed as bits in 64-bit words; `__builtin_ctzll` + `reg &= reg-1` iterate only set bits; all state cache-resident and allocation-free.
- **Top-K engine** (`src/hypute.cpp`): a cache-sized Count-Min sketch with conservative update estimates each item's weight in fixed memory, paired with a min-heap of the current top-K. Table sized directly to the key count (Lemire fast-range, no power-of-two waste) and kept densely packed, so it stays hot in L2/L3.

## Build and reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness
./build/gating_benchmark                       # latency: gated vs dense
./build/benchmark                              # top-K on a synthetic skewed stream
./build/benchmark data.csv 1                   # top-K on your CSV (item id in column 1)
```

Requirements: a C++17 compiler and CMake 3.12+. No third-party dependencies.

API for the top-K engine: [`include/hypute.h`](include/hypute.h); example in [`examples/quickstart.cpp`](examples/quickstart.cpp).

---

## Work with me

I'm Madhusudan, a low-latency, high-performance C++ engineer. I make real-time and data-heavy systems faster and lighter, and I back it with numbers you can reproduce (this repo is one example).

**I can help with:**
- Low-latency pipelines and streaming / event processing
- Cache-aware and SIMD optimization, cutting memory footprint
- Profiling hot paths and finding where the latency actually goes

**Available for freelance and contract work.** Happy to start with a quick, free look at your hot path and an honest read on what is worth speeding up.

- Email: [dmpathani@gmail.com](mailto:dmpathani@gmail.com)
- LinkedIn: [linkedin.com/in/madhusudan--](https://www.linkedin.com/in/madhusudan--/)
- GitHub: [github.com/keikurono7](https://github.com/keikurono7)

---

## License

MIT, see [LICENSE](LICENSE). Free to use, modify, and build on.
