#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace nsp::ui
{
/** Rotary Suppress knob, flipped: clockwise / drag up / wheel up = lower dB = stronger
    suppression, so the Theme's arc (rotary start -> pointer) grows with suppression strength.
    Range -40..-5 dB comes from the SliderAttachment. Value box: "-20 dB". */
class SuppressKnob final : public juce::Slider
{
public:
    static constexpr int textBoxWidth = 62, textBoxHeight = 16;

    SuppressKnob() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, textBoxHeight);
        setTextValueSuffix (" dB");
        setNumDecimalPlacesToDisplay (0);
    }

    double valueToProportionOfLength (double value) override
    {
        return 1.0 - juce::Slider::valueToProportionOfLength (value);
    }

    double proportionOfLengthToValue (double proportion) override
    {
        return juce::Slider::proportionOfLengthToValue (1.0 - proportion);
    }

    /** Arrow keys follow the flip: Up/Right = stronger suppression = lower dB (as drag and wheel). */
    bool keyPressed (const juce::KeyPress& key) override
    {
        const bool stronger = key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey;
        const bool weaker = key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey;

        if (! isEnabled() || ! (stronger || weaker))
            return juce::Slider::keyPressed (key);

        const double step = getInterval() > 0.0 ? getInterval() : (getMaximum() - getMinimum()) / 100.0;
        setValue (getValue() + (stronger ? -step : step), juce::sendNotificationSync);
        return true;
    }

    juce::String getTextFromValue (double value) override
    {
        return juce::String (juce::roundToInt (value)) + getTextValueSuffix();
    }

    double getValueFromText (const juce::String& text) override
    {
        return text.retainCharacters ("-+0123456789.").getDoubleValue();
    }
};
} // namespace nsp::ui
