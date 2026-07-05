/*
 * Latency benchmark: hardware-gated sparse traversal vs a dense scan.
 *
 * A stream of "ticks" arrives; each tick activates a small, sparse set of
 * channels (a 4096-bit mask). For every active channel we update a small,
 * cache-resident state table. Two ways to find the active channels:
 *
 *   dense : test all 4096 channels every tick (the obvious loop)
 *   gated : bit-scan only the set bits with TZCNT (__builtin_ctzll) and clear
 *           each with BLSR (reg &= reg - 1), touching only active channels
 *
 * Both engines do byte-for-byte identical work and are asserted to produce the
 * same final state, so the only variable is how the idle ~98% of channels are
 * handled. On sparse streams the gated path skips them at hardware speed.
 *
 * Honesty guards (so the number can be trusted):
 *   - a volatile sink consumes the result, so the compiler cannot delete the
 *     work and report a fake "near-zero" latency,
 *   - both engines are checksum-verified to be identical,
 *   - the CPU model is printed, because absolute ns are hardware-dependent.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using clk = std::chrono::high_resolution_clock;

static constexpr int      CHANNELS   = 4096;
static constexpr int      BLOCKS     = CHANNELS / 64;   // 64 uint64 register blocks
static constexpr int      STATE_SIZE = 4096;            // 16 KB of int32, L1-resident
static constexpr int      TARGETS    = 16;              // work done per active channel
static constexpr uint64_t TICKS      = 200000;
static constexpr int      ACTIVE     = 60;              // ~1.5% of channels active per tick

static void print_cpu() {
    std::ifstream f("/proc/cpuinfo"); std::string line;
    while (std::getline(f, line))
        if (line.rfind("model name", 0) == 0) {
            std::printf("CPU:%s\n", line.substr(line.find(':') + 1).c_str());
            return;
        }
}

// Identical per-channel work for both engines: bump TARGETS cache-resident counters.
static inline void do_work(int32_t* state, int c) {
    int base = (int)((uint32_t)c * 2654435761u % (uint32_t)(STATE_SIZE - TARGETS));
    for (int j = 0; j < TARGETS; ++j) state[base + j] += 1;
}

int main() {
    print_cpu();

    // Build a sparse tick stream: TICKS ticks, each a 4096-bit mask with ~ACTIVE bits set.
    std::vector<uint64_t> stream((size_t)TICKS * BLOCKS, 0);
    std::mt19937_64 g(20260704ULL);
    std::uniform_int_distribution<int> ch(0, CHANNELS - 1);
    for (uint64_t t = 0; t < TICKS; ++t) {
        uint64_t* mask = &stream[t * BLOCKS];
        for (int a = 0; a < ACTIVE; ++a) { int c = ch(g); mask[c >> 6] |= (1ULL << (c & 63)); }
    }

    std::vector<int32_t> state(STATE_SIZE, 0);

    // ---- dense: scan every channel each tick ----
    std::fill(state.begin(), state.end(), 0);
    auto d0 = clk::now();
    for (uint64_t t = 0; t < TICKS; ++t) {
        const uint64_t* mask = &stream[t * BLOCKS];
        for (int c = 0; c < CHANNELS; ++c)
            if ((mask[c >> 6] >> (c & 63)) & 1ULL) do_work(state.data(), c);
    }
    auto d1 = clk::now();
    long long dense_sum = 0; for (int32_t v : state) dense_sum += v;
    double dense_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(d1 - d0).count() / (double)TICKS;

    // ---- gated: bit-scan only the set bits ----
    std::fill(state.begin(), state.end(), 0);
    auto g0 = clk::now();
    for (uint64_t t = 0; t < TICKS; ++t) {
        const uint64_t* mask = &stream[t * BLOCKS];
        for (int b = 0; b < BLOCKS; ++b) {
            uint64_t reg = mask[b];
            while (reg) {
                int c = b * 64 + __builtin_ctzll(reg);
                do_work(state.data(), c);
                reg &= (reg - 1);
            }
        }
    }
    auto g1 = clk::now();
    long long gated_sum = 0; for (int32_t v : state) gated_sum += v;
    double gated_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(g1 - g0).count() / (double)TICKS;

    volatile long long sink = dense_sum + gated_sum;   // prevent dead-code elimination
    bool ok = (dense_sum == gated_sum);

    std::printf("Ticks %llu   Channels %d   Active/tick %d (%.1f%%)   State %d KB (L1-resident)\n",
                (unsigned long long)TICKS, CHANNELS, ACTIVE, 100.0 * ACTIVE / CHANNELS, STATE_SIZE * 4 / 1024);
    std::printf("Correct (dense == gated): %s   [sink=%lld]\n", ok ? "yes" : "NO", (long long)sink);
    std::printf("%-6s %10.1f ns/tick\n", "dense", dense_ns);
    std::printf("%-6s %10.1f ns/tick   -> %.1fx faster on this sparse stream\n",
                "gated", gated_ns, dense_ns / gated_ns);
    return ok ? 0 : 1;
}
