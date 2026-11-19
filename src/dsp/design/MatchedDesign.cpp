#include "MatchedDesign.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace of
{
namespace
{
constexpr double pi = std::numbers::pi;

struct Basis { double p0, p1, p2; };

Basis basisAt (double w) noexcept
{
    const double s = std::sin (0.5 * w);
    const double p1 = s * s, p0 = 1.0 - p1;
    return { p0, p1, 4.0 * p0 * p1 };
}
} // namespace

double Biquad::magSq (double w) const noexcept
{
    const auto b = basisAt (w);
    const double n0 = (b0 + b1 + b2) * (b0 + b1 + b2), n1 = (b0 - b1 + b2) * (b0 - b1 + b2), n2 = -4.0 * b0 * b2;
    const double d0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), d1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), d2 = -4.0 * a2;
    const double den = d0 * b.p0 + d1 * b.p1 + d2 * b.p2;
    return den > 0.0 ? std::max (0.0, n0 * b.p0 + n1 * b.p1 + n2 * b.p2) / den : 1.0;
}

namespace
{
/** Impulse-invariant mapping of the roots of x^2 + (w/q) x + w^2 (w in radians per sample)
    to the polynomial 1 + c1 z^-1 + c2 z^-2. */
void mapRoots (double w, double q, double& c1, double& c2) noexcept
{
    w = std::min (w, 0.98 * pi);
    const double disc = 1.0 - 1.0 / (4.0 * q * q);
    const double e = std::exp (-w / (2.0 * q));
    c2 = e * e;
    c1 = disc > 0.0 ? -2.0 * e * std::cos (w * std::sqrt (disc))
                    : -2.0 * e * std::cosh (w * std::sqrt (-disc));
}

/** Scales the numerator shape (1, c1, c2) so that it fits the analog magnitude best, in the
    relative least-squares sense, on a log-spaced grid up to Nyquist. */
void fitGain (Biquad& out, const AnalogSection& s, double fs, double c1, double c2) noexcept
{
    const double nyq = 0.5 * fs;
    const double A0 = (1.0 + out.a1 + out.a2) * (1.0 + out.a1 + out.a2);
    const double A1 = (1.0 - out.a1 + out.a2) * (1.0 - out.a1 + out.a2);
    const double A2 = -4.0 * out.a2;
    const double C0 = (1.0 + c1 + c2) * (1.0 + c1 + c2), C1 = (1.0 - c1 + c2) * (1.0 - c1 + c2), C2 = -4.0 * c2;

    constexpr int points = 24;
    const double fLo = std::min (10.0, 0.01 * nyq), fHi = 0.95 * nyq;
    const double ratio = std::pow (fHi / fLo, 1.0 / (points - 1));
    double k1 = 0.0, k2 = 0.0, f = fLo;
    for (int i = 0; i < points; ++i, f *= ratio)
    {
        const double target = s.magSq (f);
        if (target < 1.0e-6)
            continue;
        const auto bs = basisAt (2.0 * pi * f / fs);
        const double r = std::max (0.0, C0 * bs.p0 + C1 * bs.p1 + C2 * bs.p2)
                       / (target * (A0 * bs.p0 + A1 * bs.p1 + A2 * bs.p2));
        k1 += r;
        k2 += r * r;
    }
    const double gain = k2 > 0.0 ? std::sqrt (k1 / k2) : 1.0;
    out.b0 = gain;  out.b1 = gain * c1;  out.b2 = gain * c2;
}
} // namespace

