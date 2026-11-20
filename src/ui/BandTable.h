#pragma once

#include "Theme.h"
#include "plugin/PluginProcessor.h"

namespace of
{
/** Every band as a row with editable cells (docs/spec/05-ui.md section 8). Gives the whole
    EQ without the graphical display, which is also what a screen reader needs. */
class BandTable final : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit BandTable (OpenFilterProcessor&);

    /** Called from the editor's timer. */
    void refresh();
    void setSelectedBand (int band);
    /** Re-reads the theme colours it holds. */
    void themeChanged();
    std::function<void (int)> onSelectBand;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Column { Number = 1, On, ShapeCol, Freq, Gain, Q, SlopeCol, Channel };

    int getNumRows() override { return static_cast<int> (rows.size()); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRow) override;

    juce::String cellText (int row, int column) const;
    void applyText (int band, int column, const juce::String& text);
    void showChoiceMenu (int band, int column);

    struct Row { int band; BandParams params; };

    OpenFilterProcessor& processor;
    std::vector<Row> rows;
    juce::TableListBox table { "Bands", this };
    int selectedBand = -1;
    bool updatingSelection = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandTable)
};
} // namespace of
