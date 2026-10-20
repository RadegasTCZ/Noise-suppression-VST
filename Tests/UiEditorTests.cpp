#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/Params.h"
#include "../Source/UI/Theme.h"
#include "../Source/UI/SuppressKnob.h"
#include "../Source/UI/SnsPanel.h"
#include "../Source/UI/RnnoisePanel.h"
#include "../Source/UI/DfnPanel.h"
#include "../Source/UI/SegmentedSelector.h"
#include "TestPaths.h"
#include <cstdlib>
#include <set>

namespace nsp
{
class UiEditorTests final : public juce::UnitTest
{
public:
    UiEditorTests() : juce::UnitTest ("Editor", "UiEditor") {}

    static void prepare (NoiseSuppressionProcessor& p)
    {
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
    }

    static juce::Component* findById (juce::Component& root, const juce::String& id)
    {
        if (root.getComponentID() == id) return &root;
        for (auto* c : root.getChildren())
            if (auto* f = findById (*c, id)) return f;
        return nullptr;
    }

    static void collectIds (juce::Component& root, juce::StringArray& out)
    {
        for (auto* c : root.getChildren())
        {
            if (c->getComponentID().isNotEmpty()) out.add (c->getComponentID());
            collectIds (*c, out);
        }
    }

    static void setEngine (NoiseSuppressionProcessor& p, int index)
    {
        auto* e = p.apvts.getParameter (ids::engine);
        e->setValueNotifyingHost (e->convertTo0to1 ((float) index));
    }

