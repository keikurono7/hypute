/*
 * Hypute core - exact open-addressed hash aggregation.
 *
 * A single contiguous table of {source, target, value} slots with linear
 * probing and a 1-bit-per-slot occupancy bitmap. Compared with a node-based
 * std::unordered_map (or a nested map), this:
 *   - stores the same data exactly (no loss, no approximation),
 *   - uses ~24 bytes/key + probing headroom instead of ~48-80 bytes/key of
 *     per-node allocation + bucket pointers,
 *   - touches one cache line per access instead of chasing pointers across
 *     the heap, so throughput holds up when the stream is unordered.
 *
 * Load factor is kept at 0.7; the table doubles and rehashes when exceeded.
 */

#include "hypute.h"

#include <vector>
#include <cstdint>
#include <new>

namespace {

inline uint64_t mix64(uint64_t x) {                 // MurmurHash3 finalizer
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
inline uint64_t hash_key(uint64_t s, uint64_t t) {
    return mix64(s ^ (mix64(t) * 0x9E3779B97F4A7C15ULL));
}
inline size_t next_pow2(size_t v) {
    if (v < 16) return 16;
    --v; v |= v>>1; v |= v>>2; v |= v>>4; v |= v>>8; v |= v>>16; v |= v>>32;
    return v + 1;
}

struct Slot { uint64_t s; uint64_t t; double v; };

} // namespace

struct hypute_engine {
    std::vector<Slot>     slots;
    std::vector<uint64_t> occ;      // occupancy bitmap, 1 bit per slot
    size_t cap  = 0;                // power of two
    size_t mask = 0;                // cap - 1
    size_t used = 0;                // distinct keys
    uint64_t records = 0;           // updates ingested

    bool occupied(size_t i) const { return (occ[i >> 6] >> (i & 63)) & 1ULL; }
    void set(size_t i)             { occ[i >> 6] |= (1ULL << (i & 63)); }

    void allocate(size_t c) {
        cap = c; mask = c - 1;
        slots.assign(c, Slot{0, 0, 0.0});
        occ.assign((c + 63) / 64, 0ULL);
        used = 0;
    }

    void grow() {
        std::vector<Slot>     old_slots; old_slots.swap(slots);
        std::vector<uint64_t> old_occ;   old_occ.swap(occ);
        size_t old_cap = cap;
        allocate(old_cap * 2);
        for (size_t i = 0; i < old_cap; ++i) {
            if ((old_occ[i >> 6] >> (i & 63)) & 1ULL) {
                const Slot& sl = old_slots[i];
                size_t j = hash_key(sl.s, sl.t) & mask;
                while (occupied(j)) j = (j + 1) & mask;
                slots[j] = sl; set(j); ++used;
            }
        }
    }
};

extern "C" {

hypute_engine* hypute_create(size_t capacity_hint) {
    hypute_engine* e = new (std::nothrow) hypute_engine();
    if (!e) return nullptr;
    try {
        e->allocate(next_pow2(capacity_hint ? capacity_hint * 10 / 7 + 1 : 1024));
    } catch (...) { delete e; return nullptr; }
    return e;
}

void hypute_free(hypute_engine* e) { delete e; }

void hypute_update(hypute_engine* e, uint64_t source_id, uint64_t target_id, double value) {
    ++e->records;
    size_t i = hash_key(source_id, target_id) & e->mask;
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) {
            e->slots[i].v += value;                 // existing key: accumulate
            return;
        }
        i = (i + 1) & e->mask;
    }
    e->slots[i] = Slot{ source_id, target_id, value };
    e->set(i);
    if (++e->used * 10 >= e->cap * 7) e->grow();     // keep load factor <= 0.7
}

double hypute_query(const hypute_engine* e, uint64_t source_id, uint64_t target_id) {
    size_t i = hash_key(source_id, target_id) & e->mask;
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) return e->slots[i].v;
        i = (i + 1) & e->mask;
    }
    return 0.0;
}

int hypute_contains(const hypute_engine* e, uint64_t source_id, uint64_t target_id) {
    size_t i = hash_key(source_id, target_id) & e->mask;
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) return 1;
        i = (i + 1) & e->mask;
    }
    return 0;
}

size_t   hypute_size(const hypute_engine* e)         { return e->used; }
uint64_t hypute_records(const hypute_engine* e)      { return e->records; }
size_t   hypute_memory_bytes(const hypute_engine* e) {
    return e->cap * sizeof(Slot) + e->occ.size() * sizeof(uint64_t);
}

} // extern "C"
