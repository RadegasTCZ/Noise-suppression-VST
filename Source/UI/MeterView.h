#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MeterBallistics.h"
#include "../Host/PeakMeter.h"
#include <array>

namespace nsp::ui
{
/** IN and OUT horizontal peak meters. Each row: label (24 px), bar
    (-60..0 dBFS, linear in dB, flat level-blue fill on the well colour, 2 px white hold tick),
    held peak as a number (38 px, mono 11, 1 decimal, "-inf" on the floor).
    tick() runs on the message thread (editor timer) and repaints only when a drawn pixel
    position or the number text changes, so a settled meter costs nothing. */
class MeterView final : public juce::Component
{
public:
    static constexpr int kLabelWidth = 24, kNumberWidth = 38, kGap = 6, kRowHeight = 16, kRowGap = 5, kBarHeight = 8,
                         kHeight = 2 * kRowHeight + kRowGap;

    MeterView (nsp::PeakHold& in, nsp::PeakHold& out);

    void tick (float dtSec);

    const nsp::MeterBallistics& ballistics (int row) const noexcept { return rows[(size_t) row].ballistics; }
    juce::String numberText (int row) const                          { return rows[(size_t) row].shown.text; }
    int getRepaintRequestCount() const noexcept                       { return repaintRequests; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Shown
    {
        int barPx = 0, tickPx = 0;
        juce::String text;
        bool operator== (const Shown& o) const { return barPx == o.barPx && tickPx == o.tickPx && text == o.text; }
        bool operator!= (const Shown& o) const { return ! operator== (o); }
    };

    struct Row
    {
        nsp::PeakHold* source = nullptr;
        const char* label = "";
        nsp::MeterBallistics ballistics;
        Shown shown;
    };

    juce::Rectangle<int> rowBounds (int row) const;
    juce::Rectangle<int> barBounds (int row) const;
    Shown shownFor (int row) const;

    std::array<Row, 2> rows;
    int repaintRequests = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterView)
};
} // namespace nsp::ui
