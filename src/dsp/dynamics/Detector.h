#pragma once

#include "../Types.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace of
{
/** Level detection and gain computer of one dynamic band (docs/spec/03-dsp.md section 7).
    Listens to the plugin input, never to another band, through a filter that follows the
    band. process() runs per sample on the audio thread; update() once per control block. */
class Detector
{
public:
    void prepare (double sampleRate, int controlBlock) noexcept
    {
        fs = sampleRate;
        controlSeconds = controlBlock / sampleRate;
        levelCoef = 1.0 - std::exp (-1.0 / (0.005 * sampleRate));        // 5 ms power average
        slowCoef = 1.0 - std::exp (-controlSeconds / 3.0);               // 3 s program level
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};
        env[0] = env[1] = 0.0;
        slow = 1.0e-3;   // -30 dBFS until real signal arrives
        dyn = 0.0;
    }

    /** Sets the detection filter: band-pass at the band for bells, the affected region for
        shelves. Cheap; called when the band moves. */
    void setFilter (Shape shape, double freq, double q) noexcept
    {
        const double w = 2.0 * std::numbers::pi * std::clamp (freq, 10.0, 0.45 * fs) / fs;
        const double cs = std::cos (w), sn = std::sin (w);
        const double qq = shape == Shape::Bell ? std::clamp (q, 0.5, 8.0) : 0.7071;
        const double alpha = sn / (2.0 * qq), a0 = 1.0 + alpha;
        double c0, c1, c2;
        if (shape == Shape::LowShelf)       { c0 = 0.5 * (1.0 - cs); c1 = 1.0 - cs; c2 = c0; }       // low-pass
        else if (shape == Shape::Bell)      { c0 = alpha; c1 = 0.0; c2 = -alpha; }                   // band-pass, 0 dB peak
        else                                { c0 = 0.5 * (1.0 + cs); c1 = -(1.0 + cs); c2 = c0; }    // high-pass
        b0 = c0 / a0;  b1 = c1 / a0;  b2 = c2 / a0;
        a1 = -2.0 * cs / a0;  a2 = (1.0 - alpha) / a0;
    }

    /** Feeds the detector. right may be null (mono). */
    void process (const double* left, const double* right, int n, Placement place) noexcept
    {
        const bool stereo = right != nullptr;
        for (int i = 0; i < n; ++i)
        {
            double a = left[i], b = stereo ? right[i] : left[i];
            if (stereo && place == Placement::Mid)        a = b = 0.5 * (left[i] + right[i]);
            else if (stereo && place == Placement::Side)  a = b = 0.5 * (left[i] - right[i]);
            else if (place == Placement::Left)            b = a;
            else if (stereo && place == Placement::Right) a = b;

            const double fa = tick (state[0], a);
            env[0] += (fa * fa - env[0]) * levelCoef;
            if (stereo && place == Placement::Stereo)   // stereo bands react to the louder side
            {
                const double fb = tick (state[1], b);
                env[1] += (fb * fb - env[1]) * levelCoef;
            }
            else
                env[1] = env[0];
        }
    }

    /** Control rate: threshold, gain computer and ballistics. Returns the dynamic gain in dB
        to add to the band's static gain. */
    double update (const DynParams& p, double rangeDb) noexcept
    {
        const double power = std::max (env[0], env[1]);
        const double levelDb = 10.0 * std::log10 (power + 1.0e-20);

        double threshold = p.thresholdDb;
        if (p.autoThreshold)
        {
            if (levelDb > -70.0)   // frozen during silence
                slow += (power - slow) * slowCoef;
            // The same parameter is read as an offset of -30..+30 dB around the program level.
            threshold = 10.0 * std::log10 (slow + 1.0e-20) + (p.thresholdDb + 90.0) / 90.0 * 60.0 - 30.0;
        }

        const double x = p.below ? threshold - levelDb : levelDb - threshold;
        const double knee = std::max (p.kneeDb, 0.0);
        double over = 0.0;
        if (2.0 * x >= knee)             over = x;
        else if (2.0 * x > -knee)        over = (x + 0.5 * knee) * (x + 0.5 * knee) / (2.0 * knee);

        const double amount = std::min (over * (1.0 - 1.0 / std::max (p.ratio, 1.0)), std::abs (rangeDb));
        const double target = rangeDb < 0.0 ? -amount : amount;

        const double ms = std::abs (target) > std::abs (dyn) ? p.attackMs : p.releaseMs;
        const double coef = std::exp (-controlSeconds / (std::max (ms, 0.01) * 0.001));
        dyn = target + (dyn - target) * coef;
        if (std::abs (dyn) < 1.0e-4 && target == 0.0)
            dyn = 0.0;
        return dyn;
    }

    double current() const noexcept { return dyn; }

private:
    struct State { double z1 = 0.0, z2 = 0.0; };

    double tick (State& s, double x) const noexcept
    {
        const double y = b0 * x + s.z1;
        s.z1 = b1 * x - a1 * y + s.z2;
        s.z2 = b2 * x - a2 * y;
        return y;
    }

    double fs = 48000.0, controlSeconds = 32.0 / 48000.0;
    double levelCoef = 0.0, slowCoef = 0.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    State state[2];
    double env[2] { 0.0, 0.0 };
    double slow = 1.0e-3, dyn = 0.0;
};
} // namespace of
