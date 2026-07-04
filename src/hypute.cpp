/*
 * Hypute core - cache-resident top-K (heavy hitters).
 *
 * Two small pieces, both sized to live in CPU cache:
 *   1) a Count-Min sketch (depth rows x width counters) that estimates each
 *      item's running weight in fixed memory, and
 *   2) a min-heap of the current top-K items (smallest at the root) plus a tiny
 *      id->heap-slot map so updates are O(1) + O(log k).
 *
 * On each event we bump the sketch, read the estimate, and let the item into
 * the top-K heap if it now outweighs the lightest tracked item. Nothing scales
 * with the number of distinct items - the whole thing is a few hundred KB and
 * stays hot in L2, which is where the speed and the RAM-latency immunity come
 * from.
 */

#include "hypute.h"

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <new>

namespace {

inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
inline size_t next_pow2(size_t v) {
    if (v < 256) return 256;
    --v; v |= v>>1; v |= v>>2; v |= v>>4; v |= v>>8; v |= v>>16; v |= v>>32;
    return v + 1;
}

struct HeapEnt { double score; uint64_t id; };

} // namespace

struct hypute_topk {
    // Count-Min sketch
    std::vector<double>   cms;      // depth * width
    std::vector<uint64_t> seed;     // per-row hash seed
    size_t width = 0, depth = 0, mask = 0;

    // Top-K min-heap + id -> heap index
    size_t k = 0;
    std::vector<HeapEnt> heap;
    std::unordered_map<uint64_t, size_t> pos;

    uint64_t records = 0;

    // Heap helpers (min-heap on score). Keep pos[] in sync.
    void swap_ent(size_t a, size_t b) {
        std::swap(heap[a], heap[b]);
        pos[heap[a].id] = a;
        pos[heap[b].id] = b;
    }
    void sift_up(size_t i) {
        while (i > 0) {
            size_t p = (i - 1) / 2;
            if (heap[p].score <= heap[i].score) break;
            swap_ent(i, p); i = p;
        }
    }
    void sift_down(size_t i) {
        size_t n = heap.size();
        for (;;) {
            size_t l = 2*i+1, r = 2*i+2, s = i;
            if (l < n && heap[l].score < heap[s].score) s = l;
            if (r < n && heap[r].score < heap[s].score) s = r;
            if (s == i) break;
            swap_ent(i, s); i = s;
        }
    }
};

extern "C" {

hypute_topk* hypute_create(size_t k, size_t width, size_t depth) {
    if (k == 0) k = 100;
    if (width == 0) width = 8192;      // 8192 * depth counters, cache-resident
    if (depth == 0) depth = 4;
    width = next_pow2(width);

    hypute_topk* h = new (std::nothrow) hypute_topk();
    if (!h) return nullptr;
    h->width = width; h->depth = depth; h->mask = width - 1; h->k = k;
    try {
        h->cms.assign(width * depth, 0.0);
        h->seed.resize(depth);
        h->heap.reserve(k);
        h->pos.reserve(k * 2);
    } catch (...) { delete h; return nullptr; }
    for (size_t i = 0; i < depth; ++i) h->seed[i] = mix64(0x9E3779B97F4A7C15ULL * (uint64_t)(i + 1));
    return h;
}

void hypute_free(hypute_topk* h) { delete h; }

void hypute_update(hypute_topk* h, uint64_t id, double weight) {
    ++h->records;

    // Bump the sketch and read the min estimate.
    double est = 1e300;
    for (size_t r = 0; r < h->depth; ++r) {
        size_t idx = r * h->width + (size_t)(mix64(id ^ h->seed[r]) & h->mask);
        h->cms[idx] += weight;
        if (h->cms[idx] < est) est = h->cms[idx];
    }

    // Maintain the top-K heap.
    auto it = h->pos.find(id);
    if (it != h->pos.end()) {                       // already tracked: score rose
        h->heap[it->second].score = est;
        h->sift_down(it->second);                   // min-heap, key increased -> sinks
    } else if (h->heap.size() < h->k) {             // room: add it
        h->heap.push_back(HeapEnt{est, id});
        h->pos[id] = h->heap.size() - 1;
        h->sift_up(h->heap.size() - 1);
    } else if (est > h->heap[0].score) {            // beats the lightest tracked item
        h->pos.erase(h->heap[0].id);
        h->heap[0] = HeapEnt{est, id};
        h->pos[id] = 0;
        h->sift_down(0);
    }
}

double hypute_estimate(const hypute_topk* h, uint64_t id) {
    double est = 1e300;
    for (size_t r = 0; r < h->depth; ++r) {
        double c = h->cms[r * h->width + (size_t)(mix64(id ^ h->seed[r]) & h->mask)];
        if (c < est) est = c;
    }
    return est == 1e300 ? 0.0 : est;
}

size_t hypute_top(const hypute_topk* h, uint64_t* items_out, double* scores_out, size_t max) {
    std::vector<HeapEnt> v(h->heap.begin(), h->heap.end());
    std::sort(v.begin(), v.end(), [](const HeapEnt& a, const HeapEnt& b){ return a.score > b.score; });
    size_t n = v.size() < max ? v.size() : max;
    for (size_t i = 0; i < n; ++i) { items_out[i] = v[i].id; scores_out[i] = v[i].score; }
    return n;
}

size_t hypute_memory_bytes(const hypute_topk* h) {
    return h->cms.size() * sizeof(double)
         + h->seed.size() * sizeof(uint64_t)
         + h->heap.capacity() * sizeof(HeapEnt)
         + h->pos.size() * (sizeof(uint64_t) + sizeof(size_t) + 16);  // approx map overhead
}
uint64_t hypute_records(const hypute_topk* h) { return h->records; }

} // extern "C"
