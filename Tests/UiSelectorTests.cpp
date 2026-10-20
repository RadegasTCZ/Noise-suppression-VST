#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Params.h"
#include "../Source/UI/SegmentedSelector.h"
#include "../Source/UI/Theme.h"
#include <cstdlib>

namespace nsp
{
class UiSelectorTests final : public juce::UnitTest
{
public:
    UiSelectorTests() : juce::UnitTest ("Segmented selector", "UiSelector") {}

    static juce::RangedAudioParameter& param (NoiseSuppressionProcessor& p, const char* id)
    {
        return *p.apvts.getParameter (id);
    }

    static void setIndex (NoiseSuppressionProcessor& p, const char* id, int index)
    {
        auto& prm = param (p, id);
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) index));
    }

    static bool near (juce::Colour a, juce::Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
             + std::abs (a.getBlue() - b.getBlue()) <= 12;
    }

    /** Colour inside pill `i`, 4 px from its left end at mid height (clear of the centred text). */
    static juce::Colour fillOf (const juce::Image& img, const ui::SegmentedSelector& s, int i)
    {
        const auto r = s.getSegmentBounds (i);
        return img.getPixelAt (r.getX() + 4, r.getCentreY());
    }

    /** Counts gestures and value changes of one parameter. */
    struct ChangeCounter final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override                { ++values; }
        void parameterGestureChanged (int, bool) override               { ++gestures; }
        int values = 0, gestures = 0;
    };

    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> pos,
                                     juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY, &c, &c, now, pos.toFloat(), now, 1, false };
    }

    void runTest() override
    {
        beginTest ("2 x 2 grid of 29 x 20 pills, 4 px gaps (WebRTC level); 1 column of 62 x 20 (RNNoise model); 1 column of 62 x 16, 3 px gaps (DeepFilterNet strength)");
        {
            NoiseSuppressionProcessor p;
            ui::SegmentedSelector level (param (p, ids::webrtcLevel), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            expect (level.gridBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (level.getLocalBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (level.getSegmentBounds (0) == juce::Rectangle<int> (0, 0, 29, 20));
            expect (level.getSegmentBounds (1) == juce::Rectangle<int> (33, 0, 29, 20));
            expect (level.getSegmentBounds (2) == juce::Rectangle<int> (0, 24, 29, 20));
            expect (level.getSegmentBounds (3) == juce::Rectangle<int> (33, 24, 29, 20));
            expectEquals (level.segmentAt ({ 45, 34 }), 3);
            expectEquals (level.segmentAt ({ 31, 10 }), -1);   // horizontal gap
            expectEquals (level.segmentAt ({ 10, 22 }), -1);   // vertical gap

            ui::SegmentedSelector model (param (p, ids::rnnModel), { "Standard", "Light" }, 1, 62, 20, 4);
            expect (model.gridBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (model.getSegmentBounds (0) == juce::Rectangle<int> (0, 0, 62, 20));
            expect (model.getSegmentBounds (1) == juce::Rectangle<int> (0, 24, 62, 20));

            ui::SegmentedSelector strength (param (p, ids::dfnStrength), { "Gentle", "Normal", "Strong" }, 1, 62, 16, 3);
            expect (strength.gridBounds() == juce::Rectangle<int> (0, 0, 62, 54));
            expect (strength.getSegmentBounds (0) == juce::Rectangle<int> (0, 0, 62, 16));
            expect (strength.getSegmentBounds (1) == juce::Rectangle<int> (0, 19, 62, 16));
            expect (strength.getSegmentBounds (2) == juce::Rectangle<int> (0, 38, 62, 16));
        }

        beginTest ("selection follows the parameter: initial value, host automation, state load");
        {
            NoiseSuppressionProcessor p;
            ui::SegmentedSelector level (param (p, ids::webrtcLevel), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            expectEquals (level.getSelectedIndex(), 1);             // default 12 dB
            setIndex (p, ids::webrtcLevel, 3);
            expectEquals (level.getSelectedIndex(), 3);

            NoiseSuppressionProcessor other;
            setIndex (other, ids::webrtcLevel, 0);
            juce::MemoryBlock state;
            other.getStateInformation (state);
            p.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (level.getSelectedIndex(), 0);
        }

        beginTest ("a click on a pill sets the parameter; clicks on a gap or the selected pill change nothing");
        {
            NoiseSuppressionProcessor p;
            ui::SegmentedSelector model (param (p, ids::rnnModel), { "Standard", "Light" }, 1, 62, 20, 4);
            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre()));
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 1.0f);
            expectEquals (model.getSelectedIndex(), 1);
            model.mouseDown (mouseAt (model, { 30, 22 }));          // the gap between the pills
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 1.0f);
            model.selectIndex (0);
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 0.0f);
            model.selectIndex (7);                                  // out of range: ignored
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 0.0f);
        }

        beginTest ("re-clicking the selected pill sends no gesture and no value change; a right-click changes nothing");
        {
            NoiseSuppressionProcessor p;
            auto& prm = param (p, ids::rnnModel);
            ui::SegmentedSelector model (prm, { "Standard", "Light" }, 1, 62, 20, 4);
            ChangeCounter counter;
            prm.addListener (&counter);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (0).getCentre()));   // already selected
            expectEquals (counter.gestures, 0);
            expectEquals (counter.values, 0);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre(),
                                      juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
            expectEquals (counter.values, 0);
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 0.0f);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre(),
                                      juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
            expectEquals (p.apvts.getRawParameterValue (ids::rnnModel)->load(), 1.0f);
            expect (counter.values > 0);
            prm.removeListener (&counter);
        }

        beginTest ("Theme draws the selected pill accent-filled and the others knob-filled");
        {
            NoiseSuppressionProcessor p;
            ui::Theme theme;
            ui::SegmentedSelector level (param (p, ids::webrtcLevel), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            level.setLookAndFeel (&theme);

            auto img = level.createComponentSnapshot (level.getLocalBounds(), true, 1.0f);
            expect (near (fillOf (img, level, 1), ui::colours::accent));
            for (int i : { 0, 2, 3 })
                expect (near (fillOf (img, level, i), ui::colours::knob), "pill " + juce::String (i));

            setIndex (p, ids::webrtcLevel, 2);
            img = level.createComponentSnapshot (level.getLocalBounds(), true, 1.0f);
            expect (near (fillOf (img, level, 2), ui::colours::accent));
            expect (near (fillOf (img, level, 1), ui::colours::knob));
            level.setLookAndFeel (nullptr);
        }
    }
};

static UiSelectorTests uiSelectorTests;
} // namespace nsp
