# Hypute

**Fast, exact streaming aggregation.** Hypute keeps an exact running total for every `(source, target)` key over an unbounded stream of interactions. It is a drop-in replacement for the usual `std::unordered_map` (or a nested map) used to accumulate per-key values — storing the *same data, exactly* in a single cache-friendly open-addressed table, so it is faster and holds its throughput when the stream arrives **unordered**, the way real production streams do.

[![benchmark](https://github.com/keikurono7/hypute/actions/workflows/ci.yml/badge.svg)](https://github.com/keikurono7/hypute/actions/workflows/ci.yml)

Nothing is approximated. `hypute_query` returns exactly the sum an equivalent map would hold — the benchmark **proves this on every key** before reporting any timing.

---

## Why

The everyday way to aggregate a stream — `map[key] += value` — is node-based: every distinct key is a separate heap allocation reached through a pointer. That wastes memory on per-node overhead and, worse, turns every update into a pointer chase across the heap. When the stream is **sorted** (as most benchmarks feed it) the cache hides this. When it is **unordered** (as production is) throughput falls off a cliff.

Hypute stores the same key→value data in one contiguous open-addressed table: one hash, one cache line per access, no per-node allocation. Same exact answers, but the cost of disorder largely goes away.

**Good fit:** high-rate per-key counters and roll-ups where you need exact totals and read them back — recommendations, per-entity metering, event/interaction aggregation, feature accumulation.

---

## Benchmarks — real data, run in CI (not cherry-picked on a laptop)

The [`benchmark` workflow](https://github.com/keikurono7/hypute/actions/workflows/ci.yml) runs on every push on a clean GitHub Actions runner. It:

1. downloads **MovieLens 32M** (~32,000,000 real ratings, `userId, movieId, rating`),
2. aggregates `(user, movie) → rating` with both `std::unordered_map` and Hypute,
3. **verifies the two agree on every key** (exact — 0 mismatches), then
4. reports throughput and memory, and repeats each engine on the same events **sorted vs shuffled** to expose ordering sensitivity.

See the latest run under the **Actions** tab for live numbers on the real dataset. The synthetic skewed stream is included in the same run for comparison.

> Honesty note: because Hypute is *exact*, its memory grows with the number of distinct keys, just like a map — the win is speed and stability under unordered input, plus a competitive footprint, **not** magic constant memory. If you can tolerate approximate answers for a smaller footprint, a sketch (Count-Min / HyperLogLog) is a different tool.

---

## Use

```c
#include "hypute.h"

hypute_engine* e = hypute_create(0);            // 0 = default initial capacity

hypute_update(e, user_id, item_id, 1.0);        // ingest an interaction (O(1) amortized)
double total = hypute_query(e, user_id, item_id); // EXACT running total, 0.0 if unseen

hypute_free(e);
```

Full API in [`include/hypute.h`](include/hypute.h); runnable example in [`examples/quickstart.cpp`](examples/quickstart.cpp).

---

## Build & reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness: exactness through growth & collisions
./build/benchmark                              # synthetic skewed stream
./build/benchmark path/to/ratings.csv          # your data: source,target,value[,...]
```

**Requirements:** a C++17 compiler and CMake ≥ 3.12. No third-party dependencies.

---

## License

Evaluation use only — see [LICENSE](LICENSE). Production and commercial use require a license from [Hayako](https://hayako.io).

© 2026 Hayako.
