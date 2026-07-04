/*
 * Correctness self-check. No framework: assert-based. Proves the one thing this
 * engine is sold on - it is EXACT and matches std::unordered_map bit for bit,
 * including through table growth and hash collisions.
 */

#include "hypute.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_map>

int main() {
    // 1. Basic accumulate / query / contains.
    {
        hypute_engine* e = hypute_create(0);
        hypute_update(e, 10, 20, 3.0);
        hypute_update(e, 10, 20, 1.5);
        hypute_update(e, 99, 1, 7.0);
        assert(hypute_query(e, 10, 20) == 4.5);
        assert(hypute_query(e, 99, 1)  == 7.0);
        assert(hypute_query(e, 5, 5)   == 0.0);   // unseen -> exactly 0
        assert(hypute_contains(e, 10, 20) == 1);
        assert(hypute_contains(e, 5, 5)   == 0);
        assert(hypute_size(e) == 2);
        assert(hypute_records(e) == 3);
        hypute_free(e);
    }

    // 2. Exact match against a reference map through heavy growth + collisions.
    {
        hypute_engine* e = hypute_create(0);         // tiny start -> forces many rehashes
        std::unordered_map<uint64_t, double> ref;
        std::mt19937_64 g(42);
        for (int i = 0; i < 2'000'000; ++i) {
            uint64_t s = g() % 300000, t = g() % 300000;
            double v = 1.0 + (g() % 7);
            hypute_update(e, s, t, v);
            ref[(s << 32) | t] += v;
        }
        assert(hypute_size(e) == ref.size());
        for (const auto& kv : ref) {
            double got = hypute_query(e, kv.first >> 32, kv.first & 0xffffffffULL);
            assert(got == kv.second);                // EXACT, not approximate
        }
        // And absent keys read as 0.
        assert(hypute_query(e, 999999999ULL, 999999999ULL) == 0.0);
        hypute_free(e);
    }

    // 3. Large 64-bit ids (no 32-bit packing assumptions inside the engine).
    {
        hypute_engine* e = hypute_create(0);
        uint64_t big1 = 0xFFFFFFFF00000001ULL, big2 = 0x00000001FFFFFFFFULL;
        hypute_update(e, big1, big2, 2.0);
        hypute_update(e, big2, big1, 5.0);           // swapped -> a different key
        assert(hypute_query(e, big1, big2) == 2.0);
        assert(hypute_query(e, big2, big1) == 5.0);
        assert(hypute_size(e) == 2);
        hypute_free(e);
    }

    std::printf("all tests passed\n");
    return 0;
}
