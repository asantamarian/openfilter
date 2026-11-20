#include "BandTable.h"

namespace of
{
namespace
{
/** An editable value cell. */
class ValueCell final : public juce::Label
{
public:
    ValueCell()
    {
        setEditable (false, true, false);   // double-click to edit
        setJustificationType (juce::Justification::centredRight);
        setFont (theme::mono (12.0f));
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::Label::backgroundWhenEditingColourId, theme::ground);
        setColour (juce::Label::textWhenEditingColourId, theme::text);
        setColour (juce::Label::outlineWhenEditingColourId, theme::accent);
        setMinimumHorizontalScale (1.0f);
    }

    int band = -1, column = 0;
    std::function<void (int, int, const juce::String&)> onCommit;
    std::function<void (int)> onClicked;

    void textWasEdited() override
    {
        if (onCommit)
            onCommit (band, column, getText());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onClicked)
            onClicked (band);
        juce::Label::mouseDown (e);
    }
};

bool sameRow (const BandParams& a, const BandParams& b)
{
    return a.on == b.on && a.shape == b.shape && a.slope == b.slope && a.place == b.place
        && juce::exactlyEqual (a.freq, b.freq) && juce::exactlyEqual (a.gainDb, b.gainDb) && juce::exactlyEqual (a.q, b.q);
}
} // namespace

BandTable::BandTable (OpenFilterProcessor& p) : processor (p)
{
    setTitle ("Band table");
    auto& header = table.getHeader();
    const int fixed = juce::TableHeaderComponent::visible;
    header.addColumn ("#", Number, 44, 44, 44, fixed);
    header.addColumn ("On", On, 52, 52, 52, fixed);
    header.addColumn ("Shape", ShapeCol, 120, 90, 200, fixed | juce::TableHeaderComponent::resizable);
    header.addColumn ("Frequency", Freq, 110, 80, 200, fixed | juce::TableHeaderComponent::resizable);
    header.addColumn ("Gain", Gain, 100, 70, 200, fixed | juce::TableHeaderComponent::resizable);
    header.addColumn ("Q", Q, 80, 60, 200, fixed | juce::TableHeaderComponent::resizable);
    header.addColumn ("Slope", SlopeCol, 110, 90, 200, fixed | juce::TableHeaderComponent::resizable);
    header.addColumn ("Channel", Channel, 100, 80, 200, fixed | juce::TableHeaderComponent::resizable);
    themeChanged();
    table.setRowHeight (26);
    table.setHeaderHeight (26);
    table.setOutlineThickness (0);
    addAndMakeVisible (table);
    refresh();
}

void BandTable::themeChanged()
{
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textMuted);
    header.setColour (juce::TableHeaderComponent::outlineColourId, theme::line);
    header.setColour (juce::TableHeaderComponent::highlightColourId, theme::raised);
    table.setColour (juce::ListBox::backgroundColourId, theme::panel);
    table.setColour (juce::ListBox::outlineColourId, theme::line);
    rows.clear();   // rebuild the cells so their text colours follow
    table.updateContent();
    refresh();
    repaint();
}

void BandTable::refresh()
{
    std::vector<Row> now;
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto params = processor.readBand (b);
        if (params.used)
            now.push_back ({ b, params });
    }

    bool changed = now.size() != rows.size();
    for (size_t i = 0; ! changed && i < now.size(); ++i)
        changed = now[i].band != rows[i].band || ! sameRow (now[i].params, rows[i].params);
    if (! changed)
        return;

    rows = std::move (now);
    table.updateContent();
    setSelectedBand (selectedBand);
    table.repaint();
}

void BandTable::setSelectedBand (int band)
{
    selectedBand = band;
    updatingSelection = true;
    int row = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].band == band)
            row = static_cast<int> (i);
    if (row >= 0)
        table.selectRow (row, true);
    else
        table.deselectAllRows();
    updatingSelection = false;
}

void BandTable::selectedRowsChanged (int lastRow)
{
    if (updatingSelection || lastRow < 0 || lastRow >= getNumRows())
        return;
    selectedBand = rows[static_cast<size_t> (lastRow)].band;
    if (onSelectBand)
        onSelectBand (selectedBand);
}

void BandTable::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::line);
    g.fillRect (0, 0, getWidth(), 1);
    if (rows.empty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::ui (12.0f));
        g.drawText ("No bands yet", getLocalBounds(), juce::Justification::centred, false);
    }
}

void BandTable::resized() { table.setBounds (getLocalBounds().withTrimmedTop (1)); }

void BandTable::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    g.fillAll (selected ? theme::raised : theme::panel);
    g.setColour (theme::line.withAlpha (0.6f));
    g.fillRect (0, height - 1, width, 1);
    juce::ignoreUnused (row);
}

