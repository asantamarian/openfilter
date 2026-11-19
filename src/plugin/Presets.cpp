#include "Presets.h"

namespace of::presets
{
namespace
{
constexpr const char* kFormat = "openfilter.eq.preset";

// Per-band keys, in file order. "used" is implied by presence in the array.
constexpr const char* kBandKeys[] = { "shape", "slope", "freq", "gain", "q", "place", "on", "lin", "dyn", "range", "thr", "athr",
                                      "ratio", "knee", "atk", "rel", "trig", "sc", "scfree", "scfreq", "scq", "smode", "sres" };

// Globals that belong to a preset. Output gain, balance, bypass and delta do not.
constexpr const char* kGlobalKeys[] = { "phase_mode", "lin_quality", "gain_scale", "auto_gain", "character", "drive" };

juce::var plainValue (juce::RangedAudioParameter& p)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))  return c->getCurrentChoiceName();
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (&p))    return b->get();
    return static_cast<double> (p.convertFrom0to1 (p.getValue()));
}

void setPlain (juce::RangedAudioParameter& p, const juce::var& v)
{
    float normalised = p.getDefaultValue();
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
    {
        const int index = v.isString() ? c->choices.indexOf (v.toString(), true) : static_cast<int> (v);
        if (index >= 0 && index < c->choices.size())
            normalised = c->convertTo0to1 (static_cast<float> (index));
        else if (v.isString() && v.toString().getIntValue() > 0)   // slope written as a number: 24 -> "24 dB/oct"
            for (int i = 0; i < c->choices.size(); ++i)
                if (c->choices[i].getIntValue() == v.toString().getIntValue())
                    normalised = c->convertTo0to1 (static_cast<float> (i));
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
        normalised = static_cast<bool> (v) ? 1.0f : 0.0f;
    else if (v.isDouble() || v.isInt() || v.isInt64())
        normalised = p.convertTo0to1 (juce::jlimit (p.getNormalisableRange().start, p.getNormalisableRange().end, static_cast<float> (static_cast<double> (v))));

    if (! juce::approximatelyEqual (normalised, p.getValue()))
        p.setValueNotifyingHost (normalised);
}
} // namespace

juce::File userFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("OpenFilter").getChildFile ("EQ").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> list()
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, true, juce::String ("*") + kExtension);
    files.sort();
    return files;
}

juce::var toVar (const OpenFilterProcessor& processor, const juce::String& name)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kFormat);
    root->setProperty ("version", 1);
    root->setProperty ("name", name);

    auto* global = new juce::DynamicObject();
    for (const char* key : kGlobalKeys)
        if (auto* p = processor.apvts.getParameter (key))
            global->setProperty (key, plainValue (*p));
    root->setProperty ("global", global);

    juce::Array<juce::var> bands;
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! processor.readBand (b).used)
            continue;
        auto* band = new juce::DynamicObject();
        for (const char* key : kBandKeys)
            if (auto* p = processor.bandParam (b, key))
            {
                // Omitted keys take the default on load; shape and frequency are always written.
                const bool always = juce::String (key) == "shape" || juce::String (key) == "freq";
                if (always || ! juce::approximatelyEqual (p->getValue(), p->getDefaultValue()))
                    band->setProperty (key, plainValue (*p));
            }
        bands.add (band);
    }
    root->setProperty ("bands", bands);
    return root;
}

bool apply (OpenFilterProcessor& processor, const juce::var& preset, juce::String& error)
{
    if (! preset.isObject() || preset["format"].toString() != kFormat || ! preset["bands"].isArray())
    {
        error = "Not an OpenFilter EQ preset.";
        return false;
    }
    if (static_cast<int> (preset["version"]) > 1)
        error = "Preset was written by a newer version; unknown settings are ignored.";

    processor.undo.beginNewTransaction ("Load preset");

    const auto global = preset["global"];
    for (const char* key : kGlobalKeys)
        if (auto* p = processor.apvts.getParameter (key))
            if (global.isObject() && global.hasProperty (key))
                setPlain (*p, global[key]);

    const auto* bands = preset["bands"].getArray();
    for (int b = 0; b < kMaxBands; ++b)
    {
        const bool present = b < bands->size() && (*bands)[b].isObject();
        for (const char* key : kBandKeys)
            if (auto* p = processor.bandParam (b, key))
            {
                if (present && (*bands)[b].hasProperty (key))
                    setPlain (*p, (*bands)[b][key]);
                else if (! juce::approximatelyEqual (p->getValue(), p->getDefaultValue()))
                    p->setValueNotifyingHost (p->getDefaultValue());
            }
        if (auto* used = processor.bandParam (b, "used"))
            used->setValueNotifyingHost (present ? 1.0f : 0.0f);
    }
    return true;
}

bool save (const OpenFilterProcessor& processor, const juce::File& file, juce::String& error)
{
    const auto text = juce::JSON::toString (toVar (processor, file.getFileNameWithoutExtension()));
    if (! file.getParentDirectory().createDirectory() || ! file.replaceWithText (text))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    return true;
}

bool load (OpenFilterProcessor& processor, const juce::File& file, juce::String& error)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (file.loadFileAsString(), parsed);
    if (result.failed())
    {
        error = "Could not read preset: " + result.getErrorMessage();
        return false;
    }
    return apply (processor, parsed, error);
}
} // namespace of::presets
