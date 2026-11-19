#pragma once

#include "PluginProcessor.h"

/** Preset files: documented JSON in plain units (docs/spec/06-state.md section 3). */
namespace of::presets
{
inline constexpr const char* kExtension = ".ofeq";

/** userApplicationDataDirectory/OpenFilter/EQ/Presets, created on demand. */
juce::File userFolder();

/** Every preset file under the user folder, sorted by path. */
juce::Array<juce::File> list();

/** The current bands and preset-relevant globals as a preset object. */
juce::var toVar (const OpenFilterProcessor&, const juce::String& name);

/** Replaces the bands and preset-relevant globals. One undo step. Returns false, with a
    message, if the data is not a preset. Message thread only. */
bool apply (OpenFilterProcessor&, const juce::var& preset, juce::String& error);

bool save (const OpenFilterProcessor&, const juce::File& file, juce::String& error);
bool load (OpenFilterProcessor&, const juce::File& file, juce::String& error);
} // namespace of::presets
