#pragma once

#include "../Types.h"

namespace of
{
/** One analog section, as polynomials in s where s = j * f and f is in Hz:
    H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0). First order when d2 == 0. */
struct AnalogSection
{
    double n0 = 1.0, n1 = 0.0, n2 = 0.0;
    double d0 = 1.0, d1 = 0.0, d2 = 0.0;
    bool allpass = false;

    bool firstOrder() const noexcept { return d2 == 0.0; }
    double magSq (double f) const noexcept;
};

/** The analog prototype of a band: the single source of truth for both the drawn curve
    and the digital design (docs/spec/03-dsp.md sections 1 and 2). */
struct Prototype
{
    std::array<AnalogSection, kMaxSections> sections {};
    int count = 0;

    void add (const AnalogSection& s) noexcept
    {
        if (count < kMaxSections)
            sections[static_cast<size_t> (count++)] = s;
    }

    double magnitudeDb (double f) const noexcept;
};

/** Builds the prototype for a band. Uses p.gainDb as the effective gain. Ignores used/on. */
Prototype makePrototype (const BandParams& p) noexcept;

/** Convenience: prototype magnitude of an audible band in dB, 0 for an inaudible one. */
double bandMagnitudeDb (const BandParams& p, double f) noexcept;
} // namespace of
