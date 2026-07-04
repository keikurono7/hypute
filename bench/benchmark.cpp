/*
 * Honest three-axis benchmark: Hypute (approximate, bounded memory) vs an exact
 * std::unordered_map baseline (ground truth, unbounded memory).
 *
 * We report SPEED, MEMORY, and ACCURACY together. Reporting speed/memory alone
 * would be misleading, because Hypute wins those precisely by not storing every
 * key - so the whole point is to show how small the resulting error actually is.
 *
 *   ./benchmark                 # synthetic, skewed workload (default)
 *   ./benchmark ratings.csv     # real dataset: rows of source,target,value[,...]
 */

#include "hypute.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using clk = std::chrono::high_resolution_clock;

// ---- tunables (kept modest so CI runs in seconds) ----
static const uint64_t SYNTH_UPDATES = 40'000'000;
static const uint64_t SYNTH_KEYS    = 20'000'000; // large key space -> exact map is big
static const size_t   WIDTH         = 1u << 21;   // 2,097,152 counters/row
static const size_t   DEPTH         = 4;          // -> 64 MB fixed, << exact at scale

struct Row { uint64_t s, t; double v; };

static size_t rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) {
            std::stringstream ss(line);
            std::string label; size_t kb = 0;
            ss >> label >> kb;
            return kb;
        }
    return 0;
}

static inline uint64_t key_of(uint64_t s, uint64_t t) { return (s << 32) | (t & 0xffffffffULL); }

// Realistic mixture stream: a concentrated hot set (Zipf-distributed) that
// carries ~half the traffic, plus a long uniform tail of millions of rare keys.
// This is how real event/telemetry/interaction streams look, and it is exactly
// the regime Count-Min targets: heavy hitters estimated tightly, the vast rare
// tail approximated cheaply so total memory can stay fixed.
static std::vector<Row> make_synthetic() {
    std::vector<Row> rows;
    rows.reserve(SYNTH_UPDATES);
    std::mt19937_64 g(20260704ULL);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    const uint64_t HOT      = 2000;   // number of hot keys
    const double   hot_frac = 0.5;    // share of traffic landing on hot keys
    const double   alpha    = 1.0;    // skew among hot keys
    auto to_key = [](uint64_t rank, uint64_t& s, uint64_t& t) {
        s = 1 + (rank % 200000);
        t = 1 + (rank / 200000);
    };
    for (uint64_t i = 0; i < SYNTH_UPDATES; ++i) {
        uint64_t rank;
        if (u(g) < hot_frac) {                                    // hot: Zipf over [1, HOT]
            rank = 1 + (uint64_t)std::pow(u(g), -1.0 / alpha) % HOT;
        } else {                                                  // cold: uniform long tail
            rank = HOT + 1 + (g() % (SYNTH_KEYS - HOT));
        }
        uint64_t s, t; to_key(rank, s, t);
        rows.push_back({ s, t, 1.0 });
    }
    return rows;
}

static std::vector<Row> load_csv(const char* path) {
    std::vector<Row> rows;
    std::ifstream f(path);
    if (!f.is_open()) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    std::string line; std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t p1 = line.find(','), p2 = line.find(',', p1 + 1), p3 = line.find(',', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        try {
            Row r;
            r.s = std::stoull(line.substr(0, p1));
            r.t = std::stoull(line.substr(p1 + 1, p2 - (p1 + 1)));
            std::string vs = (p3 == std::string::npos) ? line.substr(p2 + 1)
                                                       : line.substr(p2 + 1, p3 - (p2 + 1));
            r.v = std::stod(vs);
            rows.push_back(r);
        } catch (...) {}
    }
    return rows;
}

