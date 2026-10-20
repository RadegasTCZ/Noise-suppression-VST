#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace nsp::ui
{
/** Contents of the engine control slot (62 x 70): one per engine, all created up front, only the
    active engine's panel visible. readout() is the footer text at the right ("" = none). */
class EnginePanel : public juce::Component
{
public:
    static constexpr int kWidth = 62, kHeight = 70, kKnobHeight = 58;   // knob = rotary 42 + value box 16; caption 12

    ~EnginePanel() override = default;
    virtual juce::String readout() const { return {}; }
    /** True when readout() is a warning (drawn whole, in the warn colour, squeezed to fit). */
    virtual bool readoutIsWarning() const { return false; }

protected:
    /** Caption under a knob: uiFont 10, dim (Theme's Label colour), centred, no border. */
    static void setUpCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (10.0f));
        l.setJustificationType (juce::Justification::centred);
        l.setBorderSize ({});
        l.setInterceptsMouseClicks (false, false);
    }
};
} // namespace nsp::ui
