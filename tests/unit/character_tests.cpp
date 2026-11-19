#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace of;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

/** Runs a sine through the engine and returns the output's second half. */
std::vector<double> through (const ParamSnapshot& p, double freq, double levelDb, int* latency = nullptr)
{
    Engine e;
    e.prepare (fs, 512, 2);
    const double amp = std::pow (10.0, levelDb / 20.0);
    std::vector<double> l (static_cast<size_t> (fs)), r;
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = amp * std::sin (2.0 * std::numbers::pi * freq * static_cast<double> (i) / fs);
    r = l;
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        double* io[2] = { l.data() + pos, r.data() + pos };
        e.process (io, 2, static_cast<int> (std::min<size_t> (512, l.size() - pos)), p);
    }
    if (latency != nullptr)
        *latency = e.latencySamples();
    return { l.begin() + static_cast<std::ptrdiff_t> (l.size() / 2), l.end() };
}

/** Level in dBFS of one frequency in a signal. */
double toneDb (const std::vector<double>& x, double freq)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double ph = 2.0 * std::numbers::pi * freq * static_cast<double> (i) / fs;
        re += x[i] * std::cos (ph);
        im += x[i] * std::sin (ph);
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / static_cast<double> (x.size()) + 1.0e-20);
}
} // namespace

TEST_CASE ("character: unity for small signals, harmonics for large ones", "[character]")
{
    for (int mode : { 1, 2 })
    {
        ParamSnapshot p;
        p.character = mode;
        p.drive = 0.5;
        int latency = 0;

        // Quiet: passes at its own level, with no audible harmonics.
        const auto quiet = through (p, 1000.0, -60.0, &latency);
        CHECK (latency == 76);
        CHECK (latency == Engine::latencyFor (0, fs, 1, false, 0, mode));
        CHECK_THAT (toneDb (quiet, 1000.0), WithinAbs (-60.0, 0.1));

        // Loud: the fundamental is compressed and harmonics appear.
        const auto loud = through (p, 1000.0, -6.0);
        const double h1 = toneDb (loud, 1000.0), h2 = toneDb (loud, 2000.0), h3 = toneDb (loud, 3000.0);
        std::printf ("  %s at -6 dBFS, drive 50 %%: fundamental %.1f dB, 2nd %.1f dB, 3rd %.1f dB\n", mode == 1 ? "Tube" : "Iron", h1, h2, h3);
        CHECK (h1 < -6.5);
        CHECK (h3 > -50.0);
        if (mode == 1)
            CHECK (h2 > -50.0);    // Tube is asymmetric: even harmonics too
        else
            CHECK (h2 < -80.0);    // Iron is symmetric: odd harmonics only
    }
}

TEST_CASE ("character: Iron saturates the lows harder; drive scales the effect", "[character]")
{
    ParamSnapshot p;
    p.character = 2;
    p.drive = 0.5;
    const double lowThird = toneDb (through (p, 60.0, -12.0), 180.0) - toneDb (through (p, 60.0, -12.0), 60.0);
    const double highThird = toneDb (through (p, 2000.0, -12.0), 6000.0) - toneDb (through (p, 2000.0, -12.0), 2000.0);
    INFO ("third harmonic relative to the fundamental: " << lowThird << " dB at 60 Hz, " << highThird << " dB at 2 kHz");
    CHECK (lowThird > highThird + 6.0);

    // More drive, more third harmonic; at zero drive there is still a gentle curve.
    p.character = 1;
    p.drive = 0.1;
    const double gentle = toneDb (through (p, 1000.0, -12.0), 3000.0);
    p.drive = 1.0;
    const double hard = toneDb (through (p, 1000.0, -12.0), 3000.0);
    CHECK (hard > gentle + 10.0);
}

TEST_CASE ("character: harmonics above Nyquist do not fold back into the audio band", "[character]")
{
    // A loud 9 kHz tone: its 3rd harmonic (27 kHz) would alias to 21 kHz and its 5th (45 kHz)
    // to 3 kHz if the stage ran at the host rate.
    for (int mode : { 1, 2 })
    {
        ParamSnapshot p;
        p.character = mode;
        p.drive = 0.7;
        const auto out = through (p, 9000.0, -6.0);
        const double fundamental = toneDb (out, 9000.0);
        const double alias5 = toneDb (out, 3000.0), alias7 = toneDb (out, 15000.0);   // 45 kHz and 63 kHz folded
        std::printf ("  %s: 9 kHz at %.1f dB; aliases at 3 kHz %.1f dB, at 15 kHz %.1f dB\n", mode == 1 ? "Tube" : "Iron", fundamental, alias5, alias7);
        CHECK (alias5 < fundamental - 70.0);
        CHECK (alias7 < fundamental - 70.0);
    }
}

TEST_CASE ("character: clean is free and exact", "[character]")
{
    ParamSnapshot p;   // Clean
    int latency = -1;
    const auto out = through (p, 1000.0, -6.0, &latency);
    CHECK (latency == 0);
    CHECK_THAT (toneDb (out, 1000.0), WithinAbs (-6.0, 1.0e-6));
    CHECK (toneDb (out, 3000.0) < -200.0);
}
