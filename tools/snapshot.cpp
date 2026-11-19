// Renders the plugin editor to a PNG without a host, and round-trips the session state.
//   of_snapshot out.png
#include "plugin/PluginEditor.h"
#include "plugin/Presets.h"
#include "match/CurveFitter.h"

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
    processor.setBandValue (b4, "dyn", 1.0f);       // Dynamic
    processor.setBandValue (b4, "range", -6.0f);
    processor.setBandValue (b4, "athr", 0.0f);
    processor.setBandValue (b4, "thr", -48.0f);
    const int b5 = processor.addBand (of::Shape::HighShelf, 9000.0, 4.0);
    processor.setBandValue (b5, "place", static_cast<float> (of::Placement::Side));
    processor.addBand (of::Shape::HighCut, 19000.0, 0.0);
    if (auto* autoGain = processor.apvts.getParameter ("auto_gain"))
        autoGain->setValueNotifyingHost (autoGain->getValueForText ("Curve"));

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

    // Every factory preset must load and produce at least one band; a band copied as JSON
    // and added again must come back the same.
    bool factoryOk = of::presets::factoryNames().size() >= 5;
    {
        of::OpenFilterProcessor scratch;
        for (int i = 0; i < of::presets::factoryNames().size(); ++i)
        {
            juce::String error;
            const bool loaded = of::presets::loadFactory (scratch, i, error);
            int used = 0;
            for (int b = 0; b < of::kMaxBands; ++b)
                used += scratch.readBand (b).used ? 1 : 0;
            if (! loaded || used == 0)
            {
                factoryOk = false;
                std::printf ("  factory preset %d failed: %s\n", i, error.toRawUTF8());
            }
        }
        const int copy = of::presets::addBandFromVar (processor, of::presets::bandToVar (processor, b4));
        const auto x = processor.readBand (b4), y = processor.readBand (copy);
        factoryOk = factoryOk && copy >= 0 && copy != b4 && x.shape == y.shape && x.place == y.place && x.dyn == y.dyn
                 && std::abs (x.freq - y.freq) < 1.0e-3 * x.freq && std::abs (x.gainDb - y.gainDb) < 1.0e-3;
        processor.removeBand (copy);
        std::printf ("factory presets and band copy: %s (%d presets)\n", factoryOk ? "ok" : "WRONG", of::presets::factoryNames().size());
    }

    // MIDI learn: arm a parameter, move controller 74, and it must be mapped, follow the
    // controller, survive a session save, and clear.
    bool midiOk = false;
    {
        of::OpenFilterProcessor m;
        m.prepareToPlay (48000.0, 512);
        auto* gain = m.apvts.getParameter ("out_gain");
        juce::AudioBuffer<float> audio (2, 512);
        const auto send = [&] (int cc, int value)
        {
            juce::MidiBuffer events;
            events.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
            audio.clear();
            m.processBlock (audio, events);
            m.applyPendingMidi();
        };

        send (74, 20);   // nothing is mapped yet: ignored
        const bool ignored = m.mappedController (*gain) < 0 && std::abs (gain->getValue() - gain->getDefaultValue()) < 1.0e-6f;
        m.beginMidiLearn (*gain);
        const bool armed = m.isLearning (*gain);
        send (74, 127);
        const bool learned = ! m.isLearning (*gain) && m.mappedController (*gain) == 74 && gain->getValue() > 0.999f;
        send (74, 0);
        const bool follows = gain->getValue() < 0.001f;
        send (10, 127);   // another controller must not touch it
        const bool isolated = gain->getValue() < 0.001f;

        juce::MemoryBlock saved;
        m.getStateInformation (saved);
        of::OpenFilterProcessor reloaded;
        reloaded.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        const bool kept = reloaded.mappedController (*reloaded.apvts.getParameter ("out_gain")) == 74;

        m.clearMidiMapping (*gain);
        send (74, 127);
        const bool cleared = m.mappedController (*gain) < 0 && gain->getValue() < 0.001f;

        midiOk = ignored && armed && learned && follows && isolated && kept && cleared;
        std::printf ("MIDI learn: %s\n", midiOk ? "ok" : "WRONG");
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

    // OF_SNAPSHOT_PIANO=1 renders the main image with the piano strip, for checking it by eye.
    processor.pianoStrip = juce::SystemStats::getEnvironmentVariable ("OF_SNAPSHOT_PIANO", "").isNotEmpty();
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

    // EQ match, end to end: capture this track through the analyzer path while audio runs,
    // make a reference that differs from it by a known EQ, and the match must find that EQ.
    bool matchOk = false;
    if (auto* e = dynamic_cast<of::OpenFilterEditor*> (editor.get()))
    {
        auto& display = e->responseDisplay();
        display.startCapture (false);
        for (int block = 0; block < 400; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                pink = 0.985f * pink + 0.06f * (random.nextFloat() * 2.0f - 1.0f);
                buffer.setSample (0, i, pink);
                buffer.setSample (1, i, pink);
            }
            processor.processBlock (buffer, midi);
            if (block % 4 == 3)
                e->updateNow();
        }
        of::fit::Curve source {};
        const bool captured = display.finishCapture (false, source);

        of::BandParams shelf, bell;
        shelf.used = bell.used = true;
        shelf.shape = of::Shape::HighShelf;  shelf.freq = 6000.0;  shelf.gainDb = 5.0;  shelf.q = 0.9;
        bell.shape = of::Shape::Bell;        bell.freq = 400.0;    bell.gainDb = -4.0;  bell.q = 1.2;
        const auto difference = of::fit::evaluate ({ shelf, bell });
        auto reference = source;
        for (int i = 0; i < of::fit::kGridSize; ++i)
            reference[static_cast<size_t> (i)] += difference[static_cast<size_t> (i)] + 7.0;   // and 7 dB louder, which must not matter

        const auto target = of::fit::matchTarget (source, reference);
        const auto fitted = of::fit::fitCurve (target, 5);
        // Compare shapes: the match removes the overall level, so remove it from the truth too.
        auto truth = difference;
        double level = 0.0;
        int n = 0;
        for (int i = 0; i < of::fit::kGridSize; ++i)
            if (of::fit::gridFrequency (i) >= 200.0 && of::fit::gridFrequency (i) <= 5000.0) { level += truth[static_cast<size_t> (i)]; ++n; }
        for (auto& v : truth)
            v -= level / n;
        const double error = of::fit::rmsError (truth, of::fit::evaluate (fitted));
        matchOk = captured && error < 0.6 && ! fitted.empty();
        std::printf ("EQ match end to end: %s (captured %.1f s, %d bands, %.2f dB RMS from the true difference)\n",
                     matchOk ? "ok" : "WRONG", display.captureSeconds (false), static_cast<int> (fitted.size()), error);

        display.setTargetCurve (&target);
        e->showMatchPanel (true);
        e->updateNow();
        const auto matchFile = out.getSiblingFile (out.getFileNameWithoutExtension() + "-match.png");
        matchFile.deleteFile();
        juce::FileOutputStream matchStream (matchFile);
        if (matchStream.openedOk())
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f), matchStream);
        e->showMatchPanel (false);
        display.setTargetCurve (nullptr);
        e->updateNow();
    }

    // Instance hub: a second instance in this process, playing something else. Each must see
    // the other, and collisions must be marked where the two overlap and nowhere else.
    bool hubOk = false;
    {
        of::OpenFilterProcessor second;
        second.prepareToPlay (48000.0, 512);
        second.instanceName = "Bass";
        float low = 0.0f;
        juce::AudioBuffer<float> other (2, 512);
        for (int block = 0; block < 120; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                pink = 0.985f * pink + 0.06f * (random.nextFloat() * 2.0f - 1.0f);
                buffer.setSample (0, i, pink);
                buffer.setSample (1, i, pink);
                low = 0.999f * low + 0.02f * (random.nextFloat() * 2.0f - 1.0f);   // mostly below 200 Hz
                other.setSample (0, i, low);
                other.setSample (1, i, low);
            }
            processor.processBlock (buffer, midi);
            second.processBlock (other, midi);
            if (block % 9 == 8)
                processor.hub->refreshNow();
        }
        // Other processors from the checks above are still alive (and silent), so look for
        // these two by identity rather than counting.
        const auto entries = processor.hub->entries();
        bool sawBoth = false, named = false, bothLive = false;
        {
            const of::InstanceHub::Entry* mine = nullptr;
            const of::InstanceHub::Entry* bass = nullptr;
            for (const auto& e : entries)
            {
                if (e.processor == &processor) mine = &e;
                if (e.processor == &second)    bass = &e;
            }
            sawBoth = mine != nullptr && bass != nullptr;
            named = sawBoth && bass->name == "Bass";
            bothLive = sawBoth && mine->hasSignal && bass->hasSignal;
            // The bass-heavy instance and the full-range one must collide in the lows and not
            // in the top octaves, where the bass instance has nothing.
            if (sawBoth)
            {
                const auto c = of::InstanceHub::collision (mine->spectrum, bass->spectrum);
                double lows = 0.0, highs = 0.0;
                for (int i = 0; i < of::fit::kGridSize; ++i)
                    (of::fit::gridFrequency (i) < 300.0 ? lows : highs) += of::fit::gridFrequency (i) < 300.0 || of::fit::gridFrequency (i) > 4000.0 ? c[static_cast<size_t> (i)] : 0.0;
                bothLive = bothLive && lows > 3.0 && highs < 0.5;
                std::printf ("  collision strength summed: %.1f below 300 Hz, %.1f above 4 kHz\n", lows, highs);
            }
        }
        const int countWithSecond = static_cast<int> (entries.size());

        // The collision measure on its own: identical spectra collide everywhere they have
        // level, spectra 20 dB apart nowhere, and silence never.
        of::fit::Curve a {}, b {}, quiet {};
        a.fill (-30.0);  b.fill (-50.0);  quiet.fill (-140.0);
        const auto same = of::InstanceHub::collision (a, a), apart = of::InstanceHub::collision (a, b), none = of::InstanceHub::collision (quiet, quiet);
        const bool measureOk = same[100] > 0.99 && apart[100] == 0.0 && none[100] == 0.0;

        hubOk = sawBoth && named && bothLive && measureOk;
        std::printf ("instance hub: %s (%d instances in the process, both live and colliding in the lows only: %s)\n", hubOk ? "ok" : "WRONG", countWithSecond, bothLive ? "yes" : "no");

        if (auto* e = dynamic_cast<of::OpenFilterEditor*> (editor.get()))
        {
            e->showInstancePanel (true);
            e->updateNow();
            const auto hubFile = out.getSiblingFile (out.getFileNameWithoutExtension() + "-instances.png");
            hubFile.deleteFile();
            juce::FileOutputStream hubStream (hubFile);
            if (hubStream.openedOk())
                juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f), hubStream);
            e->showInstancePanel (false);
            e->responseDisplay().setOverlays ({});
            e->updateNow();
        }
    }
    // The second instance left the hub with its destructor.
    bool stillListed = false;
    for (const auto& e : processor.hub->entries())
        stillListed = stillListed || e.name == "Bass";
    hubOk = hubOk && ! stillListed;

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    const bool written = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("%s %s (%d x %d)\n", written ? "wrote" : "FAILED to write", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());

    // A second image with the band table open and a wider gain range.
    processor.tableOpen = true;
    processor.pianoStrip = true;
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

    // The smallest window: the paged dock and the slimmed top bar.
    processor.tableOpen = false;
    processor.pianoStrip = false;
    processor.displayRangeDb = 12.0;
    processor.editorWidth = 800;
    processor.editorHeight = 560;
    editor.reset();
    editor.reset (processor.createEditor());
    if (auto* e = dynamic_cast<of::OpenFilterEditor*> (editor.get()))
    {
        e->responseDisplay().selectBand (b4);
        e->updateNow();
    }
    const auto smallFile = out.getSiblingFile (out.getFileNameWithoutExtension() + "-small.png");
    smallFile.deleteFile();
    {
        juce::FileOutputStream smallStream (smallFile);
        if (smallStream.openedOk())
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f), smallStream);
    }
    std::printf ("wrote %s (%d x %d)\n", smallFile.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());

    return written && tableWritten && roundTrip && presetOk && parseOk && slotsOk && factoryOk && matchOk && hubOk && midiOk ? 0 : 1;
}
