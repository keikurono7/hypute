/*
 * Honest benchmark: Hypute vs std::unordered_map<key,double>, the standard exact
 * store a competent engineer would reach for. Both hold the SAME data exactly,
 * so this is a like-for-like comparison - we first PROVE the results are
 * identical, then compare speed, memory, and behaviour under stream ordering.
 *
 *   ./benchmark                 # synthetic skewed stream (default)
 *   ./benchmark ratings.csv     # your data: rows of source,target,value[,...]
 */

#include "hypute.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using clk = std::chrono::high_resolution_clock;

static const uint64_t SYNTH_UPDATES = 20'000'000;
static const uint64_t SYNTH_KEYS    = 20'000'000;

struct Row { uint64_t s, t; double v; };

static inline uint64_t key_of(uint64_t s, uint64_t t) { return (s << 32) | (t & 0xffffffffULL); }

static size_t rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) {
            std::stringstream ss(line); std::string l; size_t kb = 0; ss >> l >> kb; return kb;
        }
    return 0;
}

// Realistic mixture: a hot set (Zipf) carrying ~half the traffic + a long
// uniform tail of millions of rare keys. Ordering and exactness are what we
// test; the skew makes the workload look like real event streams.
static std::vector<Row> make_synthetic() {
    std::vector<Row> rows; rows.reserve(SYNTH_UPDATES);
    std::mt19937_64 g(20260704ULL);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    const uint64_t HOT = 2000; const double hot_frac = 0.5, alpha = 1.0;
    for (uint64_t i = 0; i < SYNTH_UPDATES; ++i) {
        uint64_t rank = (u(g) < hot_frac)
            ? 1 + (uint64_t)std::pow(u(g), -1.0 / alpha) % HOT
            : HOT + 1 + (g() % (SYNTH_KEYS - HOT));
        rows.push_back({ 1 + (rank % 200000), 1 + (rank / 200000), 1.0 });
    }
    return rows;
}

static std::vector<Row> load_csv(const char* path) {
    std::vector<Row> rows; std::ifstream f(path);
    if (!f.is_open()) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    std::string line; std::getline(f, line);
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t p1 = line.find(','), p2 = line.find(',', p1 + 1), p3 = line.find(',', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        try {
            std::string vs = (p3 == std::string::npos) ? line.substr(p2 + 1) : line.substr(p2 + 1, p3 - (p2 + 1));
            rows.push_back({ std::stoull(line.substr(0, p1)),
                             std::stoull(line.substr(p1 + 1, p2 - (p1 + 1))), std::stod(vs) });
        } catch (...) {}
    }
    return rows;
}

// Time building each structure over `rows`; return M ops/sec.
static double run_map(const std::vector<Row>& rows, std::unordered_map<uint64_t,double>* keep) {
    std::unordered_map<uint64_t,double> local;
    std::unordered_map<uint64_t,double>& m = keep ? *keep : local;
    auto a = clk::now();
    for (const Row& r : rows) m[key_of(r.s, r.t)] += r.v;
    auto b = clk::now();
    return 1000.0 * rows.size() / std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}
static double run_hypute(const std::vector<Row>& rows, hypute_engine** keep) {
    hypute_engine* e = hypute_create(0);
    auto a = clk::now();
    for (const Row& r : rows) hypute_update(e, r.s, r.t, r.v);
    auto b = clk::now();
    if (keep) *keep = e; else hypute_free(e);
    return 1000.0 * rows.size() / std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

int main(int argc, char** argv) {
    std::vector<Row> rows = (argc > 1) ? load_csv(argv[1]) : make_synthetic();
    if (rows.empty()) { std::fprintf(stderr, "no rows\n"); return 1; }
    std::printf("Workload: %zu updates%s\n\n", rows.size(), (argc > 1) ? " (real dataset)" : " (synthetic, skewed)");

    // --- Build both in real (shuffled) arrival order; measure memory in isolation ---
    size_t r0 = rss_kb();
    hypute_engine* e = nullptr;
    double hyp_shuf = run_hypute(rows, &e);
    size_t r1 = rss_kb();
    std::unordered_map<uint64_t, double> truth;
    double map_shuf = run_map(rows, &truth);
    size_t r2 = rss_kb();

    double hyp_rss = (r1 - r0) / 1024.0, map_rss = (r2 - r1) / 1024.0;
    double hyp_foot = hypute_memory_bytes(e) / (1024.0 * 1024.0);

    // --- PROVE it is exact: every key must match the reference map ---
    size_t mismatches = 0;
    for (const auto& kv : truth) {
        uint64_t s = kv.first >> 32, t = kv.first & 0xffffffffULL;
        if (hypute_query(e, s, t) != kv.second) ++mismatches;
    }
    bool size_match = (hypute_size(e) == truth.size());
    size_t nkeys = truth.size();

    // Free both heavy structures before the ordering test so peak memory stays
    // low enough for a real large dataset on a CI runner.
    hypute_free(e); e = nullptr;
    std::unordered_map<uint64_t, double>().swap(truth);

    // --- Ordering sensitivity: same events sorted-by-key (sort in place, no copy) ---
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b){ return key_of(a.s, a.t) < key_of(b.s, b.t); });
    double map_sorted = run_map(rows, nullptr);
    double hyp_sorted = run_hypute(rows, nullptr);

    // ---- Report ----
    std::printf("EXACT?  %s   (%zu / %zu keys mismatch, key count %s)\n\n",
                (mismatches == 0 && size_match) ? "YES - identical to std::unordered_map"
                                                : "NO - BUG",
                mismatches, nkeys, size_match ? "matches" : "MISMATCH");
    std::printf("Distinct keys: %zu\n\n", nkeys);

    std::printf("%-18s %16s %14s\n", "", "unordered_map", "Hypute");
    std::printf("%-18s %13.2f M/s %11.2f M/s   (%.1fx)\n", "throughput",
                map_shuf, hyp_shuf, hyp_shuf / map_shuf);
    std::printf("%-18s %14.1f MB %12.1f MB   (map/hypute %.2f)\n", "memory (RSS)",
                map_rss, hyp_rss, map_rss / (hyp_rss > 0 ? hyp_rss : 1));
    std::printf("%-18s %31.1f MB   (exact table footprint)\n", "  Hypute table", hyp_foot);

    std::printf("\nThroughput vs stream ordering (M ops/sec) - real streams are the shuffled column:\n");
    std::printf("  %-16s sorted %8.2f   shuffled %8.2f   (shuffled = %.0f%% of sorted)\n",
                "unordered_map", map_sorted, map_shuf, 100.0 * map_shuf / map_sorted);
    std::printf("  %-16s sorted %8.2f   shuffled %8.2f   (shuffled = %.0f%% of sorted)\n",
                "Hypute", hyp_sorted, hyp_shuf, 100.0 * hyp_shuf / hyp_sorted);

    return mismatches == 0 && size_match ? 0 : 1;
}
