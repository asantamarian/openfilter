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

juce::var bandToVar (const OpenFilterProcessor& processor, int b)
{
    auto* band = new juce::DynamicObject();
    for (const char* key : kBandKeys)
        if (auto* p = processor.bandParam (b, key))
        {
            // Omitted keys take the default on load; shape and frequency are always written.
            const bool always = juce::String (key) == "shape" || juce::String (key) == "freq";
            if (always || ! juce::approximatelyEqual (p->getValue(), p->getDefaultValue()))
                band->setProperty (key, plainValue (*p));
        }
    return band;
}

int addBandFromVar (OpenFilterProcessor& processor, const juce::var& band)
{
    if (! band.isObject())
        return -1;
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (processor.readBand (b).used)
            continue;
        for (const char* key : kBandKeys)
            if (auto* p = processor.bandParam (b, key))
            {
                if (band.hasProperty (key))
                    setPlain (*p, band[key]);
                else if (! juce::approximatelyEqual (p->getValue(), p->getDefaultValue()))
                    p->setValueNotifyingHost (p->getDefaultValue());
            }
        if (auto* used = processor.bandParam (b, "used"))
            used->setValueNotifyingHost (1.0f);
        return b;
    }
    return -1;
}

namespace
{
// Factory presets: plain starting points, written in the preset format itself.
struct Factory { const char* name; const char* bands; };
const Factory kFactory[] = {
    { "Low end cleanup",
      R"([{"shape":"Low Cut","slope":"24 dB/oct","freq":35,"q":0.7071},
          {"shape":"Bell","freq":250,"gain":-2.5,"q":1.2}])" },
    { "Vocal clarity",
      R"([{"shape":"Low Cut","slope":"18 dB/oct","freq":90,"q":0.7071},
          {"shape":"Bell","freq":300,"gain":-2.5,"q":1.3},
          {"shape":"Bell","freq":3500,"gain":2.0,"q":1.0},
          {"shape":"High Shelf","freq":10000,"gain":2.5,"q":1.0}])" },
    { "Air shelf",
      R"([{"shape":"High Shelf","freq":12000,"gain":3.0,"q":0.8}])" },
    { "Tame harshness (dynamic)",
      R"([{"shape":"Bell","freq":3200,"gain":0,"q":2.0,"dyn":"Dynamic","range":-5,"athr":true,"thr":-36,"ratio":4,"atk":5,"rel":120}])" },
    { "Resonance tamer (spectral)",
      R"([{"shape":"Bell","freq":2500,"gain":0,"q":0.5,"dyn":"Spectral","smode":"Resonance","range":-8,"thr":-60,"ratio":4,"atk":10,"rel":150}])" },
    { "Kick room (cut the bass around 60 Hz on mid)",
      R"([{"shape":"Bell","freq":60,"gain":-3.0,"q":2.5,"place":"Mid"}])" },
    { "Wider top (side shelf)",
      R"([{"shape":"High Shelf","freq":6000,"gain":2.5,"q":0.8,"place":"Side"},
          {"shape":"Low Cut","slope":"12 dB/oct","freq":120,"q":0.7071,"place":"Side"}])" },
    { "Gentle master tilt",
      R"([{"shape":"Tilt Shelf","slope":"6 dB/oct","freq":1000,"gain":1.5}])" },
    { "Telephone",
      R"([{"shape":"Low Cut","slope":"24 dB/oct","freq":350,"q":0.7071},
          {"shape":"High Cut","slope":"24 dB/oct","freq":3200,"q":0.7071},
          {"shape":"Bell","freq":1200,"gain":4.0,"q":1.0}])" },
};
} // namespace

juce::StringArray factoryNames()
{
    juce::StringArray names;
    for (const auto& f : kFactory)
        names.add (f.name);
    return names;
}

bool loadFactory (OpenFilterProcessor& processor, int index, juce::String& error)
{
    if (index < 0 || index >= static_cast<int> (std::size (kFactory)))
        return false;
    juce::var bands;
    if (juce::JSON::parse (kFactory[index].bands, bands).failed())
    {
        error = "Factory preset is malformed.";
        return false;
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", kFormat);
    root->setProperty ("version", 1);
    root->setProperty ("name", kFactory[index].name);
    root->setProperty ("bands", bands);
    return apply (processor, juce::var (root), error);
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
        if (processor.readBand (b).used)
            bands.add (bandToVar (processor, b));
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
