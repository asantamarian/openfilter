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
/** Runs a sine of the given level through the engine and returns the dynamic gain of band 0
    after `seconds`, plus the measured output gain in dB over the last quarter. */
struct Result { double dynDb, gainDb; };

Result run (const ParamSnapshot& p, double levelDb, double freq, double seconds, double fs = 48000.0)
{
    Engine e;
    e.prepare (fs, 512, 2);
    const double amp = std::pow (10.0, levelDb / 20.0);
    const int total = static_cast<int> (seconds * fs);
    double in = 0.0, out = 0.0;
    std::vector<double> l (512), r (512);
    for (int pos = 0; pos < total; pos += 512)
    {
        for (int i = 0; i < 512; ++i)
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = amp * std::sin (2.0 * std::numbers::pi * freq * (pos + i) / fs);
        const auto dry = l;
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 512, p);
        if (pos > total * 3 / 4)
            for (int i = 0; i < 512; ++i)
            {
                in += dry[static_cast<size_t> (i)] * dry[static_cast<size_t> (i)];
                out += l[static_cast<size_t> (i)] * l[static_cast<size_t> (i)];
            }
    }
    return { static_cast<double> (e.dynamicGainDb (0)), 10.0 * std::log10 (out / in) };
}

ParamSnapshot bellWith (DynParams d, double staticGain = 0.0)
{
    ParamSnapshot p;
    p.bands[0] = { true, true, Shape::Bell, Slope::S12, Placement::Stereo, 1000.0, staticGain, 2.0 };
    p.bands[0].dyn = d;
    return p;
}
} // namespace

TEST_CASE ("dynamics: gain computer follows threshold, ratio and range", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = false;
    d.thresholdDb = -30.0;
    d.ratio = 2.0;
    d.kneeDb = 0.0;
    d.attackMs = 5.0;
    d.releaseMs = 50.0;
    d.rangeDb = -12.0;

    // A sine's power average is 3 dB under its peak, and the detector band-pass is 0 dB at
    // the band centre: a -20 dBFS peak sine reads -23 dB, 7 dB over a -30 dB threshold.
    // Ratio 2 turns that into 3.5 dB of reduction.
    const auto a = run (bellWith (d), -20.0, 1000.0, 1.0);
    CHECK_THAT (a.dynDb, WithinAbs (-3.5, 0.15));
    CHECK_THAT (a.gainDb, WithinAbs (-3.5, 0.2));   // the band really is cut by that much

    // Far over the threshold the range caps the reduction.
    const auto b = run (bellWith (d), 0.0, 1000.0, 1.0);
    CHECK_THAT (b.dynDb, WithinAbs (-12.0, 0.01));
    CHECK_THAT (b.gainDb, WithinAbs (-12.0, 0.2));

    // Under the threshold nothing happens, and the band stays bit-transparent-quiet.
    const auto c = run (bellWith (d), -50.0, 1000.0, 1.0);
    CHECK (c.dynDb == 0.0);
    CHECK_THAT (c.gainDb, WithinAbs (0.0, 0.01));

    // A positive range expands upward; the static gain is added on top.
    d.rangeDb = 6.0;
    const auto up = run (bellWith (d, 2.0), 0.0, 1000.0, 1.0);
    CHECK_THAT (up.dynDb, WithinAbs (6.0, 0.01));
    CHECK_THAT (up.gainDb, WithinAbs (8.0, 0.2));

    // Trigger below: acts when the level is under the threshold instead.
    d.rangeDb = -6.0;
    d.below = true;
    CHECK_THAT (run (bellWith (d), -60.0, 1000.0, 1.0).dynDb, WithinAbs (-6.0, 0.01));
    CHECK (run (bellWith (d), -10.0, 1000.0, 1.0).dynDb == 0.0);
}

TEST_CASE ("dynamics: the detector listens around the band, not everywhere", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = false;
    d.thresholdDb = -30.0;
    d.ratio = 100.0;
    d.kneeDb = 0.0;
    d.rangeDb = -12.0;

    // A loud tone four octaves below a narrow 1 kHz bell must not trigger it.
    auto p = bellWith (d);
    p.bands[0].q = 8.0;
    CHECK (std::abs (run (p, -6.0, 62.5, 1.0).dynDb) < 0.5);
    CHECK_THAT (run (p, -6.0, 1000.0, 1.0).dynDb, WithinAbs (-12.0, 0.01));
}

TEST_CASE ("dynamics: attack and release times", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = false;
    d.thresholdDb = -40.0;
    d.ratio = 100.0;
    d.kneeDb = 0.0;
    d.rangeDb = -10.0;
    d.attackMs = 50.0;
    d.releaseMs = 200.0;
    const auto p = bellWith (d);

    // After one attack time the reduction is about 63 % of the way there (plus the 5 ms the
    // level detector needs), after five it has arrived.
    const double early = run (p, 0.0, 1000.0, 0.060).dynDb;
    CHECK (early < -4.5);
    CHECK (early > -8.0);
    CHECK_THAT (run (p, 0.0, 1000.0, 0.5).dynDb, WithinAbs (-10.0, 0.1));

    // Release: loud for 0.5 s, then silence; the reduction must let go at the release rate.
    Engine e;
    e.prepare (48000.0, 512, 2);
    std::vector<double> l (512), r (512);
    double afterRelease = 0.0;
    for (int block = 0; block < 94; ++block)   // 1 s
    {
        const bool loud = block < 47;
        for (int i = 0; i < 512; ++i)
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = loud ? std::sin (2.0 * std::numbers::pi * 1000.0 * (block * 512 + i) / 48000.0) : 0.0;
        double* io[2] = { l.data(), r.data() };
        e.process (io, 2, 512, p);
        if (block == 47 + 19)   // about 200 ms into the silence
            afterRelease = e.dynamicGainDb (0);
    }
    CHECK (afterRelease < -2.5);
    CHECK (afterRelease > -5.0);   // 37 % of -10 dB is -3.7
    // Half a second is two and a half release times: -10 dB * e^-2.5 = -0.8 dB should remain.
    CHECK (std::abs (e.dynamicGainDb (0)) < 1.3);
    CHECK (std::abs (e.dynamicGainDb (0)) > 0.5);
}

