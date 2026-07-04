// Minimal Hypute top-K usage: stream item ids, ask what's trending.
// Build: see README. Run: ./quickstart
#include "hypute.h"
#include <cstdio>

int main() {
    hypute_topk* h = hypute_create(/*k*/ 5, /*width*/ 0, /*depth*/ 0);   // 0,0 = defaults

    // A stream of item events (e.g. product ids being viewed).
    const uint64_t stream[] = {10,10,10,10,10, 20,20,20, 30, 40,40,40,40, 50,50};
    for (uint64_t id : stream) hypute_update(h, id, 1.0);

    uint64_t items[5]; double scores[5];
    size_t n = hypute_top(h, items, scores, 5);

    std::printf("Top %zu trending  (footprint: %zu bytes, fixed):\n", n, hypute_memory_bytes(h));
    for (size_t i = 0; i < n; ++i)
        std::printf("  #%zu  item %llu  ~%.0f events\n", i + 1, (unsigned long long)items[i], scores[i]);

    hypute_free(h);
    return 0;
}
