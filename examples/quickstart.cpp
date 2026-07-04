// Minimal Hypute usage: aggregate a stream, then query with bounded memory.
// Build: see README. Run: ./quickstart
#include "hypute.h"
#include <cstdio>

int main() {
    // Size for ~0.1% error at 99% confidence. Footprint is fixed from here on.
    hypute_engine* e = hypute_create_eps(0.001, 0.01);
    std::printf("footprint: %.1f MB (fixed, for any number of keys)\n",
                hypute_memory_bytes(e) / (1024.0 * 1024.0));

    // Stream of (source, target, value) interactions.
    hypute_update(e, /*user*/ 42, /*item*/ 7, 3.5);
    hypute_update(e, 42, 7, 1.0);
    hypute_update(e, 42, 9, 5.0);

    std::printf("total for (42,7): %.2f   (42,9): %.2f   unseen (1,1): %.2f\n",
                hypute_query(e, 42, 7), hypute_query(e, 42, 9), hypute_query(e, 1, 1));
    std::printf("records ingested: %llu\n", (unsigned long long)hypute_records(e));

    hypute_free(e);
    return 0;
}
