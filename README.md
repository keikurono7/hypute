# Hypute

**Real-time top-K over massive streams — in kilobytes.**

[![benchmark](https://github.com/keikurono7/hypute/actions/workflows/ci.yml/badge.svg)](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)

Point a firehose of events at Hypute — product views, clicks, requests, IPs, song plays — and at any instant ask **"what are the top-K right now?"** It answers from a tiny, fixed structure that lives entirely inside the CPU cache. It never stores the events, never grows, and never slows down as the stream gets bigger.

---

## The problem it solves

Finding "what's trending" the obvious way means counting **every distinct item** in a giant hash map, then sorting. Across billions of events and tens of millions of distinct items, that map balloons into **gigabytes of RAM** — expensive cloud memory that mostly holds items nobody will ever look at (the one-view products, the single-hit URLs).

But you only care about the **top**. Hypute keeps just enough to track the heavy hitters — a fixed few hundred kilobytes that stays resident in the CPU's cache — and throws the long tail away. The result: the same "top trending" answer, from **thousands of times less memory**, at full speed, no matter how large the stream grows.

**Great for:** trending feeds, real-time leaderboards, most-viewed / most-active dashboards, hot-key and abuse/DDoS detection, ad-tech pacing.

**Not for:** an exact per-item ledger (Hypute is approximate by design — it nails the heavy hitters and deliberately forgets the rare tail; that trade is what keeps it in cache).

---

## Results — real datasets, measured in public CI

Every run happens on a clean GitHub Actions runner, finds the top-1,000 items with Hypute, and compares against the exact "count everything and sort" baseline on three things: **recall** (did we find the true top items?), **memory**, and **speed**.

- **MovieLens 32M** — top movies over 32,000,204 ratings: [`benchmark` workflow](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)
- **Amazon Reviews '23 100M** — top products over 100,000,000 reviews: [`amazon-100m` workflow](https://github.com/keikurono7/hypute/actions/workflows/amazon-100m.yml)

*(Live numbers under the repo's **Actions** tab. Headline results are filled in below from the latest run.)*

<!-- RESULTS -->

---

## Quick start

```c
#include "hypute.h"

hypute_topk* h = hypute_create(/*k*/ 1000, /*width*/ 0, /*depth*/ 0);  // 0,0 = sensible defaults

hypute_update(h, item_id, 1.0);        // ingest one event (O(1))

uint64_t items[1000]; double scores[1000];
size_t n = hypute_top(h, items, scores, 1000);   // current top-K, highest first

hypute_free(h);
```

Full API: [`include/hypute.h`](include/hypute.h). Runnable example: [`examples/quickstart.cpp`](examples/quickstart.cpp).

---

## Build & reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness: heavy-hitter recall on skewed data
./build/benchmark                              # synthetic skewed stream
./build/benchmark data.csv 1                   # your CSV; item id read from column 1
```

**Requirements:** a C++17 compiler and CMake ≥ 3.12. No third-party dependencies.

---

## How it works

A small **Count-Min sketch** (a few hundred KB — sized to stay in cache) estimates each item's running weight in fixed memory, paired with a **min-heap of the current top-K** and a tiny id→slot map. Each event bumps the sketch and, if the item now outweighs the lightest tracked one, swaps it into the top-K. Nothing scales with the number of distinct items, so the whole structure stays hot in L2/L3 cache — which is where the speed and the immunity to RAM latency come from. See [`src/hypute.cpp`](src/hypute.cpp).

---

## License

Evaluation use only — see [LICENSE](LICENSE). Production and commercial use require a license from [Hayako](https://hayako.io).

© 2026 Hayako.