Biquad designSection (const AnalogSection& s, double fs) noexcept
{
    Biquad out;
    const double nyq = 0.5 * fs;
    const double t0 = s.magSq (0.0);
    const double t1 = s.magSq (nyq);

    if (s.firstOrder())
    {
        if (s.d1 == 0.0)   // pure gain
        {
            out.b0 = s.d0 != 0.0 ? s.n0 / s.d0 : 1.0;
            return out;
        }
        const double w = 2.0 * pi * (s.d0 / s.d1) / fs;
        out.a1 = -std::exp (-w);
        if (s.allpass)
        {
            out.b0 = out.a1;
            out.b1 = 1.0;
            return out;
        }
        // Exact match at DC and Nyquist.
        const double r0 = std::sqrt (t0) * (1.0 + out.a1);
        const double r1 = std::sqrt (t1) * (1.0 - out.a1);
        out.b0 = 0.5 * (r0 + r1);
        out.b1 = 0.5 * (r0 - r1);
        return out;
    }

    // Poles: impulse invariance.
    const double fp = std::sqrt (s.d0 / s.d2);
    mapRoots (2.0 * pi * fp / fs, std::sqrt (s.d0 * s.d2) / s.d1, out.a1, out.a2);

    if (s.allpass)
    {
        out.b0 = out.a2;  out.b1 = out.a1;  out.b2 = 1.0;
        return out;
    }

    const double A0 = (1.0 + out.a1 + out.a2) * (1.0 + out.a1 + out.a2);
    const double A1 = (1.0 - out.a1 + out.a2) * (1.0 - out.a1 + out.a2);
    const double A2 = -4.0 * out.a2;
    const bool twoZeros = s.n2 > 0.0 && s.n0 > 0.0;
    const double fz = twoZeros ? std::sqrt (s.n0 / s.n2) : fp;

    // Zeros on the imaginary axis (notches, elliptic sections): keep the null exact.
    if (twoZeros && s.n1 == 0.0 && 2.0 * pi * fz / fs < 0.999 * pi)
    {
        const double c = std::cos (2.0 * pi * fz / fs);
        const double gain = t0 >= t1 ? std::sqrt (t0 * A0) / (2.0 - 2.0 * c)
                                     : std::sqrt (t1 * A1) / (2.0 + 2.0 * c);
        out.b0 = gain;  out.b1 = -2.0 * c * gain;  out.b2 = gain;
        return out;
    }

    // Low-cut sections (numerator s^2): both zeros exactly at DC so the stop band keeps its
    // slope; one gain to fit.
    if (s.n0 == 0.0 && s.n1 == 0.0)
    {
        fitGain (out, s, fs, -2.0, 1.0);
        return out;
    }

    // General case: exact match at DC, Nyquist and the section's own feature frequency
    // (after Vicanek, "Matched Second Order Digital Filters").
    const double fc = std::min (fz, 0.45 * fs);
    const auto bc = basisAt (2.0 * pi * fc / fs);
    const double B0 = A0 * t0, B1 = A1 * t1;
    const double B2 = (s.magSq (fc) * (A0 * bc.p0 + A1 * bc.p1 + A2 * bc.p2) - B0 * bc.p0 - B1 * bc.p1) / bc.p2;
    const double q0 = std::sqrt (B0), q1 = std::sqrt (B1);
    const double W = 0.5 * (q0 + q1);

    if (W * W + B2 >= 0.0)
    {
        out.b1 = 0.5 * (q0 - q1);
        out.b0 = 0.5 * (W + std::sqrt (W * W + B2));
        out.b2 = out.b0 > 0.0 ? -B2 / (4.0 * out.b0) : 0.0;
        return out;
    }

    // Those three targets cannot be met by a real numerator. Fall back to mapping the zeros
    // the same way as the poles, and fit the gain.
    double c1 = 0.0, c2 = 0.0;
    if (twoZeros)
        mapRoots (2.0 * pi * fz / fs, std::sqrt (s.n0 * s.n2) / std::abs (s.n1), c1, c2);
    else if (s.n1 != 0.0 && s.n2 == 0.0)
        c1 = -std::exp (-2.0 * pi * std::abs (s.n0 / s.n1) / fs);
    fitGain (out, s, fs, c1, c2);
    return out;
}

SvfCoeffs toSvf (const Biquad& b) noexcept
{
    SvfCoeffs c;
    const double lo = std::max (1.0 + b.a1 + b.a2, 1.0e-18);
    const double hi = std::max (1.0 - b.a1 + b.a2, 1.0e-18);
    c.g = std::sqrt (lo / hi);
    const double D = 4.0 / hi;
    c.k = std::max ((1.0 - b.a2) * D / (2.0 * c.g), 1.0e-9);
    const double cL = 0.25 * (b.b0 + b.b1 + b.b2);
    const double cH = 0.25 * (b.b0 - b.b1 + b.b2);
    const double cB = 0.5 * (b.b0 - b.b2);
    c.m0 = cH * D;
    c.m1 = cB * D / c.g - c.k * c.m0;
    c.m2 = cL * D / (c.g * c.g) - c.m0;
    return c;
}

double DigitalDesign::magnitudeDb (double freqHz, double sampleRate) const noexcept
{
    const double w = 2.0 * pi * freqHz / sampleRate;
    double m = 1.0;
    for (int i = 0; i < count; ++i)
        m *= biquads[static_cast<size_t> (i)].magSq (w);
    return 10.0 * std::log10 (std::max (m, 1.0e-30));
}

DigitalDesign designBand (const Prototype& proto, double sampleRate) noexcept
{
    DigitalDesign d;
    d.count = proto.count;
    for (int i = 0; i < proto.count; ++i)
        d.biquads[static_cast<size_t> (i)] = designSection (proto.sections[static_cast<size_t> (i)], sampleRate);
    return d;
}
} // namespace of
