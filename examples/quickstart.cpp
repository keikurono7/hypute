// Minimal Hypute usage: aggregate a stream, then read exact totals back.
// Build: see README. Run: ./quickstart
#include "hypute.h"
#include <cstdio>

int main() {
    hypute_engine* e = hypute_create(/*capacity_hint*/ 0);

    // Stream of (source, target, value) interactions.
    hypute_update(e, /*user*/ 42, /*item*/ 7, 3.5);
    hypute_update(e, 42, 7, 1.0);        // same key accumulates
    hypute_update(e, 42, 9, 5.0);

    std::printf("(42,7) = %.2f   (42,9) = %.2f   unseen (1,1) = %.2f\n",
                hypute_query(e, 42, 7), hypute_query(e, 42, 9), hypute_query(e, 1, 1));
    std::printf("distinct keys: %zu   records ingested: %llu   memory: %zu bytes\n",
                hypute_size(e), (unsigned long long)hypute_records(e), hypute_memory_bytes(e));

    hypute_free(e);
    return 0;
}
