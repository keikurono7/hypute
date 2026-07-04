/*
 * Hypute core - exact open-addressed hash aggregation.
 *
 * A single contiguous table of {source, target, value} slots with linear
 * probing and a 1-bit-per-slot occupancy bitmap. Compared with a node-based
 * std::unordered_map, this stores the same data exactly, with no per-node heap
 * allocation and one cache line touched per access.
 *
 * Sizing: the table is NOT a power of two - it is sized directly to the key
 * count via Lemire's fast range reduction, and grown by 1.3x at load factor
 * 0.8. That keeps the table densely packed (~30 bytes/key) instead of the ~2x
 * overshoot a power-of-two table suffers, so the footprint stays below a
 * node-based map's while remaining exact.
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
// Map a 64-bit hash into [0, n) without a modulo (Lemire's fastrange).
inline size_t reduce(uint64_t h, size_t n) {
    return (size_t)(((unsigned __int128)h * (unsigned __int128)n) >> 64);
}

struct Slot { uint64_t s; uint64_t t; double v; };

} // namespace

struct hypute_engine {
    std::vector<Slot>     slots;
    std::vector<uint64_t> occ;      // occupancy bitmap, 1 bit per slot
    size_t cap  = 0;
    size_t used = 0;                // distinct keys
    uint64_t records = 0;           // updates ingested

    bool occupied(size_t i) const { return (occ[i >> 6] >> (i & 63)) & 1ULL; }
    void set(size_t i)             { occ[i >> 6] |= (1ULL << (i & 63)); }

    void allocate(size_t c) {
        if (c < 16) c = 16;
        cap = c;
        slots.assign(c, Slot{0, 0, 0.0});
        occ.assign((c + 63) / 64, 0ULL);
        used = 0;
    }

    void place(uint64_t s, uint64_t t, double v) {   // insert into a fresh cell (no accumulate)
        size_t i = reduce(hash_key(s, t), cap);
        while (occupied(i)) { if (++i == cap) i = 0; }
        slots[i] = Slot{s, t, v};
        set(i);
        ++used;
    }

    void grow() {
        std::vector<Slot>     old_slots; old_slots.swap(slots);
        std::vector<uint64_t> old_occ;   old_occ.swap(occ);
        size_t old_cap = cap;
        allocate(old_cap + old_cap / 3 + 1);         // ~1.3x
        for (size_t i = 0; i < old_cap; ++i)
            if ((old_occ[i >> 6] >> (i & 63)) & 1ULL) {
                const Slot& sl = old_slots[i];
                place(sl.s, sl.t, sl.v);
            }
    }
};

extern "C" {

hypute_engine* hypute_create(size_t capacity_hint) {
    hypute_engine* e = new (std::nothrow) hypute_engine();
    if (!e) return nullptr;
    try {
        e->allocate(capacity_hint ? capacity_hint * 5 / 4 + 1 : 1024);  // headroom to ~0.8 load
    } catch (...) { delete e; return nullptr; }
    return e;
}

void hypute_free(hypute_engine* e) { delete e; }

void hypute_update(hypute_engine* e, uint64_t source_id, uint64_t target_id, double value) {
    ++e->records;
    size_t i = reduce(hash_key(source_id, target_id), e->cap);
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) {
            e->slots[i].v += value;                  // existing key: accumulate
            return;
        }
        if (++i == e->cap) i = 0;
    }
    e->slots[i] = Slot{ source_id, target_id, value };
    e->set(i);
    if (++e->used * 5 >= e->cap * 4) e->grow();       // keep load factor <= 0.8
}

double hypute_query(const hypute_engine* e, uint64_t source_id, uint64_t target_id) {
    size_t i = reduce(hash_key(source_id, target_id), e->cap);
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) return e->slots[i].v;
        if (++i == e->cap) i = 0;
    }
    return 0.0;
}

int hypute_contains(const hypute_engine* e, uint64_t source_id, uint64_t target_id) {
    size_t i = reduce(hash_key(source_id, target_id), e->cap);
    while (e->occupied(i)) {
        if (e->slots[i].s == source_id && e->slots[i].t == target_id) return 1;
        if (++i == e->cap) i = 0;
    }
    return 0;
}

size_t   hypute_size(const hypute_engine* e)         { return e->used; }
uint64_t hypute_records(const hypute_engine* e)      { return e->records; }
size_t   hypute_memory_bytes(const hypute_engine* e) {
    return e->cap * sizeof(Slot) + e->occ.size() * sizeof(uint64_t);
}

} // extern "C"
