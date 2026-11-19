#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Design tokens of the default theme "Nocturne" (docs/spec/05-ui.md section 2). */
namespace of::theme
{
inline const juce::Colour ground      { 0xff0A0C10 };
inline const juce::Colour panel       { 0xff12151C };
inline const juce::Colour raised      { 0xff1A1E27 };
inline const juce::Colour line        { 0xff262B36 };
inline const juce::Colour lineStrong  { 0xff343B49 };
inline const juce::Colour gridMinor   { 0xff151920 };
inline const juce::Colour gridMajor   { 0xff1E232D };
inline const juce::Colour gridH       { 0xff171B23 };
inline const juce::Colour textFaint   { 0xff7D8798 };
inline const juce::Colour textMuted   { 0xff9AA3B2 };
inline const juce::Colour text        { 0xffE8ECF2 };
inline const juce::Colour curve       { 0xffF4F7FB };
inline const juce::Colour accent      { 0xff6EE7C5 };
inline const juce::Colour warn        { 0xffFFB454 };

inline juce::Colour band (int index)
{
    static const juce::Colour colours[] = { juce::Colour (0xffFF7A6B), juce::Colour (0xffFFB454), juce::Colour (0xffB6E36A), juce::Colour (0xff5FE3B5),
                                            juce::Colour (0xff5CC8FF), juce::Colour (0xff8C9BFF), juce::Colour (0xffC58CFF), juce::Colour (0xffFF7EC2) };
    return colours[static_cast<size_t> (index & 7)];
}

inline juce::Font ui (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

/** Upper-case section label, e.g. "FREQ". */
inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (textMuted);
    g.setFont (ui (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), area, just, false);
}
} // namespace of::theme