juce::String BandTable::cellText (int row, int column) const
{
    const auto& r = rows[static_cast<size_t> (row)];
    const auto& p = r.params;
    switch (column)
    {
        case Number:   return juce::String (r.band + 1);
        case On:       return p.on ? "On" : "Off";
        case ShapeCol: return params::shapeNames()[static_cast<int> (p.shape)];
        case Freq:     return params::formatFrequency (p.freq);
        case Gain:     return hasGain (p.shape) ? params::formatGain (p.gainDb) : juce::String ("-");
        case Q:        return juce::String (p.q, 2);
        case SlopeCol: return p.shape == Shape::FlatTilt ? juce::String ("-") : params::slopeNames()[static_cast<int> (p.slope)];
        case Channel:  return params::placementNames()[static_cast<int> (p.place)];
        default:       return {};
    }
}

void BandTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row >= getNumRows() || column == Freq || column == Gain || column == Q)
        return;   // value cells are components
    const auto& r = rows[static_cast<size_t> (row)];

    if (column == Number)
    {
        g.setColour (theme::band (r.band));
        g.fillEllipse (10.0f, 0.5f * static_cast<float> (height) - 3.5f, 7.0f, 7.0f);
    }
    g.setColour (column == On && ! r.params.on ? theme::textFaint : theme::text);
    g.setFont (column == Number ? theme::mono (12.0f) : theme::ui (12.0f));
    g.drawText (cellText (row, column), juce::Rectangle<int> (width, height).reduced (column == Number ? 22 : 8, 0),
                juce::Justification::centredLeft, true);
}

juce::Component* BandTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (row >= getNumRows() || (column != Freq && column != Gain && column != Q))
    {
        delete existing;
        return nullptr;
    }

    auto* cell = dynamic_cast<ValueCell*> (existing);
    if (cell == nullptr)
    {
        delete existing;
        cell = new ValueCell();
        cell->onCommit = [this] (int band, int col, const juce::String& text) { applyText (band, col, text); };
        cell->onClicked = [this] (int band)
        {
            setSelectedBand (band);
            if (onSelectBand)
                onSelectBand (band);
        };
    }
    const auto& r = rows[static_cast<size_t> (row)];
    cell->band = r.band;
    cell->column = column;
    const bool applies = column != Gain || hasGain (r.params.shape);
    cell->setEnabled (applies);
    cell->setColour (juce::Label::textColourId, applies ? theme::text : theme::textFaint);
    if (! cell->isBeingEdited())
        cell->setText (cellText (row, column), juce::dontSendNotification);
    static const char* names[] = { "Frequency", "Gain", "Q" };
    cell->setTitle ("Band " + juce::String (r.band + 1) + " " + names[column - Freq]);
    return cell;
}

void BandTable::applyText (int band, int column, const juce::String& text)
{
    processor.undo.beginNewTransaction ("Edit band " + juce::String (band + 1));
    if (column == Freq)
    {
        const double f = params::parseFrequency (text);
        if (f > 0.0)
            processor.setBandValue (band, "freq", static_cast<float> (juce::jlimit (10.0, 30000.0, f)));
    }
    else if (text.trim().containsAnyOf ("0123456789"))
    {
        const float v = text.trim().getFloatValue();
        if (column == Gain) processor.setBandValue (band, "gain", juce::jlimit (-30.0f, 30.0f, v));
        if (column == Q)    processor.setBandValue (band, "q", juce::jlimit (0.025f, 40.0f, v));
    }
    rows.clear();   // force the next refresh to rewrite every cell, including a rejected entry
    refresh();
}

void BandTable::cellClicked (int row, int column, const juce::MouseEvent&)
{
    if (row >= getNumRows())
        return;
    const auto& r = rows[static_cast<size_t> (row)];
    if (column == On)
    {
        processor.undo.beginNewTransaction ("Toggle band");
        processor.setBandValue (r.band, "on", r.params.on ? 0.0f : 1.0f);
    }
    else if (column == ShapeCol || column == SlopeCol || column == Channel)
        showChoiceMenu (r.band, column);
}

void BandTable::showChoiceMenu (int band, int column)
{
    const char* suffix = column == ShapeCol ? "shape" : column == SlopeCol ? "slope" : "place";
    const auto& names = column == ShapeCol ? params::shapeLabels() : column == SlopeCol ? params::slopeNames() : params::placementNames();
    const auto current = processor.readBand (band);
    const int selected = column == ShapeCol ? static_cast<int> (current.shape) : column == SlopeCol ? static_cast<int> (current.slope) : static_cast<int> (current.place);

    juce::PopupMenu menu;
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i], true, i == selected);

    juce::Component::SafePointer<BandTable> safe (this);
    const juce::String key (suffix);
    menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (getTopLevelComponent()), [safe, band, key] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        safe->processor.undo.beginNewTransaction ("Edit band " + juce::String (band + 1));
        safe->processor.setBandValue (band, key.toRawUTF8(), static_cast<float> (result - 1));
    });
}
} // namespace of
