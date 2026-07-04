# Hypute

**Fast, exact streaming aggregation.**

[![benchmark](https://github.com/keikurono7/hypute/actions/workflows/ci.yml/badge.svg)](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)

Hypute keeps a running total for every `(source, target)` key over an unbounded stream of events — the thing you normally reach for a `std::unordered_map` to do (`map[key] += value`). It stores the **same data, exactly**, in one cache-friendly table instead of a tree of heap-allocated nodes, so it is **faster and uses less memory** while returning **byte-for-byte identical results**.

Nothing is approximated or dropped. Every benchmark below first *proves* Hypute agrees with `std::unordered_map` on every single key, then reports timing.

---

## What it is (in one minute)

You have a firehose of events — `(user, item, amount)`, `(src_ip, dst_ip, bytes)`, `(account, merchant, value)` — and you want the running total per key, readable at any time.

- The usual way, `std::unordered_map<key, value>`, allocates a separate node on the heap for every distinct key and chases a pointer to reach it. That burns memory on per-node overhead and stalls the CPU on cache misses — badly, once the data no longer arrives in a tidy order (production data never does).
- Hypute keeps the same key→total pairs in a single contiguous, densely-packed table. One hash, one cache line per access, no per-node allocation. Same exact answers, less memory, more speed.

**Use it when:** you need exact per-key totals over a high-rate stream and want to read them back — recommendations, per-entity metering/billing counters, event & interaction aggregation, feature accumulation, traffic/volume tracking.

**Don't use it when:** you can tolerate *approximate* answers for a far smaller footprint (then a sketch like Count-Min / HyperLogLog is the right tool), or you need range scans / ordered iteration (use a B-tree or a real database).

---

## Results — real datasets, measured in public CI (not on a laptop)

Every run happens on a clean GitHub Actions runner, aggregates the dataset with **both** `std::unordered_map` and Hypute, and **verifies they are identical on every key** before printing any number. Both structures store the full 64-bit `(source, target)` pair — no packing tricks, a like-for-like comparison.

| Dataset | Records | Distinct keys | Exact? | Throughput | Memory |
|---|--:|--:|:--:|---|---|
| [MovieLens 32M](https://github.com/keikurono7/hypute/actions/workflows/ci.yml) | 32,000,204 | 32,000,204 | ✅ 0 mismatch | 2.63 → **5.88 M/s** (2.2×) | 1837 → **999 MB** (1.84× less) |
| [Amazon Reviews '23](https://github.com/keikurono7/hypute/actions/workflows/amazon-100m.yml) | 100,000,000 | 100,000,000 | ✅ 0 mismatch | 2.10 → **5.56 M/s** (2.7×) | 5324 → **3158 MB** (1.69× less) |

*(Arrows read `std::unordered_map` → Hypute. Both datasets are worst cases for an aggregator: each key appears once, so there is no accumulation to amortize — Hypute still wins on speed and memory at every scale.)*

Reproduce them yourself with the steps below, or open the linked workflows under the repo's **Actions** tab.

> Because Hypute is *exact*, its memory still grows with the number of distinct keys — as any exact store must. The win is a smaller constant factor, not magic constant memory.

---

## Quick start

```c
#include "hypute.h"

hypute_engine* e = hypute_create(0);              // 0 = default initial capacity

hypute_update(e, user_id, item_id, 1.0);          // ingest an event      (O(1) amortized)
hypute_update(e, user_id, item_id, 2.5);          // same key accumulates
double total = hypute_query(e, user_id, item_id); // EXACT total (3.5); 0.0 if never seen

hypute_free(e);
```

Full API: [`include/hypute.h`](include/hypute.h) — `create` / `update` / `query` / `contains` / `size` / `records` / `memory_bytes`. Runnable example: [`examples/quickstart.cpp`](examples/quickstart.cpp).

---

## Build & reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness: exact through growth & collisions
./build/benchmark                              # synthetic skewed stream
./build/benchmark path/to/ratings.csv          # your data: rows of  source,target,value[,...]
```

**Requirements:** a C++17 compiler and CMake ≥ 3.12. No third-party dependencies.

To reproduce the real-data numbers: the **MovieLens 32M** run is the `benchmark` workflow (runs on every push); the **Amazon 100M** run is the `amazon-100m` workflow (manual — *Actions → amazon-100m → Run workflow*).

---

## How it works

A single open-addressed table of `{source, target, value}` slots with linear probing and a 1-bit-per-slot occupancy bitmap. The table is sized directly to the key count (Lemire fast-range reduction, no power-of-two rounding) and kept ~80% full, so it stays denser than a node-based map while every lookup touches essentially one cache line. See [`src/hypute.cpp`](src/hypute.cpp) — it's ~120 lines.

---

## License

Evaluation use only — see [LICENSE](LICENSE). Production and commercial use require a license from [Hayako](https://hayako.io).

© 2026 Hayako.
