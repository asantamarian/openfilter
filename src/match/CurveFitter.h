#pragma once

#include "dsp/Types.h"

#include <array>
#include <vector>

namespace of
{
/** Turns a target response into a few EQ bands (docs/spec/04-analysis.md section 3.2).
    Used by EQ match and by curve sketching. Pure functions; run them off the audio thread. */
namespace fit
{
inline constexpr int kGridSize = 256;

/** The frequency of grid point i: log spaced from 20 Hz to 20 kHz. */
double gridFrequency (int i) noexcept;

using Curve = std::array<double, kGridSize>;   // dB at each grid point

/** Response in dB of a set of bands on the grid. */
Curve evaluate (const std::vector<BandParams>& bands);

/** Fits at most maxBands bands (shelves at the ends where the target calls for them, bells
    elsewhere) to the target. */
std::vector<BandParams> fitCurve (const Curve& target, int maxBands);

/** Root-mean-square difference between two curves, in dB. */
double rmsError (const Curve& a, const Curve& b) noexcept;

/** Averages a curve over a window of the given width in octaves. */
Curve smooth (const Curve& curve, double octaves);

/** The EQ that takes a source spectrum to a reference spectrum (both in dB on the grid):
    smoothed over a third of an octave, limited to +-18 dB, with the level difference between
    200 Hz and 5 kHz removed so the match changes tone, not loudness. */
Curve matchTarget (const Curve& source, const Curve& reference);
} // namespace fit
} // namespace of
