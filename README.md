# Hypute

**A fast, memory-efficient engine for real-time "top-K" over massive streams.**

[![benchmark](https://github.com/keikurono7/hypute/actions/workflows/ci.yml/badge.svg)](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)

Hypute answers one question continuously over a firehose of events: *what are the top items right now?* (top products, top IPs, hottest keys). It does it from a tiny structure that lives in the CPU cache, so it stays fast and light no matter how large the stream gets.

I built it to show what careful, low-latency, cache-aware C++ can do. On **100 million real records** it recovers the true top 1,000 with **99.9% accuracy** using about **45x less memory** and running **~2x faster** than the standard approach, and every number below is reproducible on a clean CI runner, not a tuned laptop.

> If your real-time or data-heavy systems are slower or more expensive than they should be, this is the kind of work I do. See [Work with me](#work-with-me).

---

## What it does, in plain terms

Finding "what's trending" the normal way means counting every distinct item in a hash map and sorting. Across billions of events and tens of millions of distinct items, that map grows into gigabytes of RAM, most of it holding items nobody will ever look at. Hypute keeps only enough to track the heavy hitters (a fixed few megabytes that stays resident in cache) and drops the long tail. Same "top" answer, a fraction of the memory, at full speed.

**Good for:** trending feeds, real-time leaderboards, most-active dashboards, hot-key and abuse detection, ad-tech pacing.

**Not for:** an exact per-item ledger. Hypute is approximate by design. It nails the heavy hitters and deliberately forgets rare items, which is exactly what keeps it in cache.

---

## Benchmarks (real data, public CI)

Each run happens on a clean GitHub Actions runner, finds the top 1,000 with Hypute, and compares against the exact "count everything and sort" baseline on recall, memory, and speed.

### Amazon Reviews '23, top 1,000 products over 100,000,000 reviews (8.78M distinct)

|                | exact (count + sort) | Hypute            |
|----------------|---------------------:|------------------:|
| Recall @ 1000  | ground truth         | **99.9%** (999/1000) |
| Memory         | 360 MB               | **8 MB (45x less)** |
| Throughput     | 9.6 M/s              | **19.7 M/s (~2x faster)** |

The win scales with how many distinct items you have. On a low-cardinality stream (for example MovieLens, ~84K distinct movies) an exact map is already tiny, so Hypute is not needed. It earns its keep above roughly a million distinct items, exactly where the exact map balloons into gigabytes.

Reproduce it: the `benchmark` workflow runs MovieLens 32M on every push, and the `amazon-100m` workflow runs the 100M test on demand (Actions tab).

---

## How it works

A small Count-Min sketch (a few MB, sized to stay in cache) estimates each item's running weight in fixed memory, using conservative update to keep the estimates tight. It is paired with a min-heap of the current top-K and a tiny id-to-slot map, so each event is O(1) plus O(log k). The table is sized directly to the key count (Lemire fast-range reduction, no power-of-two waste) and kept densely packed. Nothing scales with the number of distinct items, so the whole structure stays hot in L2/L3, which is where the speed and the low memory come from. The core is in [`src/hypute.cpp`](src/hypute.cpp).

---

## Use it

```c
#include "hypute.h"

hypute_topk* h = hypute_create(/*k*/ 1000, /*width*/ 0, /*depth*/ 0);  // 0,0 = sensible defaults

hypute_update(h, item_id, 1.0);        // ingest one event (O(1))

uint64_t items[1000]; double scores[1000];
size_t n = hypute_top(h, items, scores, 1000);   // current top-K, highest first

hypute_free(h);
```

Full API in [`include/hypute.h`](include/hypute.h). Runnable example in [`examples/quickstart.cpp`](examples/quickstart.cpp).

## Build and reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness: heavy-hitter recall on skewed data
./build/benchmark                              # synthetic skewed stream
./build/benchmark data.csv 1                   # your CSV; item id read from column 1
```

Requirements: a C++17 compiler and CMake 3.12+. No third-party dependencies.

---

## Work with me

I'm a low-latency, high-performance C++ engineer. I make real-time and data-heavy systems faster and lighter, and I back it with numbers you can reproduce (this repo is one example: several times the throughput and a large memory cut, verified in public CI).

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
