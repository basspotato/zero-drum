#pragma once

#include <cstdint>

namespace zd::dsp {

/// Small, fast, seedable PRNG (xorshift64*). Deterministic across platforms.
class Random
{
public:
    explicit Random(std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept { setSeed(seed); }

    void setSeed(std::uint64_t seed) noexcept { state_ = seed ? seed : 0x9E3779B97F4A7C15ull; }

    std::uint64_t next() noexcept
    {
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return state_ * 0x2545F4914F6CDD1Dull;
    }

    /// Uniform in [0, 1).
    double uniform() noexcept { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    /// Uniform in [-1, 1).
    double bipolar() noexcept { return 2.0 * uniform() - 1.0; }

private:
    std::uint64_t state_ = 0;
};

/// Stateless hash (splitmix64 finaliser) for decisions that must not depend on block size,
/// such as per-step probability: hash(seed, lane, absoluteStep) always gives the same answer.
inline std::uint64_t hash64(std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline double hashToUnit(std::uint64_t x) noexcept
{
    return static_cast<double>(hash64(x) >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace zd::dsp
