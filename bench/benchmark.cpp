/*
 * Honest benchmark for Hypute top-K.
 *
 * The job: over a huge stream of item ids, find the top-K heaviest items
 * ("trending"). The exact way is to count every distinct item in a hash map
 * (gigabytes) and sort. Hypute keeps only a tiny cache-resident sketch + a
 * top-K heap (kilobytes) and never stores the long tail.
 *
 * We measure what actually matters for a trending product:
 *   - RECALL@K: of the true top-K items, how many did Hypute find?  (accuracy)
 *   - memory:   Hypute's fixed footprint vs the exact map's RSS.
 *   - speed:    events/second.
 *
 *   ./benchmark                      # synthetic skewed (Zipf-like) stream
 *   ./benchmark data.csv [col]       # real CSV; item id read from column `col` (default 1)
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

static const size_t K            = 1000;
static const size_t WIDTH        = 16384;   // 16384 * 4 counters * 8B = 512 KB (cache-resident)
static const size_t DEPTH        = 4;
static const uint64_t SYNTH_EVTS = 20'000'000;
static const uint64_t SYNTH_ITEMS = 5'000'000;

static size_t rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.rfind("VmRSS:", 0) == 0) {
            std::stringstream ss(line); std::string l; size_t kb = 0; ss >> l >> kb; return kb;
        }
    return 0;
}

// spread ranks across the id space so ids look real (not 1,2,3,...)
static inline uint64_t mix_id(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return x;
}
// Skewed synthetic stream: a few items are wildly popular, most are rare - just
// like real "trending" data. Zipf-ish via a power-law over item rank.
static std::vector<uint64_t> make_synthetic() {
    std::vector<uint64_t> v; v.reserve(SYNTH_EVTS);
    std::mt19937_64 g(20260704ULL);
    std::uniform_real_distribution<double> u(1e-12, 1.0);
    const double alpha = 1.05;
    for (uint64_t i = 0; i < SYNTH_EVTS; ++i) {
        uint64_t rank = 1 + (uint64_t)std::pow(u(g), -1.0 / alpha) % SYNTH_ITEMS;
        v.push_back(mix_id(rank));
    }
    return v;
}

static std::vector<uint64_t> load_csv(const char* path, size_t col) {
    std::vector<uint64_t> v; std::ifstream f(path);
    if (!f.is_open()) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    std::string line; std::getline(f, line);          // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t start = 0, c = 0; bool got = false;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ',') {
                if (c == col) {
                    try { v.push_back(std::stoull(line.substr(start, i - start))); got = true; } catch (...) {}
                    break;
                }
                ++c; start = i + 1;
            }
        }
        (void)got;
    }
    return v;
}

int main(int argc, char** argv) {
    size_t col = (argc > 2) ? (size_t)std::stoul(argv[2]) : 1;
    std::vector<uint64_t> stream = (argc > 1) ? load_csv(argv[1], col) : make_synthetic();
    if (stream.empty()) { std::fprintf(stderr, "no events\n"); return 1; }
    std::printf("Stream: %zu events%s   (top-%zu)\n\n", stream.size(),
                (argc > 1) ? " (real dataset)" : " (synthetic, skewed)", K);

    // ---- Hypute: tiny cache-resident top-K ----
    hypute_topk* h = hypute_create(K, WIDTH, DEPTH);
    auto t0 = clk::now();
    for (uint64_t id : stream) hypute_update(h, id, 1.0);
    auto t1 = clk::now();
    double hyp_mops = 1000.0 * stream.size() / std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double hyp_kb = hypute_memory_bytes(h) / 1024.0;

    std::vector<uint64_t> est_items(K); std::vector<double> est_scores(K);
    size_t est_n = hypute_top(h, est_items.data(), est_scores.data(), K);

    // ---- Exact: count everything, sort, take true top-K ----
    size_t r0 = rss_kb();
    std::unordered_map<uint64_t, uint64_t> counts;
    auto t2 = clk::now();
    for (uint64_t id : stream) ++counts[id];
    auto t3 = clk::now();
    size_t r1 = rss_kb();
    double map_mops = 1000.0 * stream.size() / std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();
    double map_mb = (r1 - r0) / 1024.0;

    std::vector<std::pair<uint64_t,uint64_t>> all(counts.begin(), counts.end());
    size_t topn = std::min(K, all.size());
    std::partial_sort(all.begin(), all.begin() + topn, all.end(),
                      [](auto& a, auto& b){ return a.second > b.second; });

    // ---- Recall@K: how many of the true top-K did Hypute find? ----
    std::unordered_map<uint64_t,int> truth_set;
    for (size_t i = 0; i < topn; ++i) truth_set[all[i].first] = 1;
    size_t hits = 0;
    for (size_t i = 0; i < est_n; ++i) if (truth_set.count(est_items[i])) ++hits;
    double recall = topn ? 100.0 * hits / topn : 0.0;

    // ---- Report ----
    std::printf("Distinct items: %zu\n\n", counts.size());
    std::printf("%-20s %16s %16s\n", "", "exact count+sort", "Hypute top-K");
    std::printf("%-20s %13.1f MB %13.1f KB\n", "memory", map_mb, hyp_kb);
    std::printf("%-20s %31.0fx smaller\n", "  ", (map_mb * 1024.0) / (hyp_kb > 0 ? hyp_kb : 1));
    std::printf("%-20s %13.2f M/s %13.2f M/s\n", "throughput", map_mops, hyp_mops);
    std::printf("\nRECALL@%zu : %.1f%%  (%zu of the true top-%zu items recovered)\n",
                K, recall, hits, topn);
    if (topn >= 5) {
        std::printf("\nTrue top 5 vs Hypute's estimate:\n");
        for (size_t i = 0; i < 5; ++i)
            std::printf("  #%zu  true id %20llu  count %llu   |  hypute est for it: %.0f\n",
                        i + 1, (unsigned long long)all[i].first, (unsigned long long)all[i].second,
                        hypute_estimate(h, all[i].first));
    }

    hypute_free(h);
    return 0;
}
