#pragma once

#include <algorithm>

namespace zd {

/// Short linear fade used when a voice is stolen or choked, so cut-offs never click.
class KillFade
{
public:
    void prepare(double sampleRate) noexcept { length_ = std::max(1, static_cast<int>(0.002 * sampleRate)); }
    void reset() noexcept { remaining_ = -1; }
    void start() noexcept { if (remaining_ < 0) remaining_ = length_; }
    bool isFading() const noexcept { return remaining_ >= 0; }
    bool isFinished() const noexcept { return remaining_ == 0; }

    /// Gain for the next sample. Returns 1 when not fading.
    float next() noexcept
    {
        if (remaining_ < 0)
            return 1.0f;
        if (remaining_ == 0)
            return 0.0f;
        --remaining_;
        return static_cast<float>(remaining_) / static_cast<float>(length_);
    }

private:
    int length_ = 96;
    int remaining_ = -1;
};

/// Interface shared by every voice engine. Voices render mono and add into `out`.
/// All methods are realtime safe: no allocation, no locks, no I/O.
class Voice
{
public:
    virtual ~Voice() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void render(float* out, int numSamples) noexcept = 0;

    bool isActive() const noexcept { return active_; }
    void kill() noexcept { if (active_) fade_.start(); }
    void reset() noexcept { active_ = false; fade_.reset(); }

    unsigned age = 0; ///< trigger order, used for voice stealing

protected:
    void beginNote() noexcept { active_ = true; fade_.reset(); }
    void endNote() noexcept { active_ = false; fade_.reset(); }

    bool active_ = false;
    KillFade fade_;
};

} // namespace zd
