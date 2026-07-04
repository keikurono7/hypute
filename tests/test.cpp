/*
 * Correctness self-check for Hypute top-K. Assert-based, no framework.
 * Verifies the property the product is sold on: on skewed data it recovers the
 * genuine heavy hitters, from a tiny fixed-size structure.
 */

#include "hypute.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

int main() {
    // 1. Trivial: a few items, clear winner.
    {
        hypute_topk* h = hypute_create(3, 1024, 4);
        for (int i = 0; i < 100; ++i) hypute_update(h, 7, 1.0);   // 7 is dominant
        for (int i = 0; i < 10;  ++i) hypute_update(h, 8, 1.0);
        hypute_update(h, 9, 1.0);
        uint64_t items[3]; double sc[3];
        size_t n = hypute_top(h, items, sc, 3);
        assert(n >= 1);
        assert(items[0] == 7);                        // heaviest is first
        assert(sc[0] >= 100.0);
        assert(hypute_records(h) == 111);
        hypute_free(h);
    }

    // 2. Heavy-hitter recovery on a skewed stream: the true top-20 must be found.
    {
        const size_t K = 20;
        hypute_topk* h = hypute_create(K, 8192, 4);
        std::unordered_map<uint64_t,uint64_t> truth;
        std::mt19937_64 g(1);
        std::uniform_real_distribution<double> u(1e-12, 1.0);
        for (int i = 0; i < 2'000'000; ++i) {
            uint64_t id = 1 + (uint64_t)std::pow(u(g), -1.0/1.1) % 100000;  // skewed
            hypute_update(h, id, 1.0);
            ++truth[id];
        }
        std::vector<std::pair<uint64_t,uint64_t>> all(truth.begin(), truth.end());
        std::partial_sort(all.begin(), all.begin() + K, all.end(),
                          [](auto&a, auto&b){ return a.second > b.second; });
        uint64_t items[K]; double sc[K];
        size_t n = hypute_top(h, items, sc, K);
        std::unordered_map<uint64_t,int> est; for (size_t i=0;i<n;++i) est[items[i]]=1;
        size_t hits = 0; for (size_t i=0;i<K;++i) if (est.count(all[i].first)) ++hits;
        assert(hits >= 18);                           // >= 90% recall on the true top-20

        // Memory is tiny and fixed regardless of the 100k distinct items seen.
        assert(hypute_memory_bytes(h) < 2 * 1024 * 1024);   // < 2 MB
        hypute_free(h);
    }

    std::printf("all tests passed\n");
    return 0;
}