TEST_CASE ("dynamics: auto threshold makes the band independent of input level", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = true;
    d.thresholdDb = -54.0;   // read as an offset: -6 dB under the program level
    d.ratio = 4.0;
    d.kneeDb = 0.0;
    d.rangeDb = -12.0;
    const auto p = bellWith (d);

    const double loud = run (p, -6.0, 1000.0, 12.0).dynDb;
    const double quiet = run (p, -24.0, 1000.0, 12.0).dynDb;
    INFO ("loud " << loud << " dB, quiet " << quiet << " dB");
    CHECK (loud < -2.0);                      // it does act
    CHECK_THAT (loud - quiet, WithinAbs (0.0, 0.5));
}

TEST_CASE ("dynamics: gain scale also scales the range; shapes without dynamics ignore it", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = false;
    d.thresholdDb = -60.0;
    d.ratio = 100.0;
    d.kneeDb = 0.0;
    d.rangeDb = -8.0;

    auto p = bellWith (d);
    p.gainScale = 0.5;
    CHECK_THAT (run (p, 0.0, 1000.0, 1.0).dynDb, WithinAbs (-4.0, 0.01));

    p.gainScale = 1.0;
    p.bands[0].shape = Shape::LowCut;
    CHECK (run (p, 0.0, 1000.0, 0.5).dynDb == 0.0);
}

TEST_CASE ("dynamics: external sidechain drives a band set to External", "[dynamics]")
{
    DynParams d;
    d.on = true;
    d.autoThreshold = false;
    d.thresholdDb = -30.0;
    d.ratio = 100.0;
    d.kneeDb = 0.0;
    d.rangeDb = -9.0;
    auto p = bellWith (d);

    // Quiet programme, loud 1 kHz key on the sidechain.
    const auto runKeyed = [&] (bool external, int scChannels)
    {
        p.bands[0].dyn.external = external;
        Engine e;
        e.prepare (48000.0, 512, 2);
        std::vector<double> l (512), r (512), k (512);
        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double ph = 2.0 * std::numbers::pi * 1000.0 * (block * 512 + i) / 48000.0;
                l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = 0.001 * std::sin (ph);
                k[static_cast<size_t> (i)] = 0.8 * std::sin (ph);
            }
            double* io[2] = { l.data(), r.data() };
            const double* sc[2] = { k.data(), k.data() };
            e.process (io, 2, 512, p, scChannels > 0 ? sc : nullptr, scChannels);
        }
        return static_cast<double> (e.dynamicGainDb (0));
    };

    CHECK_THAT (runKeyed (true, 2), WithinAbs (-9.0, 0.01));    // keyed from the sidechain
    CHECK_THAT (runKeyed (true, 1), WithinAbs (-9.0, 0.01));    // mono sidechain works too
    CHECK (runKeyed (false, 2) == 0.0);                          // Internal ignores it
    CHECK (runKeyed (true, 0) == 0.0);                           // no sidechain: falls back to the input
}

TEST_CASE ("solo: the output becomes the band's part of the input", "[solo]")
{
    ParamSnapshot p;
    p.bands[2] = { true, true, Shape::Bell, Slope::S12, Placement::Stereo, 1000.0, 9.0, 4.0 };
    p.bands[3] = { true, true, Shape::LowCut, Slope::S24, Placement::Stereo, 200.0, 0.0, 0.7071 };

    const auto gainOf = [&] (int solo, double freq)
    {
        Engine e;
        e.prepare (48000.0, 512, 2);
        e.setSoloBand (solo);
        std::vector<double> l (512), r (512);
        double in = 0.0, out = 0.0;
        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < 512; ++i)
                l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = 0.25 * std::sin (2.0 * std::numbers::pi * freq * (block * 512 + i) / 48000.0);
            const auto dry = l;
            double* io[2] = { l.data(), r.data() };
            e.process (io, 2, 512, p);
            if (block >= 40)
                for (int i = 0; i < 512; ++i)
                {
                    in += dry[static_cast<size_t> (i)] * dry[static_cast<size_t> (i)];
                    out += l[static_cast<size_t> (i)] * l[static_cast<size_t> (i)];
                }
        }
        return 10.0 * std::log10 (out / in);
    };

    // Soloing the bell: its centre passes at unity (not +9: you hear the input, band-passed),
    // and a tone far away is strongly attenuated.
    CHECK_THAT (gainOf (2, 1000.0), WithinAbs (0.0, 0.1));
    CHECK (gainOf (2, 100.0) < -25.0);
    // Soloing the low cut: you hear what it removes, the lows.
    CHECK_THAT (gainOf (3, 50.0), WithinAbs (0.0, 0.3));
    CHECK (gainOf (3, 4000.0) < -40.0);
    // No solo, or a solo on an unused band: normal processing.
    CHECK_THAT (gainOf (-1, 1000.0), WithinAbs (9.0, 0.1));
    CHECK_THAT (gainOf (7, 1000.0), WithinAbs (9.0, 0.1));
}
