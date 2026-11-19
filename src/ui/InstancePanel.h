#pragma once

#include "ResponseDisplay.h"
#include "hub/InstanceHub.h"

namespace of
{
/** Lists the other OpenFilter EQ instances in the project (docs/spec/04-analysis.md
    section 2.3). Click one to draw its spectrum over this display and mark collisions. */
class InstancePanel final : public juce::Component
{
public:
    InstancePanel (OpenFilterProcessor&, ResponseDisplay&);

    /** Called from the editor's timer: re-reads the hub and pushes overlays to the display. */
    void refresh();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** For tools: overlay every other instance. */
    void showAll();

private:
    juce::Rectangle<int> rowBounds (int row) const { return { 10, 40 + row * 58, getWidth() - 20, 52 }; }

    OpenFilterProcessor& processor;
    ResponseDisplay& display;
    juce::TextButton closeButton { "Close" };
    std::vector<InstanceHub::Entry> others;
    InstanceHub::Entry self;
    std::vector<int> shown;   // serials of the instances overlaid on the display

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstancePanel)
};
} // namespace of
