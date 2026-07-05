#ifndef HYPUTE_H
#define HYPUTE_H

/*
 * Hypute - real-time top-K over massive streams, in kilobytes.
 *
 * Feed it a stream of item ids (product views, clicks, requests, IPs, ...) and
 * at any moment ask "what are the top-K heaviest hitters right now?". Hypute
 * answers from a tiny, fixed structure that lives entirely inside the CPU cache
 * - no per-item storage, no RAM round-trips - so it stays fast no matter how
 * many distinct items or events flow through, and its memory never grows.
 *
 * It is approximate by design: it tracks the heavy hitters (the items that
 * matter for "trending" / "top") with high accuracy, and does not remember the
 * long tail of rare items at all. That trade is what lets the whole thing fit
 * in cache. Use it for trending / leaderboards / hot-key & abuse detection -
 * not as an exact per-item ledger.
 *
 * Author: Madhusudan  <dmpathani@gmail.com>  https://github.com/keikurono7
 * MIT licensed.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hypute_topk hypute_topk;

/* Create a top-K tracker.
 *   k      : how many top items to maintain.
 *   width  : counters per sketch row (larger -> more accuracy). 0 = default.
 *   depth  : number of sketch rows.                             0 = default.
 * Total memory is fixed for the tracker's whole lifetime (see hypute_memory_bytes). */
hypute_topk* hypute_create(size_t k, size_t width, size_t depth);

void hypute_free(hypute_topk* h);

/* Ingest one event: item `id` occurred with `weight` (use 1.0 to count). O(depth). */
void hypute_update(hypute_topk* h, uint64_t id, double weight);

/* Fill items_out/scores_out with the current top items (highest first) and
 * return how many were written (<= k <= max). Both arrays must hold `max`. */
size_t hypute_top(const hypute_topk* h, uint64_t* items_out, double* scores_out, size_t max);

/* Estimated total weight for a single id (heavy hitters accurate; tail ~noise). */
double hypute_estimate(const hypute_topk* h, uint64_t id);

/* Introspection. */
size_t   hypute_memory_bytes(const hypute_topk* h);  /* fixed footprint in bytes   */
uint64_t hypute_records(const hypute_topk* h);       /* number of events ingested  */

#ifdef __cplusplus
}
#endif

#endif /* HYPUTE_H */
