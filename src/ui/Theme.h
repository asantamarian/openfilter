#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>

/** Design tokens of the default theme "Nocturne" (docs/spec/05-ui.md section 2). */
namespace of::theme
{
// The tokens are plain variables so a theme can replace them (Theme.cpp). They start as
// Nocturne. A theme is a user-wide choice, shared by every instance in the process.
inline juce::Colour ground      { 0xff07080F };
inline juce::Colour panel       { 0xff0E101B };
inline juce::Colour raised      { 0xff171A2B };
inline juce::Colour line        { 0xff262B45 };
inline juce::Colour lineStrong  { 0xff3A4168 };
inline juce::Colour gridMinor   { 0xff12152A };
inline juce::Colour gridMajor   { 0xff1E2347 };
inline juce::Colour gridH       { 0xff171B38 };
inline juce::Colour textFaint   { 0xff7D8798 };
inline juce::Colour textMuted   { 0xff9AA3B2 };
inline juce::Colour text        { 0xffE8ECF2 };
inline juce::Colour curve       { 0xffF4F7FB };
inline juce::Colour accent      { 0xff6EE7C5 };
inline juce::Colour warn        { 0xffFFB454 };

inline std::array<juce::Colour, 8> bandColours { juce::Colour (0xffFF7A6B), juce::Colour (0xffFFB454), juce::Colour (0xffB6E36A), juce::Colour (0xff5FE3B5),
                                                 juce::Colour (0xff5CC8FF), juce::Colour (0xff8C9BFF), juce::Colour (0xffC58CFF), juce::Colour (0xffFF7EC2) };

inline juce::Colour band (int index) { return bandColours[static_cast<size_t> (index & 7)]; }

/** Themes (docs/spec/06-state.md section 6): three built in, plus JSON files with the
    extension .oftheme in userFolder(). */
juce::StringArray available();
juce::String currentName();
/** Switches theme. Returns false, with a message, if a theme file cannot be read. */
bool apply (const juce::String& name, juce::String& error);
/** Remembers the choice for next time, and loads it. */
void save (const juce::String& name);
void loadSaved();
juce::File userFolder();
/** The current theme as the text of a theme file: a starting point for making one. */
juce::String exampleFile();
bool isLight();

// The interface carries its own fonts, so it looks the same on every machine: Inter for
// text, JetBrains Mono for numbers. Loaded once per process.
inline juce::Typeface::Ptr interRegular()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    return face;
}

inline juce::Typeface::Ptr interSemiBold()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    return face;
}

inline juce::Typeface::Ptr jetBrainsMono()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    return face;
}

inline juce::Font ui (float size, bool bold = false)
{
    // Point height is the font's nominal size (its em), which is what the design sizes mean;
    // plain height would be the whole line, and text would come out about a fifth smaller.
    return juce::Font (juce::FontOptions (bold ? interSemiBold() : interRegular()).withPointHeight (size));
}

inline juce::Font mono (float size)
{
    return juce::Font (juce::FontOptions (jetBrainsMono()).withPointHeight (size * 0.96f));
}

/** Neon look: a stroke with a soft halo of its own colour, built from wider, fainter
    strokes underneath (cheap, and works in every JUCE renderer). */
inline void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width, float strength = 1.0f)
{
    const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;
    const juce::PathStrokeType::EndCapStyle cap = juce::PathStrokeType::rounded;
    g.setColour (colour.withMultipliedAlpha (0.05f * strength));
    g.strokePath (path, juce::PathStrokeType (width + 12.0f, joint, cap));
    g.setColour (colour.withMultipliedAlpha (0.10f * strength));
    g.strokePath (path, juce::PathStrokeType (width + 7.0f, joint, cap));
    g.setColour (colour.withMultipliedAlpha (0.22f * strength));
    g.strokePath (path, juce::PathStrokeType (width + 3.0f, joint, cap));
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (width, joint, cap));
}

/** A filled dot with the same kind of halo. */
inline void fillGlowEllipse (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float strength = 1.0f)
{
    g.setColour (colour.withMultipliedAlpha (0.08f * strength));
    g.fillEllipse (area.expanded (8.0f));
    g.setColour (colour.withMultipliedAlpha (0.16f * strength));
    g.fillEllipse (area.expanded (4.5f));
    g.setColour (colour.withMultipliedAlpha (0.30f * strength));
    g.fillEllipse (area.expanded (2.0f));
    g.setColour (colour);
    g.fillEllipse (area);
}

/** Buttons with a neon outline when they are on. */
class NeonLook final : public juce::LookAndFeel_V4
{
public:
    // Buttons, lists and menus use the bundled font too.
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override
    {
        return font.isBold() ? interSemiBold() : interRegular();
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = button.getToggleState();
        const float alpha = button.isEnabled() ? 1.0f : 0.45f;
        g.setColour ((down ? lineStrong : over ? raised.brighter (0.08f) : raised).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (area, 7.0f);
        if (on)
        {
            g.setColour (accent.withAlpha (0.10f));
            g.fillRoundedRectangle (area, 7.0f);
            g.setColour (accent.withAlpha (0.18f));
            g.drawRoundedRectangle (area.expanded (0.5f), 7.5f, 3.0f);
            g.setColour (accent);
            g.drawRoundedRectangle (area, 7.0f, 1.2f);
        }
        else
        {
            g.setColour ((over ? lineStrong : line).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (area, 7.0f, 1.0f);
        }
    }
};

/** Upper-case section label, e.g. "FREQ". */
inline void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification just = juce::Justification::centredLeft)
{
    g.setColour (textMuted);
    g.setFont (ui (10.0f, true).withExtraKerningFactor (0.08f));
    g.drawText (text.toUpperCase(), area, just, false);
}
} // namespace of::theme
