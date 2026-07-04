#ifndef HYPUTE_H
#define HYPUTE_H

/*
 * Hypute - fast, exact streaming aggregation.
 *
 * Hypute keeps an exact running total for every (source, target) key over an
 * unbounded stream of interactions. It is a drop-in replacement for the common
 * pattern of a nested std::unordered_map<source, unordered_map<target, value>>
 * (or a flat map keyed by the pair) - but it stores the same data in a single
 * cache-friendly open-addressed table, so it is faster, uses less memory, and
 * holds its throughput far better when the stream arrives unordered (as real
 * production streams do).
 *
 * Results are EXACT: hypute_query returns exactly the sum an equivalent map
 * would hold. Nothing is approximated or dropped. Memory grows with the number
 * of distinct keys, as it must for an exact store - the win is a smaller
 * constant factor and much better access locality.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hypute_engine hypute_engine;

/* Create an engine. `capacity_hint` pre-sizes the table to avoid early rehashes
 * (0 = a small default). Returns NULL on allocation failure. */
hypute_engine* hypute_create(size_t capacity_hint);

/* Release the engine and all its memory. */
void hypute_free(hypute_engine* e);

/* Add `value` to the running total for (source, target). O(1) amortized. */
void hypute_update(hypute_engine* e, uint64_t source_id, uint64_t target_id, double value);

/* Exact accumulated total for (source, target); 0.0 if the key was never seen. */
double hypute_query(const hypute_engine* e, uint64_t source_id, uint64_t target_id);

/* 1 if the key has been seen, else 0. */
int hypute_contains(const hypute_engine* e, uint64_t source_id, uint64_t target_id);

/* Introspection. */
size_t   hypute_size(const hypute_engine* e);          /* number of distinct keys         */
uint64_t hypute_records(const hypute_engine* e);       /* number of updates ingested      */
size_t   hypute_memory_bytes(const hypute_engine* e);  /* current table footprint (bytes) */

#ifdef __cplusplus
}
#endif

#endif /* HYPUTE_H */
