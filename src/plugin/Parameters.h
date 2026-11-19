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
} // namespace of::params
