/*
 * Hypute core - Count-Min Sketch over a fixed counter grid.
 *
 * depth independent rows, each `width` counters wide. update() adds `value` to
 * one counter per row (row-specific hash); query() returns the minimum across
 * rows, which for non-negative weights is a tight over-estimate of the true
 * per-key total. Memory is width*depth doubles and never grows.
 */

#include "hypute.h"

#include <vector>
#include <cmath>
#include <new>

namespace {

// 64-bit finalizer (fmix64 from MurmurHash3) - strong, cheap avalanche.
inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

inline size_t next_pow2(size_t v) {
    if (v < 2) return 2;
    --v;
    v |= v >> 1;  v |= v >> 2;  v |= v >> 4;
    v |= v >> 8;  v |= v >> 16; v |= v >> 32;
    return v + 1;
}

constexpr double E = 2.718281828459045;

} // namespace

struct hypute_engine {
    size_t width = 0;
    size_t depth = 0;
    size_t mask  = 0;              // width - 1 (width is a power of two)
    std::vector<double>   counters;
    std::vector<uint64_t> seed;    // one hash seed per row
    uint64_t records = 0;
    double   total   = 0.0;
};

namespace {
// Fold a (source, target) pair into a single hashed base, then derive the
// per-row column from that base and the row seed.
inline uint64_t fold_key(uint64_t s, uint64_t t) {
    return mix64(s) ^ (mix64(t) * 0x9E3779B97F4A7C15ULL);
}
inline size_t column(const hypute_engine* e, size_t row, uint64_t base) {
    return row * e->width + (size_t)(mix64(base ^ e->seed[row]) & e->mask);
}
} // namespace

extern "C" {

hypute_engine* hypute_create(size_t width, size_t depth) {
    if (width == 0 || depth == 0) return nullptr;
    width = next_pow2(width);

    hypute_engine* e = new (std::nothrow) hypute_engine();
    if (!e) return nullptr;

    e->width = width;
    e->depth = depth;
    e->mask  = width - 1;
    try {
        e->counters.assign(width * depth, 0.0);
        e->seed.resize(depth);
    } catch (...) {
        delete e;
        return nullptr;
    }
    for (size_t i = 0; i < depth; ++i)
        e->seed[i] = mix64(0x9E3779B97F4A7C15ULL * (uint64_t)(i + 1));
    return e;
}

hypute_engine* hypute_create_eps(double epsilon, double delta) {
    if (!(epsilon > 0.0 && epsilon < 1.0)) return nullptr;
    if (!(delta   > 0.0 && delta   < 1.0)) return nullptr;
    size_t width = (size_t)std::ceil(E / epsilon);
    size_t depth = (size_t)std::ceil(std::log(1.0 / delta));
    if (depth < 1) depth = 1;
    return hypute_create(width, depth);
}

void hypute_free(hypute_engine* e) { delete e; }

void hypute_update(hypute_engine* e, uint64_t source_id, uint64_t target_id, double value) {
    const uint64_t base = fold_key(source_id, target_id);
    for (size_t r = 0; r < e->depth; ++r)
        e->counters[column(e, r, base)] += value;
    ++e->records;
    e->total += value;
}

double hypute_query(const hypute_engine* e, uint64_t source_id, uint64_t target_id) {
    const uint64_t base = fold_key(source_id, target_id);
    double m = e->counters[column(e, 0, base)];
    for (size_t r = 1; r < e->depth; ++r) {
        double x = e->counters[column(e, r, base)];
        if (x < m) m = x;
    }
    return m;
}

size_t hypute_memory_bytes(const hypute_engine* e) {
    return e->width * e->depth * sizeof(double) + e->depth * sizeof(uint64_t);
}
uint64_t hypute_records(const hypute_engine* e)     { return e->records; }
double   hypute_total_weight(const hypute_engine* e) { return e->total; }
double   hypute_epsilon(const hypute_engine* e)      { return E / (double)e->width; }

} // extern "C"
