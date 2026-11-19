#pragma once

#include "Prototype.h"

namespace of
{
/** Digital section (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Squared magnitude at w radians per sample. */
    double magSq (double w) const noexcept;
};

/** Trapezoidal state-variable realisation of a biquad (docs/spec/03-dsp.md section 4).
    Any g > 0, k > 0 is stable, which makes these safe to interpolate. */
struct SvfCoeffs
{
    double g = 1.0, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
};

/** Matched design of one analog section: impulse-invariant poles, numerator fitted to the
    analog magnitude (docs/spec/03-dsp.md section 3). */
Biquad designSection (const AnalogSection& s, double sampleRate) noexcept;

SvfCoeffs toSvf (const Biquad& b) noexcept;

struct DigitalDesign
{
    std::array<Biquad, kMaxSections> biquads {};
    int count = 0;

    double magnitudeDb (double freqHz, double sampleRate) const noexcept;
};

DigitalDesign designBand (const Prototype& proto, double sampleRate) noexcept;

/** Designs a whole band. For gain-type shapes one gain direction is designed directly and
    the other is its exact inverse, so a boost followed by the same cut is flat by
    construction. */
DigitalDesign designBand (const BandParams& band, double sampleRate) noexcept;
} // namespace of
