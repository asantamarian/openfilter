// Renders the plugin editor to a PNG without a host, and round-trips the session state.
//   of_snapshot out.png
#include "plugin/PluginEditor.h"
#include "plugin/Presets.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "openfilter.png");

    of::OpenFilterProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // The same example curve as the UI mockup.
    const int lowCut = processor.addBand (of::Shape::LowCut, 80.0, 0.0);
    processor.setBandValue (lowCut, "slope", static_cast<float> (of::Slope::S24));
    const int b2 = processor.addBand (of::Shape::Bell, 210.0, -3.5);
    processor.setBandValue (b2, "q", 1.4f);
    const int b3 = processor.addBand (of::Shape::Bell, 950.0, 2.5);
    processor.setBandValue (b3, "q", 0.9f);
    const int b4 = processor.addBand (of::Shape::Bell, 3200.0, -4.5);
    processor.setBandValue (b4, "q", 2.2f);
    processor.setBandValue (b4, "place", static_cast<float> (of::Placement::Mid));
    const int b5 = processor.addBand (of::Shape::HighShelf, 9000.0, 4.0);
    processor.setBandValue (b5, "place", static_cast<float> (of::Placement::Side));
    processor.addBand (of::Shape::HighCut, 19000.0, 0.0);

    // State round trip: what is saved must load back identically.
    juce::MemoryBlock first, second;
    processor.getStateInformation (first);
    of::OpenFilterProcessor other;
    other.setStateInformation (first.getData(), static_cast<int> (first.getSize()));
    other.getStateInformation (second);
    // Values pass through the parameter's 0..1 mapping on load, so compare with a tolerance
    // of one part in 100 000 rather than bit for bit.
    bool roundTrip = first.getSize() == second.getSize();
    for (auto* p : processor.getParameters())
    {
        auto* a = dynamic_cast<juce::RangedAudioParameter*> (p);
        auto* b = a != nullptr ? other.apvts.getParameter (a->getParameterID()) : nullptr;
        if (a == nullptr || b == nullptr || std::abs (a->getValue() - b->getValue()) > 1.0e-5f)
        {
            roundTrip = false;
            std::printf ("  differs: %s\n", a != nullptr ? a->getParameterID().toRawUTF8() : "?");
        }
    }
    std::printf ("state round trip: %s (%d bytes, %d parameters)\n", roundTrip ? "ok" : "MISMATCH",
                 static_cast<int> (first.getSize()), processor.getParameters().size());

    // Preset round trip: to JSON text, into a fresh instance, and the bands must match.
    bool presetOk = false;
    {
        const auto text = juce::JSON::toString (of::presets::toVar (processor, "Snapshot"));
        juce::var parsed;
        juce::String error;
        of::OpenFilterProcessor fresh;
        fresh.addBand (of::Shape::Notch, 500.0, 0.0);   // must be replaced, not merged
        presetOk = juce::JSON::parse (text, parsed).wasOk() && of::presets::apply (fresh, parsed, error);
        for (int b = 0; b < of::kMaxBands && presetOk; ++b)
        {
            const auto x = processor.readBand (b), y = fresh.readBand (b);
            presetOk = x.used == y.used && (! x.used || (x.shape == y.shape && x.slope == y.slope && x.place == y.place
                                                         && std::abs (x.freq - y.freq) < 1.0e-3 * x.freq && std::abs (x.gainDb - y.gainDb) < 1.0e-3 && std::abs (x.q - y.q) < 1.0e-3));
        }
        juce::String rejected;
        presetOk = presetOk && ! of::presets::apply (fresh, juce::var ("nonsense"), rejected) && rejected.isNotEmpty();
        std::printf ("preset round trip: %s (%d characters of JSON)\n", presetOk ? "ok" : "MISMATCH", text.length());
    }

    // Typed frequency entry.
    const auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-6 * juce::jmax (1.0, b); };
    using of::params::parseFrequency;
    const bool parseOk = near (parseFrequency ("850"), 850.0) && near (parseFrequency ("850 Hz"), 850.0) && near (parseFrequency ("1.2k"), 1200.0)
                      && near (parseFrequency ("1k2"), 1200.0) && near (parseFrequency ("3.20 kHz"), 3200.0) && near (parseFrequency ("A4"), 440.0)
                      && near (parseFrequency ("a3"), 220.0) && near (parseFrequency ("C4"), 261.6255653005986) && near (parseFrequency ("Bb3"), 233.0818807590449)
                      && near (parseFrequency ("C#5"), 554.3652619537442) && near (parseFrequency ("A4+100"), 466.1637615180899) && near (parseFrequency ("B2"), 123.47082531403103)
                      && near (parseFrequency ("hello"), 0.0) && near (parseFrequency (""), 0.0);
    std::printf ("frequency parsing: %s\n", parseOk ? "ok" : "WRONG");

    // A/B/C/D: edit in B, go back to A, and both must come back as they were.
    bool slotsOk = false;
    {
        const double freqA = processor.readBand (b4).freq;
        processor.switchSlot (1);
        processor.setBandValue (b4, "freq", 500.0f);
        processor.switchSlot (0);
        const bool backToA = std::abs (processor.readBand (b4).freq - freqA) < 1.0e-3 * freqA;
        processor.switchSlot (1);
        const bool bKept = std::abs (processor.readBand (b4).freq - 500.0) < 0.5;
        // Slots survive a session save and load.
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        of::OpenFilterProcessor reloaded;
        reloaded.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const bool activeKept = reloaded.activeSlot() == 1 && std::abs (reloaded.readBand (b4).freq - 500.0) < 0.5;
        reloaded.switchSlot (0);
        const bool otherKept = std::abs (reloaded.readBand (b4).freq - freqA) < 1.0e-3 * freqA;
        processor.switchSlot (0);
        slotsOk = backToA && bKept && activeKept && otherKept;
        std::printf ("A/B/C/D slots: %s\n", slotsOk ? "ok" : "WRONG");
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    // Run some audio through so the meters and the analyzer show something: pink-ish noise
    // plus a 440 Hz tone. The analyzer taps only run while an editor exists.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random (1);
    float pink = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            pink = 0.985f * pink + 0.06f * (random.nextFloat() * 2.0f - 1.0f);
            const float s = pink + 0.02f * (random.nextFloat() * 2.0f - 1.0f)
                          + 0.1f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * static_cast<float> (block * 512 + i) / 48000.0f);
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        processor.processBlock (buffer, midi);
    }
    if (auto* e = dynamic_cast<of::OpenFilterEditor*> (editor.get()))
    {
        e->responseDisplay().selectBand (b4);
        e->updateNow();   // what the 30 Hz timer would do
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    const bool written = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("%s %s (%d x %d)\n", written ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());

    // A second image with the band table open and a wider gain range.
    processor.tableOpen = true;
    processor.displayRangeDb = 30.0;
    editor.reset();
    editor.reset (processor.createEditor());
    if (auto* e = dynamic_cast<of::OpenFilterEditor*> (editor.get()))
    {
        e->responseDisplay().selectBand (b4);
        e->updateNow();
    }
    const auto tableFile = out.getSiblingFile (out.getFileNameWithoutExtension() + "-table.png");
    tableFile.deleteFile();
    juce::FileOutputStream tableStream (tableFile);
    const bool tableWritten = tableStream.openedOk()
        && juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f), tableStream);
    std::printf ("%s %s\n", tableWritten ? "wrote" : "FAILED to write", tableFile.getFullPathName().toRawUTF8());

    return written && tableWritten && roundTrip && presetOk && parseOk && slotsOk ? 0 : 1;
}
