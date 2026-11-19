#pragma once

#include "../design/MatchedDesign.h"

namespace of
{
/** One band on one channel: a cascade of state-variable sections plus a wet/dry mix.

    - Small parameter moves interpolate the section coefficients (g, k, m0, m1, m2). Any
      g > 0, k > 0 is a stable section, so intermediate values are safe.
    - Switching the band in or out, or to an unrelated design, is a crossfade of the whole
      cascade against the dry signal. Sections are never faded one by one: a cascade often
      pairs a very high-gain section with a very low-gain one, and fading them separately
      would let the pair get out of step.
    - While the mix is a settled zero the filter is idle: it costs nothing and passes the
      signal bit for bit.

    Real-time safe: no allocation, no locks. */
class ChannelFilter
{
public:
    void reset() noexcept;

    /** New section coefficients. Loaded at once (with cleared state) when the filter is
        idle, otherwise interpolated over rampSamples. Callers must only change the section
        count while the filter is idle. */
    void setCoeffs (const SvfCoeffs* coeffs, int count, int rampSamples) noexcept;

    /** Crossfades the cascade in (true) or out to dry (false). */
    void fadeTo (bool in, int rampSamples) noexcept;

    void process (double* x, int n) noexcept;

    /** False when the filter is a settled bypass. */
    bool active() const noexcept { return mix != 0.0 || mixTarget != 0.0; }

private:
    struct Section
    {
        SvfCoeffs cur {}, target {}, inc {};
        double s1 = 0.0, s2 = 0.0;
    };

    void processChunk (double* x, int n) noexcept;

    static constexpr int kChunk = 128;

    std::array<Section, kMaxSections> sections {};
    std::array<double, kChunk> scratch {};
    int count = 0;
    int coefRamp = 0;
    double mix = 0.0, mixTarget = 0.0, mixInc = 0.0;
    int mixRamp = 0;
};
} // namespace of
