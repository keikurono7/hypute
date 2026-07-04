# Hypute

**Bounded-memory streaming aggregation.** Hypute keeps running per-key totals over an unbounded stream of `(source, target, value)` interactions using a *fixed* amount of memory — no matter how many distinct keys or records it sees. It trades exactness for a small, tunable, provably-bounded over-estimate, and in return it never grows.

It is a production wrapper around a [Count-Min Sketch](https://en.wikipedia.org/wiki/Count%E2%80%93min_sketch): the memory footprint you pick at startup is the memory footprint forever.

---

## Why

An exact store (`unordered_map`, Redis hash, a table) must hold every distinct key. At scale that memory grows without bound and moves onto expensive cloud RAM. If your workload is **skewed** — a hot set of keys carries most of the traffic, with a long tail of rare keys — and you mainly care about the heavy hitters, Hypute gives you their totals accurately at a memory cost that stays flat as the key space explodes.

**Good fit:** per-entity counters, rate/volume tracking, top-K feeds, telemetry roll-ups, feature aggregation, abuse/fraud signals — anywhere the heavy hitters matter and the rare tail can be approximate.

**Not a fit:** when you need the exact value for *every* key, including rare ones.

---

## Benchmark

`./benchmark` — 40M-update skewed stream (a 2,000-key hot set carrying 50% of traffic, plus a ~12.6M-key uniform long tail), compared against an exact `std::unordered_map`. Numbers below are from a Release build on x86-64; reproduce with the steps further down.

### Speed & memory

|                | Exact (`unordered_map`) | Hypute        |
|----------------|------------------------:|--------------:|
| ns / update    | 162.5                   | **50.8**      |
| memory         | 573.8 MB                | **64.0 MB** (fixed) |
| memory ratio   | 1.0×                    | **9× smaller** |

Memory is fixed, so the gap widens as the key space grows (exact extrapolated from the measured ~48 bytes/key):

| Distinct keys | Exact    | Hypute   | Smaller by |
|--------------:|---------:|---------:|-----------:|
| 1,000,000     | 0.05 GB  | 0.063 GB | ~1×        |
| 10,000,000    | 0.48 GB  | 0.063 GB | 8×         |
| 100,000,000   | 4.76 GB  | 0.063 GB | **76×**    |
| 1,000,000,000 | 47.6 GB  | 0.063 GB | **761×**   |

### Accuracy (the honest part)

Hypute **never under-estimates** a non-negative stream. The over-estimate is near-zero for heavy hitters and grows on the rare tail — which is exactly the trade that keeps memory fixed:

| True count of a key | # keys      | % of traffic | Median error |
|---------------------|------------:|-------------:|-------------:|
| ≥ 10,000            | 43          | 48.9%        | **0.01%**    |
| 1,000 – 9,999       | 97          | 0.8%         | 0.19%        |
| 100 – 999           | 321         | 0.3%         | 2.00%        |
| 10 – 99             | 1,292       | 0.1%         | 21.05%       |
| 1 – 9               | 12,641,461  | 50.0%        | 400%         |

**Read this honestly:** the keys that carry ~50% of traffic and matter for decisions are estimated within a fraction of a percent, in 9× (→76× at scale) less memory and 3× faster. The millions of singleton keys in the long tail are deliberately approximated. If your queries target heavy hitters, that tail costs you nothing; if you must read exact values for rare keys, use an exact store.

Accuracy is tunable: wider sketch → lower error (see `hypute_create_eps`).

---

## Use

```c
#include "hypute.h"

// Size for ~0.1% error at 99% confidence. Footprint is fixed from here on.
hypute_engine* e = hypute_create_eps(0.001, 0.01);

hypute_update(e, user_id, item_id, 1.0);   // ingest an interaction  (O(depth))
double total = hypute_query(e, user_id, item_id);   // estimated running total

hypute_free(e);
```

Full API in [`include/hypute.h`](include/hypute.h). Runnable example: [`examples/quickstart.cpp`](examples/quickstart.cpp).

---

## Build & reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

ctest --test-dir build --output-on-failure   # correctness (error bound, fixed memory)
./build/benchmark                              # synthetic skewed workload
./build/benchmark ratings.csv                  # your own CSV: source,target,value[,...]
./build/quickstart
```

**Requirements:** a C++17 compiler and CMake ≥ 3.12. No third-party dependencies.

---

## License

Evaluation use only — see [LICENSE](LICENSE). Production and commercial use require a license from [Hayako](https://hayako.io).

© 2026 Hayako.
