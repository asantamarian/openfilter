#include "Prototype.h"
#include "EllipticTable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace of
{
namespace
{
using cplx = std::complex<double>;
constexpr double pi = std::numbers::pi;

struct Quad { double c2, c1, c0; };   // c2 s^2 + c1 s + c0, normalised frequency

/** Upper-half-plane Butterworth poles of order n on the unit circle. Returns the number of
    complex pairs; a real pole at -1 exists additionally when n is odd. */
int butterworthPairs (int n, std::array<cplx, 8>& out) noexcept
{
    const int pairs = n / 2;
    for (int k = 1; k <= pairs; ++k)
    {
        const double theta = pi * (2.0 * k - 1.0) / (2.0 * n);
        out[static_cast<size_t> (k - 1)] = { -std::sin (theta), std::cos (theta) };
    }
    return pairs;
}

/** Changes the Q of unit-circle poles by a common factor, keeping their radius. */
void scaleQ (std::array<cplx, 8>& poles, int pairs, double factor) noexcept
{
    for (int i = 0; i < pairs; ++i)
    {
        auto& p = poles[static_cast<size_t> (i)];
        const double q = std::clamp (factor / (-2.0 * p.real()), 0.05, 100.0);
        const double re = -1.0 / (2.0 * q);
        p = re > -1.0 ? cplx { re, std::sqrt (1.0 - re * re) } : cplx { re, 0.0 };
    }
}

/** Low-pass to band-pass root mapping s -> Q (s + 1/s). A low-pass root p yields the
    band-pass quadratics returned here (one for a real root, two for a complex root that
    stands for a conjugate pair). */
int bandQuads (cplx p, bool isReal, double q, Quad out[2]) noexcept
{
    if (isReal)
    {
        out[0] = { 1.0, -p.real() / q, 1.0 };
        return 1;
    }
    const cplx h = p / (2.0 * q);
    const cplx root = std::sqrt (h * h - 1.0);
    const cplx s1 = h + root, s2 = h - root;
    out[0] = { 1.0, -2.0 * s1.real(), std::norm (s1) };
    out[1] = { 1.0, -2.0 * s2.real(), std::norm (s2) };
    return 2;
}

AnalogSection section (Quad num, Quad den, double fc) noexcept
{
    AnalogSection s;
    s.n2 = num.c2;  s.n1 = num.c1 * fc;  s.n0 = num.c0 * fc * fc;
    s.d2 = den.c2;  s.d1 = den.c1 * fc;  s.d0 = den.c0 * fc * fc;
    return s;
}

AnalogSection firstOrder (double n1, double n0, double d1, double d0, double fc) noexcept
{
    AnalogSection s;
    s.n2 = 0.0;  s.n1 = n1;  s.n0 = n0 * fc;
    s.d2 = 0.0;  s.d1 = d1;  s.d0 = d0 * fc;
    return s;
}

void scaleNumerator (AnalogSection& s, double g) noexcept
{
    s.n0 *= g;  s.n1 *= g;  s.n2 *= g;
}

void addCut (Prototype& proto, bool lowCut, int order, double fc, double q) noexcept
{
    std::array<cplx, 8> poles {};
    const int pairs = butterworthPairs (order, poles);
    if (pairs > 0)
        scaleQ (poles, pairs, std::pow (q / std::numbers::sqrt2 * 2.0, 1.0 / pairs));   // q / 0.7071

    for (int i = 0; i < pairs; ++i)
    {
        const auto p = poles[static_cast<size_t> (i)];
        const Quad den { 1.0, -2.0 * p.real(), std::norm (p) };
        proto.add (section (lowCut ? Quad { 1.0, 0.0, 0.0 } : Quad { 0.0, 0.0, 1.0 }, den, fc));
    }
    if (order % 2 == 1)
        proto.add (lowCut ? firstOrder (1.0, 0.0, 1.0, 1.0, fc) : firstOrder (0.0, 1.0, 1.0, 1.0, fc));
}

void addBrickwall (Prototype& proto, bool lowCut, double fc) noexcept
{
    double dcGain = kEllipticGain;
    const int first = proto.count;
    for (int i = 0; i < 6; ++i)
    {
        // Pair the highest-Q pole with the zero nearest the pass band.
        const double wz = kEllipticZeroFreq[i];
        const cplx p { kEllipticPoleRe[5 - i], kEllipticPoleIm[5 - i] };
        const double wp2 = std::norm (p);
        dcGain *= wz * wz / wp2;
        if (! lowCut)   // high cut: the low-pass prototype itself, each section with unit DC gain
            proto.add (section ({ wp2 / (wz * wz), 0.0, wp2 }, { 1.0, -2.0 * p.real(), wp2 }, fc));
        else            // low cut: s -> 1/s, each section with unit gain at infinity
            proto.add (section ({ 1.0, 0.0, 1.0 / (wz * wz) }, { 1.0, -2.0 * p.real() / wp2, 1.0 / wp2 }, fc));
    }
    scaleNumerator (proto.sections[static_cast<size_t> (first)], dcGain);
}

/** Symmetric Butterworth shelf of order n with total linear gain g. High shelf: unity at DC,
    g at infinity. Low shelf is its mirror. */
void addShelf (Prototype& proto, bool high, int order, double fc, double g, double q) noexcept
{
    std::array<cplx, 8> poles {};
    const int pairs = butterworthPairs (order, poles);
    if (pairs > 0)
        scaleQ (poles, pairs, std::pow (q, 1.0 / pairs));

    const double r = std::pow (g, 1.0 / (2.0 * order));
    for (int i = 0; i < pairs; ++i)
    {
        const double iq = -2.0 * poles[static_cast<size_t> (i)].real();   // 1/Q of the pair
        if (high)
            proto.add (section ({ r * r, r * iq, 1.0 }, { 1.0 / (r * r), iq / r, 1.0 }, fc));
        else
            proto.add (section ({ 1.0, r * iq, r * r }, { 1.0, iq / r, 1.0 / (r * r) }, fc));
    }
    if (order % 2 == 1)
        proto.add (high ? firstOrder (r, 1.0, 1.0 / r, 1.0, fc) : firstOrder (1.0, r, 1.0, 1.0 / r, fc));
}

void addBell (Prototype& proto, int order, double fc, double g, double q) noexcept
{
    std::array<cplx, 8> poles {};
    const int pairs = butterworthPairs (order, poles);
    const double r = std::pow (g, 1.0 / (2.0 * order));

    // Low shelf in the low-pass domain: zeros at r * B, poles at B / r. Map both to band-pass.
    for (int i = 0; i < pairs; ++i)
    {
        Quad num[2], den[2];
        const auto p = poles[static_cast<size_t> (i)];
        bandQuads (p * r, false, q, num);
        bandQuads (p / r, false, q, den);
        proto.add (section (num[0], den[0], fc));
        proto.add (section (num[1], den[1], fc));
    }
    if (order % 2 == 1)
    {
        Quad num[2], den[2];
        bandQuads ({ -r, 0.0 }, true, q, num);
        bandQuads ({ -1.0 / r, 0.0 }, true, q, den);
        proto.add (section (num[0], den[0], fc));
    }
}

void addBandFilter (Prototype& proto, bool notch, int order, double fc, double q) noexcept
{
    std::array<cplx, 8> poles {};
    const int pairs = butterworthPairs (order, poles);
    const Quad num = notch ? Quad { 1.0, 0.0, 1.0 } : Quad { 0.0, 1.0 / q, 0.0 };

    for (int i = 0; i < pairs; ++i)
    {
        Quad den[2];
        bandQuads (poles[static_cast<size_t> (i)], false, q, den);
        proto.add (section (num, den[0], fc));
        proto.add (section (num, den[1], fc));
    }
    if (order % 2 == 1)
    {
        Quad den[2];
        bandQuads ({ -1.0, 0.0 }, true, q, den);
        proto.add (section (num, den[0], fc));
    }
}
} // namespace

double AnalogSection::magSq (double f) const noexcept
{
    if (allpass)
        return 1.0;
    const double f2 = f * f;
    const double nr = n0 - n2 * f2, ni = n1 * f;
    const double dr = d0 - d2 * f2, di = d1 * f;
    const double den = dr * dr + di * di;
    return den > 0.0 ? (nr * nr + ni * ni) / den : 1.0;
}

double Prototype::magnitudeDb (double f) const noexcept
{
    double m = 1.0;
    for (int i = 0; i < count; ++i)
        m *= sections[static_cast<size_t> (i)].magSq (f);
    return 10.0 * std::log10 (std::max (m, 1.0e-30));
}

Prototype makePrototype (const BandParams& p) noexcept
{
    Prototype proto;
    const double fc = std::clamp (p.freq, 1.0, 1.0e6);
    const double q = std::clamp (p.q, 0.025, 40.0);
    const double g = std::pow (10.0, std::clamp (p.gainDb, -60.0, 60.0) / 20.0);
    const int sdb = slopeDb (p.slope);
    const int cutOrder = sdb / 6;
    const int bandOrder = std::max (1, (sdb + 6) / 12);   // 6,12 -> 1; 18,24 -> 2; 30,36 -> 3; ...

    switch (p.shape)
    {
        case Shape::LowCut:
        case Shape::HighCut:
            if (p.slope == Slope::Brickwall)
                addBrickwall (proto, p.shape == Shape::LowCut, fc);
            else
                addCut (proto, p.shape == Shape::LowCut, cutOrder, fc, q);
            break;

        case Shape::LowShelf:   addShelf (proto, false, cutOrder, fc, g, q); break;
        case Shape::HighShelf:  addShelf (proto, true, cutOrder, fc, g, q); break;

        case Shape::TiltShelf:
            addShelf (proto, true, cutOrder, fc, g, q);
            scaleNumerator (proto.sections[0], 1.0 / std::sqrt (g));
            break;

        case Shape::Bell:       addBell (proto, bandOrder, fc, g, q); break;
        case Shape::Notch:      addBandFilter (proto, true, bandOrder, fc, q); break;
        case Shape::BandPass:   addBandFilter (proto, false, bandOrder, fc, q); break;

        case Shape::AllPass:
        {
            AnalogSection s;
            if (p.slope == Slope::S6)
                s = firstOrder (-1.0, 1.0, 1.0, 1.0, fc);
            else
                s = section ({ 1.0, -1.0 / q, 1.0 }, { 1.0, 1.0 / q, 1.0 }, fc);
            s.allpass = true;
            proto.add (s);
            break;
        }

        case Shape::FlatTilt:
        {
            // Target dB(f) = (G/6) * log2 (f / f0): first-order shelves an octave apart,
            // extending an octave and a half past each end of the audio band.
            const double r = std::pow (g, 1.0 / 12.0);   // sqrt of the per-octave linear gain
            for (int k = 0; k <= 13; ++k)
                proto.add (firstOrder (r, 1.0, 1.0 / r, 1.0, 5.0 * std::pow (2.0, k)));
            scaleNumerator (proto.sections[0], std::pow (10.0, -proto.magnitudeDb (fc) / 20.0));
            break;
        }
    }
    return proto;
}

double bandMagnitudeDb (const BandParams& p, double f) noexcept
{
    return p.audible() ? makePrototype (p).magnitudeDb (f) : 0.0;
}
} // namespace of
