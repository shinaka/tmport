#pragma once
// Borland C++ rand/srand and the game's random(n) idiom (re/docs/code_1000.md §5).

#include <cstdint>

namespace tmport {

class BorlandRand {
public:
    // 1000:0842 srand: the game passes a 16-bit value (low word of timeGetTime()).
    void srand(uint16_t seed) { seed_ = seed; }
    // 1000:0859 rand: seed = seed * 0x015A4E35 + 1 (mod 2^32); returns (seed >> 16) & 0x7FFF.
    int rand() {
        seed_ = seed_ * 0x015A4E35u + 1u;
        ++calls_;
        return static_cast<int>((seed_ >> 16) & 0x7FFF);
    }
    // R(n) = rand() * n / 32768 with 32-bit signed arithmetic (IMUL, CDQ, IDIV): 0..n-1.
    int random(int n);

    uint32_t seed() const { return seed_; }
    void setRawSeed(uint32_t s) { seed_ = s; }

    // rand() calls so far (diagnostics: the per-frame traces compare them with the DOSBox-X call
    // counts of tools/gg/capture_mem.py; nothing in the games reads it).
    uint64_t calls() const { return calls_; }
    void setCalls(uint64_t n) { calls_ = n; }

private:
    uint32_t seed_ = 1;  // initial value in DGROUP (DS:3044)
    uint64_t calls_ = 0;
};

}  // namespace tmport