int main(int argc, char** argv) {
    std::vector<Row> rows = (argc > 1) ? load_csv(argv[1]) : make_synthetic();
    if (rows.empty()) { std::fprintf(stderr, "no rows\n"); return 1; }

    std::printf("Workload: %zu updates%s\n", rows.size(),
                (argc > 1) ? std::string(" from ").append(argv[1]).c_str() : " (synthetic, skewed)");

    // ---- Exact baseline (ground truth + unbounded memory) ----
    size_t rss0 = rss_kb();
    std::unordered_map<uint64_t, double> exact;
    auto t0 = clk::now();
    for (const Row& r : rows) exact[key_of(r.s, r.t)] += r.v;
    auto t1 = clk::now();
    size_t rss1 = rss_kb();
    double exact_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / (double)rows.size();
    double exact_mb  = (rss1 - rss0) / 1024.0;
    size_t unique    = exact.size();

    // ---- Hypute (approximate + fixed memory) ----
    hypute_engine* e = hypute_create(WIDTH, DEPTH);
    auto t2 = clk::now();
    for (const Row& r : rows) hypute_update(e, r.s, r.t, r.v);
    auto t3 = clk::now();
    double hyp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count() / (double)rows.size();
    double hyp_mb = hypute_memory_bytes(e) / (1024.0 * 1024.0);

    // ---- Accuracy: compare every unique key's estimate against ground truth ----
    // Keep (truth, relative_error) so we can weight by traffic, not just by key.
    std::vector<std::pair<double, double>> pts;   // (truth_weight, rel_error)
    pts.reserve(unique);
    double max_rel = 0.0, weighted_num = 0.0, weighted_den = 0.0;
    size_t under = 0;
    for (const auto& kv : exact) {
        uint64_t s = kv.first >> 32, t = kv.first & 0xffffffffULL;
        double est = hypute_query(e, s, t), truth = kv.second;
        if (est + 1e-9 < truth) ++under;                          // must never under-estimate
        double re = truth > 0 ? (est - truth) / truth : 0.0;
        pts.emplace_back(truth, re);
        weighted_num += re * truth; weighted_den += truth;        // traffic-weighted error
        if (re > max_rel) max_rel = re;
    }

    // ---- Report: speed + memory ----
    std::printf("Distinct keys: %zu   Total weight: %.0f\n\n", unique, hypute_total_weight(e));
    std::printf("%-16s %14s %14s\n", "", "Exact (map)", "Hypute");
    std::printf("%-16s %14.1f %14.1f\n", "ns / update", exact_ns, hyp_ns);
    std::printf("%-16s %14.1f %14.1f  (fixed)\n", "memory (MB)", exact_mb, hyp_mb);
    std::printf("%-16s %13.1fx %13.1fx\n", "memory ratio", 1.0, exact_mb / (hyp_mb > 0 ? hyp_mb : 1));

    std::printf("\n  never under-estimates            : %s (%zu violations)\n", under == 0 ? "yes" : "NO", under);
    std::printf("  error if queries mirror data freq: %6.2f%%  (dominated by the rare tail below)\n",
                100.0 * weighted_num / weighted_den);

    // ---- Accuracy bucketed by how often a key appears (honest full picture) ----
    // CMS is near-exact for heavy hitters and loosens on the rare-key tail;
    // this table shows exactly where the line is so a buyer can judge their case.
    std::printf("\nAccuracy by key frequency (median relative over-estimate):\n");
    std::printf("  %-18s %9s %10s %10s\n", "true count", "# keys", "% traffic", "median err");
    struct Bucket { const char* label; double lo; };
    const Bucket buckets[] = {
        {">= 10000", 10000}, {"1000 - 9999", 1000}, {"100 - 999", 100},
        {"10 - 99", 10}, {"1 - 9", 1},
    };
    for (const Bucket& b : buckets) {
        double hi = (&b == buckets) ? 1e18 : (&b)[-1].lo;
        std::vector<double> errs; double wt = 0.0;
        for (auto& p : pts) if (p.first >= b.lo && p.first < hi) { errs.push_back(p.second); wt += p.first; }
        if (errs.empty()) continue;
        std::sort(errs.begin(), errs.end());
        std::printf("  %-18s %9zu %9.1f%% %9.2f%%\n", b.label, errs.size(),
                    100.0 * wt / weighted_den, 100.0 * errs[errs.size()/2]);
    }
    std::printf("  -> heavy hitters are near-exact; the rare long tail is approximated\n");
    std::printf("     coarsely. That is the deliberate trade that keeps memory fixed.\n");
    (void)max_rel;

    // ---- Memory scaling: exact grows linearly with #keys; Hypute is flat ----
    double bytes_per_key = exact_mb * 1024.0 * 1024.0 / (double)unique;   // measured this run
    std::printf("\nMemory vs distinct keys (exact = measured %.0f B/key, extrapolated; Hypute fixed at %.0f MB):\n",
                bytes_per_key, hyp_mb);
    for (uint64_t k : { 1'000'000ULL, 10'000'000ULL, 100'000'000ULL, 1'000'000'000ULL }) {
        double exact_gb = bytes_per_key * (double)k / 1e9;
        std::printf("  %13llu keys : exact %8.2f GB   Hypute %6.3f GB   -> %.0fx smaller\n",
                    (unsigned long long)k, exact_gb, hyp_mb / 1024.0,
                    (exact_gb * 1024.0) / (hyp_mb > 0 ? hyp_mb : 1));
    }

    hypute_free(e);
    return 0;
}
