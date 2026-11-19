#include "ChannelFilter.h"

#include <algorithm>

namespace of
{
void ChannelFilter::reset() noexcept
{
    for (auto& s : sections)
        s = Section {};
    count = 0;
    coefRamp = 0;
    mix = mixTarget = mixInc = 0.0;
    mixRamp = 0;
}

void ChannelFilter::setCoeffs (const SvfCoeffs* coeffs, int newCount, int rampSamples) noexcept
{
    newCount = std::clamp (newCount, 0, kMaxSections);

    if (! active() || newCount != count)
    {
        // Idle (or a count change the caller should have faded out for): load directly.
        count = newCount;
        coefRamp = 0;
        for (int i = 0; i < count; ++i)
        {
            auto& s = sections[static_cast<size_t> (i)];
            s.cur = s.target = coeffs[i];
            s.inc = {};
            s.s1 = s.s2 = 0.0;
        }
        return;
    }

    coefRamp = std::max (1, rampSamples);
    const double inv = 1.0 / coefRamp;
    for (int i = 0; i < count; ++i)
    {
        auto& s = sections[static_cast<size_t> (i)];
        const auto& t = coeffs[i];
        s.target = t;
        s.inc = { (t.g - s.cur.g) * inv, (t.k - s.cur.k) * inv, (t.m0 - s.cur.m0) * inv,
                  (t.m1 - s.cur.m1) * inv, (t.m2 - s.cur.m2) * inv };
    }
}

void ChannelFilter::fadeTo (bool in, int rampSamples) noexcept
{
    const double goal = in ? 1.0 : 0.0;
    if (goal == mixTarget && (mixRamp > 0 || mix == goal))
        return;   // already there or on the way

    mixTarget = goal;
    if (mix == goal)
    {
        mixRamp = 0;
        return;
    }
    mixRamp = std::max (1, rampSamples);
    mixInc = (goal - mix) / mixRamp;
}

void ChannelFilter::process (double* x, int n) noexcept
{
    while (n > 0 && active())
    {
        const int len = std::min (n, kChunk);
        processChunk (x, len);
        x += len;
        n -= len;
    }
}

void ChannelFilter::processChunk (double* x, int n) noexcept
{
    const bool direct = mix == 1.0 && mixRamp == 0;   // fully wet: filter in place
    double* buf = x;
    if (! direct)
    {
        std::copy_n (x, n, scratch.data());
        buf = scratch.data();
    }

    for (int si = 0; si < count; ++si)
    {
        auto& s = sections[static_cast<size_t> (si)];
        auto c = s.cur;
        double s1 = s.s1, s2 = s.s2;
        int i = 0;

        // Ramping part: coefficients move every sample.
        for (int ramp = coefRamp; i < n && ramp > 0; ++i)
        {
            if (--ramp == 0)
                c = s.target;
            else
            {
                c.g += s.inc.g;    c.k += s.inc.k;
                c.m0 += s.inc.m0;  c.m1 += s.inc.m1;  c.m2 += s.inc.m2;
            }
            const double t1 = 1.0 / (1.0 + c.g * (c.g + c.k)), t2 = c.g * t1, t3 = c.g * t2;
            const double v0 = buf[i];
            const double v3 = v0 - s2;
            const double v1 = t1 * s1 + t2 * v3;
            const double v2 = s2 + t2 * s1 + t3 * v3;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            buf[i] = c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
        }

        // Steady part.
        const double t1 = 1.0 / (1.0 + c.g * (c.g + c.k)), t2 = c.g * t1, t3 = c.g * t2;
        for (; i < n; ++i)
        {
            const double v0 = buf[i];
            const double v3 = v0 - s2;
            const double v1 = t1 * s1 + t2 * v3;
            const double v2 = s2 + t2 * s1 + t3 * v3;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            buf[i] = c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
        }

        s.cur = c;
        s.s1 = s1;
        s.s2 = s2;
    }
    coefRamp = std::max (0, coefRamp - n);

    if (direct)
        return;

    for (int i = 0; i < n; ++i)
    {
        if (mixRamp > 0)
        {
            mix += mixInc;
            if (--mixRamp == 0)
                mix = mixTarget;
        }
        x[i] += mix * (buf[i] - x[i]);
    }

    if (mix == 0.0 && mixRamp == 0)   // faded out: go idle with clean state
        for (auto& s : sections)
            s.s1 = s.s2 = 0.0;
}
} // namespace of
