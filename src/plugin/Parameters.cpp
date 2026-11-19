#include "Parameters.h"

namespace of::params
{
namespace
{
using Float = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Bool = juce::AudioParameterBool;
using Range = juce::NormalisableRange<float>;

juce::ParameterID pid (const juce::String& id) { return { id, 1 }; }

Range logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float n) { return s * std::pow (e / s, n); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
             [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
}

juce::AudioParameterFloatAttributes freqAttr()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return formatFrequency (v); })
        .withValueFromStringFunction ([] (const juce::String& t)
        {
            const auto s = t.trim().toLowerCase();
            const float v = s.getFloatValue();
            return s.containsChar ('k') ? v * 1000.0f : v;
        });
}

juce::AudioParameterFloatAttributes dbAttr()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return formatGain (v); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}

juce::AudioParameterFloatAttributes unitAttr (const char* unit, int decimals)
{
    const juce::String u (unit);
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([u, decimals] (float v, int) { return juce::String (v, decimals) + (u.isEmpty() ? "" : " " + u); })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
}
} // namespace

juce::String bandId (int band, const char* suffix)
{
    return "b" + juce::String (band + 1).paddedLeft ('0', 2) + "_" + suffix;
}

const juce::StringArray& shapeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };
    return names;
}

const juce::StringArray& slopeNames()
{
    static const juce::StringArray names { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
    return names;
}

const juce::StringArray& placementNames()
{
    static const juce::StringArray names { "Stereo", "Left", "Right", "Mid", "Side" };
    return names;
}

juce::String formatFrequency (double hz)
{
    return hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0, 2) + " kHz";
}

juce::String formatGain (double db)
{
    if (std::abs (db) < 0.05)
        db = 0.0;
    return (db > 0.0 ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto fixed = juce::AudioParameterChoiceAttributes().withAutomatable (false);

    // Globals (docs/spec/02-parameters.md section 3).
    layout.add (std::make_unique<Choice> (pid ("phase_mode"), "Phase Mode", juce::StringArray { "Zero Latency", "Natural", "Linear", "Mixed" }, 0, fixed));
    layout.add (std::make_unique<Choice> (pid ("lin_quality"), "Linear Quality", juce::StringArray { "Low", "Medium", "High", "Very High", "Max" }, 1, fixed));
    layout.add (std::make_unique<Float> (pid ("gain_scale"), "Gain Scale", Range (-200.0f, 200.0f), 100.0f, unitAttr ("%", 0)));
    layout.add (std::make_unique<Float> (pid ("out_gain"), "Output Gain", Range (-36.0f, 36.0f), 0.0f, dbAttr()));
    layout.add (std::make_unique<Float> (pid ("out_pan"), "Output Balance", Range (-100.0f, 100.0f), 0.0f, unitAttr ("", 0)));
    layout.add (std::make_unique<Bool> (pid ("out_invert"), "Invert Polarity", false));
    layout.add (std::make_unique<Choice> (pid ("auto_gain"), "Auto Gain", juce::StringArray { "Off", "Curve", "Loudness" }, 0));
    layout.add (std::make_unique<Bool> (pid ("bypass"), "Bypass", false));
    layout.add (std::make_unique<Bool> (pid ("delta"), "Delta", false));
    layout.add (std::make_unique<Choice> (pid ("character"), "Character", juce::StringArray { "Clean", "Tube", "Iron" }, 0, fixed));
    layout.add (std::make_unique<Float> (pid ("drive"), "Drive", Range (0.0f, 100.0f), 30.0f, unitAttr ("%", 0)));
    layout.add (std::make_unique<Choice> (pid ("lookahead"), "Dynamics Lookahead", juce::StringArray { "Off", "2 ms", "5 ms", "10 ms" }, 0, fixed));
    layout.add (std::make_unique<Bool> (pid ("sc_listen"), "Sidechain Listen", false));

    // Bands (section 2): 23 parameters each, in a group per band.
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto n = "Band " + juce::String (b + 1) + " ";
        const auto id = [b] (const char* suffix) { return pid (bandId (b, suffix)); };
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), "Band " + juce::String (b + 1), " | ");

        group->addChild (std::make_unique<Bool> (id ("used"), n + "Used", false));
        group->addChild (std::make_unique<Bool> (id ("on"), n + "Enabled", true));
        group->addChild (std::make_unique<Choice> (id ("shape"), n + "Shape", shapeNames(), 0));
        group->addChild (std::make_unique<Choice> (id ("slope"), n + "Slope", slopeNames(), 1));
        group->addChild (std::make_unique<Float> (id ("freq"), n + "Frequency", logRange (10.0f, 30000.0f), 1000.0f, freqAttr()));
        group->addChild (std::make_unique<Float> (id ("gain"), n + "Gain", Range (-30.0f, 30.0f), 0.0f, dbAttr()));
        group->addChild (std::make_unique<Float> (id ("q"), n + "Q", logRange (0.025f, 40.0f), 1.0f, unitAttr ("", 2)));
        group->addChild (std::make_unique<Choice> (id ("place"), n + "Placement", placementNames(), 0));
        group->addChild (std::make_unique<Bool> (id ("lin"), n + "Linear Phase", false));
        group->addChild (std::make_unique<Choice> (id ("dyn"), n + "Dynamics Mode", juce::StringArray { "Off", "Dynamic", "Spectral" }, 0));
        group->addChild (std::make_unique<Float> (id ("range"), n + "Dynamic Range", Range (-30.0f, 30.0f), 0.0f, dbAttr()));
        group->addChild (std::make_unique<Float> (id ("thr"), n + "Threshold", Range (-90.0f, 0.0f), -30.0f, dbAttr()));
        group->addChild (std::make_unique<Bool> (id ("athr"), n + "Auto Threshold", true));
        group->addChild (std::make_unique<Float> (id ("ratio"), n + "Ratio", logRange (1.0f, 100.0f), 3.0f, unitAttr (":1", 1)));
        group->addChild (std::make_unique<Float> (id ("knee"), n + "Knee", Range (0.0f, 24.0f), 6.0f, dbAttr()));
        group->addChild (std::make_unique<Float> (id ("atk"), n + "Attack", logRange (0.05f, 500.0f), 10.0f, unitAttr ("ms", 1)));
        group->addChild (std::make_unique<Float> (id ("rel"), n + "Release", logRange (5.0f, 5000.0f), 120.0f, unitAttr ("ms", 0)));
        group->addChild (std::make_unique<Choice> (id ("trig"), n + "Trigger", juce::StringArray { "Above", "Below" }, 0));
        group->addChild (std::make_unique<Choice> (id ("sc"), n + "Detector Source", juce::StringArray { "Internal", "External" }, 0));
        group->addChild (std::make_unique<Bool> (id ("scfree"), n + "Free Detector Filter", false));
        group->addChild (std::make_unique<Float> (id ("scfreq"), n + "Detector Frequency", logRange (10.0f, 30000.0f), 1000.0f, freqAttr()));
        group->addChild (std::make_unique<Float> (id ("scq"), n + "Detector Q", logRange (0.1f, 20.0f), 1.0f, unitAttr ("", 2)));
        group->addChild (std::make_unique<Choice> (id ("smode"), n + "Spectral Mode", juce::StringArray { "Absolute", "Resonance" }, 1));
        group->addChild (std::make_unique<Float> (id ("sres"), n + "Spectral Resolution", logRange (1.0f / 24.0f, 1.0f), 1.0f / 6.0f, unitAttr ("oct", 3)));

        layout.add (std::move (group));
    }
    return layout;
}
} // namespace of::params
