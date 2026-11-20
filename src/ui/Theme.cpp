#include "Theme.h"

namespace of::theme
{
namespace
{
struct Token { const char* key; juce::Colour* target; };

const Token kTokens[] = {
    { "ground", &ground }, { "panel", &panel }, { "raised", &raised }, { "line", &line }, { "lineStrong", &lineStrong },
    { "gridMinor", &gridMinor }, { "gridMajor", &gridMajor }, { "gridH", &gridH },
    { "textFaint", &textFaint }, { "textMuted", &textMuted }, { "text", &text },
    { "curve", &curve }, { "accent", &accent }, { "warn", &warn },
};

struct BuiltIn { const char* name; juce::uint32 tokens[14]; juce::uint32 bands[8]; };

// Token order as in kTokens.
const BuiltIn kBuiltIn[] = {
    { "Nocturne",
      { 0xff07080F, 0xff0E101B, 0xff171A2B, 0xff262B45, 0xff3A4168, 0xff12152A, 0xff1E2347, 0xff171B38,
        0xff7D8798, 0xff9AA3B2, 0xffE8ECF2, 0xffF4F7FB, 0xff6EE7C5, 0xffFFB454 },
      { 0xffFF7A6B, 0xffFFB454, 0xffB6E36A, 0xff5FE3B5, 0xff5CC8FF, 0xff8C9BFF, 0xffC58CFF, 0xffFF7EC2 } },
    { "High contrast",
      { 0xff000000, 0xff000000, 0xff1C1C1C, 0xff8C8C8C, 0xffFFFFFF, 0xff1A1A1A, 0xff3A3A3A, 0xff2A2A2A,
        0xffD0D0D0, 0xffE6E6E6, 0xffFFFFFF, 0xffFFFFFF, 0xff00FFC8, 0xffFFD000 },
      { 0xffFF8A7A, 0xffFFC060, 0xffC8F070, 0xff60F0C0, 0xff70D4FF, 0xffA0ACFF, 0xffD4A0FF, 0xffFF90CC } },
    { "Daylight",
      { 0xffF4F5F8, 0xffE9EBF0, 0xffFFFFFF, 0xffC9CDD6, 0xff9AA1AF, 0xffE6E8EE, 0xffD5D9E2, 0xffDEE1E8,
        0xff5E6675, 0xff4A5160, 0xff14171D, 0xff14171D, 0xff0A8F74, 0xffB26A00 },
      { 0xffD9412F, 0xffC97A00, 0xff5E8F10, 0xff0E9C76, 0xff1479C9, 0xff4A5BE0, 0xff8A47D6, 0xffD43C8C } },
};

juce::String active = "Nocturne";

juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenFilter").getChildFile ("EQ").getChildFile ("settings.json");
}

void applyBuiltIn (const BuiltIn& b)
{
    for (size_t i = 0; i < std::size (kTokens); ++i)
        *kTokens[i].target = juce::Colour (b.tokens[i]);
    for (size_t i = 0; i < 8; ++i)
        bandColours[i] = juce::Colour (b.bands[i]);
}

bool parseColour (const juce::var& v, juce::Colour& out)
{
    auto s = v.toString().trim();
    if (! s.startsWithChar ('#'))
        return false;
    s = s.substring (1);
    if (s.length() == 6)
        s = "ff" + s;
    if (s.length() != 8 || ! s.containsOnly ("0123456789abcdefABCDEF"))
        return false;
    out = juce::Colour (static_cast<juce::uint32> (s.getHexValue64()));
    return true;
}
} // namespace

juce::File userFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("OpenFilter").getChildFile ("Themes");
    dir.createDirectory();
    return dir;
}

juce::StringArray available()
{
    juce::StringArray names;
    for (const auto& b : kBuiltIn)
        names.add (b.name);
    for (const auto& f : userFolder().findChildFiles (juce::File::findFiles, false, "*.oftheme"))
        names.addIfNotAlreadyThere (f.getFileNameWithoutExtension());
    return names;
}

juce::String currentName() { return active; }

bool apply (const juce::String& name, juce::String& error)
{
    for (const auto& b : kBuiltIn)
        if (name == b.name)
        {
            applyBuiltIn (b);
            active = name;
            return true;
        }

    const auto file = userFolder().getChildFile (name + ".oftheme");
    juce::var parsed;
    if (! file.existsAsFile() || juce::JSON::parse (file.loadFileAsString(), parsed).failed() || ! parsed.isObject())
    {
        error = "Could not read the theme \"" + name + "\".";
        return false;
    }

    // Missing keys fall back to Nocturne.
    applyBuiltIn (kBuiltIn[0]);
    for (const auto& token : kTokens)
        if (parsed.hasProperty (token.key))
            parseColour (parsed[token.key], *token.target);
    if (const auto* list = parsed["bands"].getArray())
        for (int i = 0; i < juce::jmin (8, list->size()); ++i)
            parseColour ((*list)[i], bandColours[static_cast<size_t> (i)]);
    active = name;
    return true;
}

void save (const juce::String& name)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("theme", name);
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (juce::JSON::toString (juce::var (root)));
}

void loadSaved()
{
    juce::var parsed;
    if (juce::JSON::parse (settingsFile().loadFileAsString(), parsed).wasOk() && parsed.isObject())
    {
        juce::String ignored;
        const auto name = parsed["theme"].toString();
        if (name.isNotEmpty() && name != active)
            apply (name, ignored);
    }
}

juce::String exampleFile()
{
    auto* root = new juce::DynamicObject();
    for (const auto& token : kTokens)
        root->setProperty (token.key, "#" + token.target->toDisplayString (false));
    juce::Array<juce::var> list;
    for (const auto& c : bandColours)
        list.add ("#" + c.toDisplayString (false));
    root->setProperty ("bands", list);
    return juce::JSON::toString (juce::var (root));
}

bool isLight() { return ground.getBrightness() > 0.5f; }
} // namespace of::theme
