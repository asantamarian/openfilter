#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Types.h"

/** Parameter IDs and layout. See docs/spec/02-parameters.md. IDs are permanent. */
namespace of::params
{
/** "b01_freq" for band 0 and suffix "freq". */
juce::String bandId (int band, const char* suffix);

const juce::StringArray& shapeNames();
const juce::StringArray& slopeNames();
const juce::StringArray& placementNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Formats a frequency the way the UI shows it: "850 Hz", "3.20 kHz". */
juce::String formatFrequency (double hz);
juce::String formatGain (double db);

/** Parses "850", "850 hz", "1.2k", "1k2", and note names such as "A4", "C#3", "Bb2+20"
    (cents), with A4 = 440 Hz. Returns 0 when the text is not a frequency. */
double parseFrequency (const juce::String& text);
} // namespace of::params