    static int accentPixels (juce::Component& c)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto px = img.getPixelAt (x, y);
                if (std::abs (px.getRed() - 0xf0) + std::abs (px.getGreen() - 0xa0) + std::abs (px.getBlue() - 0x50) <= 30
                    && px.getAlpha() > 200)
                    ++n;
            }
        return n;
    }

    void runTest() override
    {
        beginTest ("createEditorAndMakeActive returns the 440 x 140 non-resizable editor");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            expect (p.hasEditor());
            // createEditor() is private in JUCE 9's AudioProcessor; the unique_ptr is declared after p,
            // so the editor is deleted before the processor
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorAndMakeActive());
            expect (e != nullptr);
            expect (dynamic_cast<NoiseSuppressionEditor*> (e.get()) != nullptr);
            expectEquals (e->getWidth(), 440);
            expectEquals (e->getHeight(), 140);
            expect (! e->isResizable());
            expectEquals (dynamic_cast<NoiseSuppressionEditor&> (*e).titleText(), juce::String ("NOISE SUPPRESSION"));
            p.editorBeingDeleted (e.get());   // JUCE asserts in Debug otherwise
            e.reset();
        }

        beginTest ("layout: design geometry, every child inside the window");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            expect (findById (e, ids::engine)->getBounds() == juce::Rectangle<int> (158, 10, 188, 24));
            expect (findById (e, ids::bypass)->getBounds() == juce::Rectangle<int> (356, 8, 76, 28));
            expect (findById (e, ids::monitorResidual)->getBounds() == juce::Rectangle<int> (174, 99, 130, 20));
            expect (findById (e, "panel.sns")->getBounds() == juce::Rectangle<int> (19, 53, 62, 70));
            auto* limiter = findById (e, ids::limiter);
            expect (limiter != nullptr && dynamic_cast<juce::ToggleButton*> (limiter) != nullptr, "limiter toggle");
            if (limiter != nullptr)
            {
                expect (juce::Rectangle<int> (91, 53, 62, 70).contains (limiter->getBounds()), limiter->getBounds().toString());
                expect (! limiter->getBounds().isEmpty());
            }
            for (auto* c : e.getChildren())
                expect (e.getLocalBounds().contains (c->getBounds()), c->getComponentID() + " " + c->getBounds().toString());
        }

        beginTest ("WebRTC 2 x 2 pills and RNNoise stacked pills in the 62 x 70 slot");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            expect (findById (e, "panel.webrtc")->getBounds() == juce::Rectangle<int> (19, 53, 62, 70));
            expect (findById (e, "panel.rnnoise")->getBounds() == juce::Rectangle<int> (19, 53, 62, 70));

            auto* level = dynamic_cast<ui::SegmentedSelector*> (findById (e, ids::webrtcLevel));
            auto* model = dynamic_cast<ui::SegmentedSelector*> (findById (e, ids::rnnModel));
            expect (level != nullptr && model != nullptr);
            if (level == nullptr || model == nullptr)
                return;

            // editor coordinates of the design layout
            const juce::Rectangle<int> levelPills[] = { { 19, 60, 29, 20 }, { 52, 60, 29, 20 }, { 19, 84, 29, 20 }, { 52, 84, 29, 20 } };
            const juce::StringArray levelText { "6", "12", "18", "21" };
            expectEquals (level->getNumSegments(), 4);
            for (int i = 0; i < 4; ++i)
            {
                expect (e.getLocalArea (level, level->getSegmentBounds (i)) == levelPills[i], "level pill " + juce::String (i));
                expectEquals (level->getSegmentText (i), levelText[i]);
            }

            const juce::Rectangle<int> modelPills[] = { { 19, 60, 62, 20 }, { 19, 84, 62, 20 } };
            expectEquals (model->getNumSegments(), 2);
            for (int i = 0; i < 2; ++i)
                expect (e.getLocalArea (model, model->getSegmentBounds (i)) == modelPills[i], "model pill " + juce::String (i));
            expectEquals (model->getSegmentText (0), juce::String ("Standard"));
            expectEquals (model->getSegmentText (1), juce::String ("Light"));
        }

        beginTest ("every parameter is bound to exactly one control, and the binding is live");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            juce::StringArray controlIds;
            collectIds (e, controlIds);

            juce::StringArray paramIds;
            for (auto* param : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                    paramIds.add (withId->paramID);
            const juce::StringArray expected { ids::engine, ids::bypass, ids::monitorResidual,
                                               ids::classicSuppress, ids::snsSuppress, ids::webrtcLevel, ids::rnnModel, ids::dfnStrength,
                                               ids::limiter };
            expectEquals (paramIds.size(), expected.size());
            for (auto& id : expected)
                expect (paramIds.contains (id), id);

            for (auto& id : paramIds)
            {

                int n = 0;
                for (auto& c : controlIds)
                    if (c == id) ++n;
                expectEquals (n, 1, id);

                auto* param = p.apvts.getParameter (id);
                auto* control = findById (e, id);
                if (param == nullptr || control == nullptr) { expect (false, id + " missing"); continue; }
                const float target = param->getValue() < 0.5f ? 1.0f : 0.0f;
                param->setValueNotifyingHost (target);       // message thread: the attachment updates synchronously
                const float denorm = param->convertFrom0to1 (target);
                if (auto* s = dynamic_cast<juce::Slider*> (control))
                    expectWithinAbsoluteError (s->getValue(), (double) denorm, 1.0e-4, id);
                else if (auto* b = dynamic_cast<juce::Button*> (control))
                    expect (b->getToggleState() == (denorm >= 0.5f), id);
                else if (auto* cb = dynamic_cast<juce::ComboBox*> (control))
                    expectEquals (cb->getSelectedItemIndex(), juce::roundToInt (denorm), id);
                else if (auto* seg = dynamic_cast<ui::SegmentedSelector*> (control))
                    expectEquals (seg->getSelectedIndex(), juce::roundToInt (denorm), id);
                else
                    expect (false, id + " is not a Slider, Button, ComboBox or SegmentedSelector");
            }
        }

        beginTest ("engine combo lists engineNames(); value boxes read '-20 dB' and '100 %'");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            auto* combo = dynamic_cast<juce::ComboBox*> (findById (e, ids::engine));
            expect (combo != nullptr);
            const auto names = engineNames();
            expectEquals (combo->getNumItems(), names.size());
            for (int i = 0; i < names.size(); ++i)
                expectEquals (combo->getItemText (i), names[i]);
            expectEquals (combo->getSelectedItemIndex(), (int) kEngineSns);   // default engine

            auto* sns = dynamic_cast<juce::Slider*> (findById (e, ids::snsSuppress));
            expectEquals (sns->getTextFromValue (sns->getValue()), juce::String ("-20 dB"));
        }

        beginTest ("changing the engine parameter then ticking the timer shows the right panel and readout");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            auto* classic = findById (e, "panel.classic");
            auto* sns = findById (e, "panel.sns");
            expect (classic != nullptr && sns != nullptr);
            expect (sns->isVisible() && ! classic->isVisible());   // default SNS, shown from construction

            setEngine (p, kEngineClassic);
            e.timerCallback();
            expect (classic->isVisible());
            expect (! sns->isVisible());
            expectEquals (e.readoutText(), juce::String());

            setEngine (p, kEngineSns);
            e.timerCallback();
            expect (sns->isVisible());
            expect (! classic->isVisible());
            expectEquals (e.readoutText(), "Floor " + juce::String (p.getSnsNoiseFloorDb(), 1));

            auto* webrtc = findById (e, "panel.webrtc");
            auto* rnnoise = findById (e, "panel.rnnoise");
            auto* dfn = findById (e, "panel.dfn");
            expect (webrtc != nullptr && rnnoise != nullptr && dfn != nullptr);
            for (auto [engine, shown] : { std::pair<int, juce::Component*> { kEngineWebRtc, webrtc }, { kEngineRnnoise, rnnoise }, { kEngineDfn, dfn } })
            {
                setEngine (p, engine);
                e.timerCallback();
                for (auto* panel : { classic, sns, webrtc, rnnoise, dfn })
                    expect (panel->isVisible() == (panel == shown), "engine " + juce::String (engine) + " " + panel->getComponentID());
                expectEquals (e.readoutText(), juce::String(), "no readout for engine " + juce::String (engine));   // only SNS shows a readout
                expect (! e.readoutIsWarning());
            }
        }

        beginTest ("RnnoisePanel: 'Model failed to load' warning only while the selected model failed");
        {
            NoiseSuppressionProcessor p;
            ui::RnnoisePanel ok (p.apvts, [] (int) { return false; });
            expectEquals (ok.readout(), juce::String());
            expect (! ok.readoutIsWarning());

            ui::RnnoisePanel lightBroken (p.apvts, [] (int model) { return model == 1; });
            expectEquals (lightBroken.readout(), juce::String());          // Standard selected: fine
            auto* model = p.apvts.getParameter (ids::rnnModel);
            model->setValueNotifyingHost (model->convertTo0to1 (1.0f));
            expectEquals (lightBroken.readout(), juce::String ("Model failed to load"));
            expect (lightBroken.readoutIsWarning());
        }

        beginTest ("DfnPanel: 'Model failed to load' warning only while the model failed");
        {
            NoiseSuppressionProcessor p;
            ui::DfnPanel ok (p.apvts, [] { return false; });
            expectEquals (ok.readout(), juce::String());
            expect (! ok.readoutIsWarning());

            ui::DfnPanel broken (p.apvts, [] { return true; });
            expectEquals (broken.readout(), juce::String ("Model failed to load"));
            expect (broken.readoutIsWarning());
        }

        beginTest ("DfnPanel: strength pills and caption lie inside the 62 x 70 slot without overlapping");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            auto* panel = findById (e, "panel.dfn");
            auto* strength = dynamic_cast<ui::SegmentedSelector*> (findById (e, ids::dfnStrength));
            expect (panel != nullptr && strength != nullptr);
            if (panel == nullptr || strength == nullptr)
                return;
            expect (panel->getBounds() == juce::Rectangle<int> (19, 53, 62, 70));
            expectEquals (strength->getNumSegments(), 3);
            expectEquals (strength->getSegmentText (1), juce::String ("Normal"));
            expectEquals (strength->getSelectedIndex(), 1);
            expect (strength->getBounds() == juce::Rectangle<int> (0, 2, 62, 54));

            juce::Label* caption = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* l = dynamic_cast<juce::Label*> (c))
                    caption = l;
            expect (caption != nullptr);
            if (caption == nullptr)
                return;
            expectEquals (caption->getText(), juce::String ("Strength"));
            expect (caption->getBounds() == juce::Rectangle<int> (0, 58, 62, 12));
            expect (panel->getLocalBounds().contains (strength->getBounds()));
            expect (panel->getLocalBounds().contains (caption->getBounds()));
            expect (! strength->getBounds().intersects (caption->getBounds()));
        }

        beginTest ("SnsPanel readout formats the floor with one decimal");
        {
            NoiseSuppressionProcessor p;
            ui::SnsPanel panel (p.apvts, [] { return -62.4f; });
            expectEquals (panel.readout(), juce::String ("Floor -62.4"));
            ui::SnsPanel quiet (p.apvts, [] { return -90.0f; });
            expectEquals (quiet.readout(), juce::String ("Floor -90.0"));
        }

        beginTest ("SuppressKnob is flipped: -40 dB = proportion 1, -5 dB = 0, arc grows with strength");
        {
            ui::Theme theme;
            ui::SuppressKnob k;
            k.setLookAndFeel (&theme);
            k.setRange (-40.0, -5.0, 1.0);
            k.setSize (62, 58);
            k.setValue (-40.0, juce::dontSendNotification);
            expectWithinAbsoluteError (k.valueToProportionOfLength (k.getValue()), 1.0, 1.0e-9);
            expectGreaterThan (accentPixels (k), 100);
            k.setValue (-5.0, juce::dontSendNotification);
            expectWithinAbsoluteError (k.valueToProportionOfLength (k.getValue()), 0.0, 1.0e-9);
            expectEquals (accentPixels (k), 0);
            expectWithinAbsoluteError (k.proportionOfLengthToValue (1.0), -40.0, 1.0e-9);
            expectWithinAbsoluteError (k.proportionOfLengthToValue (0.0), -5.0, 1.0e-9);
            expectWithinAbsoluteError (k.proportionOfLengthToValue (k.valueToProportionOfLength (-23.0)), -23.0, 1.0e-9);
            expectEquals (k.getTextFromValue (-20.0), juce::String ("-20 dB"));
            expectWithinAbsoluteError (k.getValueFromText ("-17 dB"), -17.0, 1.0e-9);
            k.setValue (-20.0, juce::dontSendNotification);
            k.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
            expectWithinAbsoluteError (k.getValue(), -21.0, 1.0e-9); // Up = stronger = lower dB
            k.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
            expectWithinAbsoluteError (k.getValue(), -20.0, 1.0e-9);
            k.setLookAndFeel (nullptr);
        }

        beginTest ("snapshot per engine (Tests/render/ui-*.png, inspected by eye)");
        {
            NoiseSuppressionProcessor p;
            prepare (p);
            NoiseSuppressionEditor e (p);
            const auto dir = test::testsDir().getChildFile ("render");
            expect (dir.createDirectory().wasOk());
            const std::pair<int, const char*> shots[] = { { kEngineSns, "ui-sns.png" }, { kEngineClassic, "ui-classic.png" },
                                                          { kEngineWebRtc, "ui-webrtc.png" }, { kEngineRnnoise, "ui-rnnoise.png" }, { kEngineDfn, "ui-dfn.png" } };
            for (auto& [engine, fileName] : shots)
            {
                setEngine (p, engine);
                e.timerCallback();
                const auto img = e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f);
                expectEquals (img.getWidth(), 440);
                expectEquals (img.getHeight(), 140);
                const auto file = dir.getChildFile (fileName);
                file.deleteFile();
                juce::FileOutputStream os (file);
                expect (os.openedOk(), file.getFullPathName());
                expect (juce::PNGImageFormat().writeImageToStream (img, os));
                logMessage ("    wrote " + file.getFullPathName());
            }
        }
    }
};

static UiEditorTests uiEditorTests;
} // namespace nsp
