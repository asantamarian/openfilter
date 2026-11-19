#pragma once

#include <array>
#include <cstddef>

using std::size_t;

namespace of
{
inline constexpr int kMaxBands = 32;
inline constexpr int kMaxSections = 16;

// Order matches the choice parameters in docs/spec/02-parameters.md. Never reorder.
enum class Shape { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, FlatTilt, AllPass };
enum class Slope { S6, S12, S18, S24, S30, S36, S48, S72, S96, Brickwall };
enum class Placement { Stereo, Left, Right, Mid, Side };

inline constexpr int kNumShapes = 10;
inline constexpr int kNumSlopes = 10;

/** Slope in dB per octave. Brickwall reports 96 for shapes that have no brickwall form. */
constexpr int slopeDb (Slope s) noexcept
{
    constexpr int table[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96, 96 };
    return table[static_cast<int> (s)];
}

constexpr bool hasGain (Shape s) noexcept
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf
        || s == Shape::TiltShelf || s == Shape::FlatTilt;
}

constexpr bool isCut (Shape s) noexcept { return s == Shape::LowCut || s == Shape::HighCut; }

constexpr bool canBeDynamic (Shape s) noexcept
{
    return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf || s == Shape::TiltShelf;
}

/** Dynamics settings of a band (docs/spec/02-parameters.md). */
struct DynParams
{
    bool on = false;
    double rangeDb = 0.0;        // gain added at full detection; negative compresses the band
    double thresholdDb = -30.0;  // dBFS, or an offset around the program level when auto
    bool autoThreshold = true;
    double ratio = 3.0;
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 120.0;
    bool below = false;          // act when the level is under the threshold
    bool external = false;       // listen to the sidechain input instead of the plugin input
    bool freeFilter = false;     // detector listens at its own frequency and Q, not the band's
    double freeFreq = 1000.0, freeQ = 1.0;
    bool spectral = false;       // work per frequency bin instead of on the whole band
    bool resonance = true;       // spectral: judge each bin against its surroundings, not a level
    double resolutionOct = 1.0 / 6.0;   // spectral: smoothing of the per-bin gains across frequency

    bool operator== (const DynParams&) const = default;
};

/** Plain band settings. gainDb is the effective gain, after gain scale. */
struct BandParams
{
    bool used = false;
    bool on = true;
    Shape shape = Shape::Bell;
    Slope slope = Slope::S12;
    Placement place = Placement::Stereo;
    double freq = 1000.0;
    double gainDb = 0.0;
    double q = 1.0;
    DynParams dyn {};
    bool linear = false;   // in Mixed phase mode: realise this band in the linear-phase path

    bool operator== (const BandParams&) const = default;

    /** True when band dynamics (one gain for the whole band) are on and can act. */
    bool dynamic() const noexcept
    {
        return used && on && dyn.on && ! dyn.spectral && canBeDynamic (shape) && (dyn.rangeDb > 1.0e-6 || dyn.rangeDb < -1.0e-6);
    }

    /** True when spectral dynamics (a gain per frequency bin) are on and can act. */
    bool spectral() const noexcept
    {
        return used && on && dyn.on && dyn.spectral && canBeDynamic (shape) && (dyn.rangeDb > 1.0e-6 || dyn.rangeDb < -1.0e-6);
    }

    /** True when the band changes the signal at all. */
    bool audible() const noexcept
    {
        if (! used || ! on)
            return false;
        if (hasGain (shape))
            return gainDb > 1.0e-6 || gainDb < -1.0e-6 || dynamic();
        return true;
    }
};

struct ParamSnapshot
{
    std::array<BandParams, kMaxBands> bands {};   // gainDb here is the raw parameter value
    int phaseMode = 0;       // 0 zero latency, 1 natural, 2 linear, 3 mixed
    int linQuality = 1;      // 0 low .. 4 max: length of the linear-phase kernel
    int autoGain = 0;        // 0 off, 1 from the curve, 2 loudness matched
    int lookahead = 0;       // 0 off, 1..3: 2, 5, 10 ms. Delays the audio, not the detectors
    bool detectorListen = false;   // solo plays the band's detector signal instead of its region

    /** True when any band slot is set to Spectral: the spectral stage and its latency are in. */
    bool wantsSpectral() const noexcept
    {
        for (const auto& b : bands)
            if (b.used && b.dyn.on && b.dyn.spectral && canBeDynamic (b.shape))
                return true;
        return false;
    }
    double gainScale = 1.0;
    double outGainDb = 0.0;
    double pan = 0.0;        // -1..1
    bool invert = false;
    bool bypass = false;
    bool delta = false;
};
} // namespace of
