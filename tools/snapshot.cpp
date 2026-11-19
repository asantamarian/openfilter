// Renders the plugin editor to a PNG without a host, and round-trips the session state.
//   of_snapshot out.png
#include "plugin/PluginEditor.h"

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
    return written && roundTrip ? 0 : 1;
}
