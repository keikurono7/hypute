#ifndef HYPUTE_H
#define HYPUTE_H

/*
 * Hypute - bounded-memory streaming aggregation.
 *
 * Hypute maintains running per-key totals over an unbounded stream using a
 * fixed amount of memory, independent of how many distinct keys or records it
 * sees. It is an approximate engine built on a Count-Min Sketch: queries return
 * an estimate with a provable, tunable error bound rather than an exact value.
 *
 * Use it when the working set of distinct keys would not fit in RAM (or would
 * cost too much cloud memory) and a small, bounded over-estimate is acceptable
 * - e.g. per-entity counters, rate/volume tracking, top-K feeds, telemetry
 * roll-ups, feature aggregation. Use an exact store when you need the precise
 * value for every key.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hypute_engine hypute_engine;

/*
 * Create an engine with an explicit sketch geometry.
 *   width : counters per row (rounded up to a power of two). Larger -> lower error.
 *   depth : number of independent rows.               Larger -> higher confidence.
 * Footprint is fixed at width*depth*8 bytes for the engine's whole lifetime.
 * Returns NULL on invalid arguments or allocation failure.
 */
hypute_engine* hypute_create(size_t width, size_t depth);

/*
 * Create an engine sized from a target accuracy instead of raw geometry:
 *   epsilon : relative error factor, 0 < epsilon < 1  (width = ceil(e/epsilon))
 *   delta   : failure probability,   0 < delta   < 1  (depth = ceil(ln(1/delta)))
 * The over-estimate stays below epsilon * (total ingested weight) with
 * probability at least 1 - delta. Example: (0.001, 0.01) -> ~0.1% of total
 * weight error, 99% confidence.
 */
hypute_engine* hypute_create_eps(double epsilon, double delta);

/* Release all memory held by the engine. */
void hypute_free(hypute_engine* e);

/*
 * Ingest one interaction: add `value` to the running total for (source, target).
 * O(depth), allocation-free. `value` must be >= 0 for the error bound to hold.
 */
void hypute_update(hypute_engine* e, uint64_t source_id, uint64_t target_id, double value);

/*
 * Estimated accumulated value for (source, target). For non-negative updates
 * this never under-estimates the true total; the over-estimate is bounded as
 * described in hypute_create_eps.
 */
double hypute_query(const hypute_engine* e, uint64_t source_id, uint64_t target_id);

/* Introspection. */
size_t   hypute_memory_bytes(const hypute_engine* e);  /* fixed footprint in bytes   */
uint64_t hypute_records(const hypute_engine* e);       /* number of updates ingested */
double   hypute_total_weight(const hypute_engine* e);  /* sum of all ingested values */
double   hypute_epsilon(const hypute_engine* e);       /* current error factor e/width */

#ifdef __cplusplus
}
#endif

#endif /* HYPUTE_H */
