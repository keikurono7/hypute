#ifndef HYPUTE_GATE_H
#define HYPUTE_GATE_H

/*
 * Hypute gating - the sparse-traversal primitive, header-only.
 *
 * Given a bitmask spread across `nblocks` 64-bit words (one bit per channel),
 * hypute_for_each_set_bit invokes fn(channel_index) for ONLY the set bits,
 * using a hardware bit-scan (TZCNT via __builtin_ctzll) and clearing each bit
 * with BLSR (reg &= reg - 1). Idle channels are never touched.
 *
 * This is the exact primitive measured in bench/gating_benchmark.cpp (~8x
 * faster than a dense scan on sparse streams). It is fully inlinable, so a
 * caller pays no abstraction cost. C++ only (templated on the callback).
 *
 * Example:
 *   uint64_t mask[64];                 // 4096 channels
 *   hypute_for_each_set_bit(mask, 64, [&](size_t ch){ handle(ch); });
 *
 * Author: Madhusudan  <dmpathani@gmail.com>  https://github.com/keikurono7
 * MIT licensed.
 */

#include <cstdint>
#include <cstddef>

template <typename Fn>
inline void hypute_for_each_set_bit(const uint64_t* blocks, std::size_t nblocks, Fn&& fn) {
    for (std::size_t b = 0; b < nblocks; ++b) {
        uint64_t reg = blocks[b];
        while (reg) {
            fn(b * 64 + static_cast<std::size_t>(__builtin_ctzll(reg)));
            reg &= (reg - 1);
        }
    }
}

#endif /* HYPUTE_GATE_H */
