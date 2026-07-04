/*
 * Correctness self-check for the Hypute core. No framework: assert-based.
 * Verifies the guarantees the product is sold on:
 *   1. exact recovery when there are no collisions,
 *   2. never under-estimates non-negative streams,
 *   3. memory is truly fixed regardless of key count,
 *   4. accuracy improves as width grows (the tunable-error promise).
 */

#include "hypute.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

static uint64_t k(uint64_t s, uint64_t t) { return (s << 32) | t; }

int main() {
    // 1. No collisions (huge width, few keys) -> estimates are exact.
    {
        hypute_engine* e = hypute_create(1u << 16, 4);
        hypute_update(e, 10, 20, 3.0);
        hypute_update(e, 10, 20, 1.5);   // same key accumulates
        hypute_update(e, 99, 1, 7.0);
        assert(std::fabs(hypute_query(e, 10, 20) - 4.5) < 1e-9);
        assert(std::fabs(hypute_query(e, 99, 1)  - 7.0) < 1e-9);
        assert(hypute_query(e, 5, 5) == 0.0);            // unseen key -> 0
        hypute_free(e);
    }

    // 2. Never under-estimates a non-negative stream (the core CMS guarantee).
    {
        hypute_engine* e = hypute_create(4096, 5);       // deliberately small -> collisions
        std::unordered_map<uint64_t, double> truth;
        std::mt19937_64 g(1);
        for (int i = 0; i < 200000; ++i) {
            uint64_t s = g() % 50000, t = g() % 50000;
            double v = 1.0 + (g() % 10);
            hypute_update(e, s, t, v);
            truth[k(s, t)] += v;
        }
        for (auto& kv : truth) {
            double est = hypute_query(e, kv.first >> 32, kv.first & 0xffffffffULL);
            assert(est + 1e-6 >= kv.second);             // >= true, always
        }
        hypute_free(e);
    }

    // 3. Memory is fixed: same footprint after 0 and after 1M distinct keys.
    {
        hypute_engine* e = hypute_create(1u << 18, 4);
        size_t before = hypute_memory_bytes(e);
        for (uint64_t i = 0; i < 1'000'000; ++i) hypute_update(e, i, i * 7 + 1, 1.0);
        assert(hypute_memory_bytes(e) == before);
        assert(hypute_records(e) == 1'000'000);
        hypute_free(e);
    }

    // 4. Wider sketch -> lower error on the same stream (tunable accuracy).
    {
        auto median_err = [](size_t width) {
            hypute_engine* e = hypute_create(width, 5);
            std::unordered_map<uint64_t, double> truth;
            std::mt19937_64 g(7);
            for (int i = 0; i < 300000; ++i) {
                uint64_t s = g() % 40000, t = g() % 40000;
                hypute_update(e, s, t, 1.0);
                truth[k(s, t)] += 1.0;
            }
            std::vector<double> errs;
            for (auto& kv : truth) {
                double est = hypute_query(e, kv.first >> 32, kv.first & 0xffffffffULL);
                errs.push_back((est - kv.second) / kv.second);
            }
            std::sort(errs.begin(), errs.end());
            hypute_free(e);
            return errs[errs.size() / 2];
        };
        assert(median_err(1u << 18) <= median_err(1u << 12));  // wider is no worse (usually better)
    }

    // 5. hypute_create_eps sizes sensibly and rejects nonsense.
    {
        assert(hypute_create_eps(0.0, 0.5) == nullptr);
        assert(hypute_create_eps(0.5, 1.0) == nullptr);
        hypute_engine* e = hypute_create_eps(0.01, 0.01);
        assert(e != nullptr);
        assert(hypute_epsilon(e) <= 0.01 + 1e-12);             // width >= e/epsilon
        hypute_free(e);
    }

    std::printf("all tests passed\n");
    return 0;
}
