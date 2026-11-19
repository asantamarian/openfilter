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

/** Built-in starting points. Read-only; shown under "Factory" in the preset menu. */
juce::StringArray factoryNames();
bool loadFactory (OpenFilterProcessor&, int index, juce::String& error);

/** One band as the object used inside a preset's "bands" array (also the clipboard format). */
juce::var bandToVar (const OpenFilterProcessor&, int band);
/** Adds a band from such an object in the lowest free slot. Returns the slot, or -1. */
int addBandFromVar (OpenFilterProcessor&, const juce::var& band);
bool load (OpenFilterProcessor&, const juce::File& file, juce::String& error);
} // namespace of::presets
