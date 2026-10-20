#include "PluginEditor.h"
#include "Params.h"
#include "UI/ClassicPanel.h"
#include "UI/RnnoisePanel.h"
#include "UI/DfnPanel.h"
#include "UI/SnsPanel.h"
#include "UI/WebRtcPanel.h"
#include "UI/SuppressKnob.h"

namespace
{
juce::Font titleFont()
{
    // design: 11 px, weight 600, letter-spacing .08em -> kerning factor 0.08 (FontOptions::withKerningFactor)
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), 11.0f, juce::Font::bold)
                           .withKerningFactor (0.08f));
}

double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

NoiseSuppressionEditor::NoiseSuppressionEditor (NoiseSuppressionProcessor& p)
    : juce::AudioProcessorEditor (p),
      proc (p),
      engineParam (p.apvts.getRawParameterValue (nsp::ids::engine)),
      meters (p.inputPeak, p.outputPeak)
{
    using Apvts = juce::AudioProcessorValueTreeState;

    engineBox.addItemList (nsp::engineNames(), 1);           // item id = choice index + 1; items before the attachment
    engineBox.setComponentID (nsp::ids::engine);
    addAndMakeVisible (engineBox);
    engineAttachment = std::make_unique<Apvts::ComboBoxAttachment> (p.apvts, nsp::ids::engine, engineBox);

    bypassButton.setComponentID (nsp::ids::bypass);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<Apvts::ButtonAttachment> (p.apvts, nsp::ids::bypass, bypassButton);

    monitorButton.setComponentID (nsp::ids::monitorResidual);
    addAndMakeVisible (monitorButton);
    monitorAttachment = std::make_unique<Apvts::ButtonAttachment> (p.apvts, nsp::ids::monitorResidual, monitorButton);

    limiterButton.setComponentID (nsp::ids::limiter);
    addAndMakeVisible (limiterButton);
    limiterAttachment = std::make_unique<Apvts::ButtonAttachment> (p.apvts, nsp::ids::limiter, limiterButton);

    panels.push_back (std::make_unique<nsp::ui::ClassicPanel> (p.apvts));                                     // kEngineClassic
    panels.push_back (std::make_unique<nsp::ui::SnsPanel> (p.apvts, [&p] { return p.getSnsNoiseFloorDb(); })); // kEngineSns
    panels.push_back (std::make_unique<nsp::ui::WebRtcPanel> (p.apvts));                                      // kEngineWebRtc
    panels.push_back (std::make_unique<nsp::ui::RnnoisePanel> (p.apvts, [&p] (int model) { return p.isRnnoiseModelFailed (model); })); // kEngineRnnoise
    panels.push_back (std::make_unique<nsp::ui::DfnPanel> (p.apvts, [&p] { return p.isDfnModelFailed(); })); // kEngineDfn
    jassert ((int) panels.size() == nsp::engineNames().size());
    for (auto& panel : panels)
        addChildComponent (*panel);

    addAndMakeVisible (meters);

    // last: a Slider rebuilds its value box only on a look-and-feel change, so every child must exist first
    setLookAndFeel (&theme);

    setResizable (false, false);
    setSize (kWidth, kHeight);             // the only setSize call (fixed size, no resizing)
    lastTickSeconds = nowSeconds();
    timerCallback();                       // active panel, readout and title state before the first paint
    startTimerHz (30);
}

NoiseSuppressionEditor::~NoiseSuppressionEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::String NoiseSuppressionEditor::titleText() const
{
    return rateSupported ? juce::String ("NOISE SUPPRESSION") : juce::String ("Unsupported sample rate");
}

void NoiseSuppressionEditor::timerCallback()
{
    const double now = nowSeconds();
    meters.tick ((float) juce::jlimit (0.0, 0.1, now - lastTickSeconds));
    lastTickSeconds = now;

    const int engine = juce::jlimit (0, (int) panels.size() - 1, juce::roundToInt (engineParam->load()));
    if (engine != shownEngine)
    {
        for (size_t i = 0; i < panels.size(); ++i)
            panels[i]->setVisible ((int) i == engine);
        shownEngine = engine;
    }

    const auto text = panels[(size_t) engine]->readout();
    const bool warning = panels[(size_t) engine]->readoutIsWarning();
    if (text != readout || warning != readoutWarning)
    {
        readout = text;
        readoutWarning = warning;
        repaint (readoutArea);
    }

    const bool supported = proc.isSampleRateSupported();
    if (supported != rateSupported)
    {
        rateSupported = supported;
        repaint (titleArea);
    }
}

void NoiseSuppressionEditor::resized()
{
    using nsp::ui::EnginePanel;
    auto area = getLocalBounds().reduced (kPad);                                   // 8, 8, 424, 124
    auto header = area.removeFromTop (kHeaderHeight);                              // 8, 8, 424, 28
    titleArea = header.removeFromLeft (kTitleWidth);                               // 8, 8, 140, 28
    header.removeFromLeft (kGapX);
    bypassButton.setBounds (header.removeFromRight (kBypassWidth));                // 356, 8, 76, 28
    header.removeFromRight (kGapX);
    engineBox.setBounds (header.withSizeKeepingCentre (header.getWidth(), kComboHeight)); // 158, 10, 188, 24

    area.removeFromTop (kGapY);
    boxArea = area;                                                                // 8, 44, 424, 88
    auto inner = boxArea.reduced (1).reduced (kBoxPadX, kBoxPadY);                 // 19, 53, 402, 70

    const auto slot = inner.removeFromLeft (EnginePanel::kWidth);                  // 19, 53, 62, 70
    for (auto& panel : panels)
        panel->setBounds (slot);
    inner.removeFromLeft (kGapX);

    limiterButton.setBounds (inner.removeFromLeft (EnginePanel::kWidth));          // 91, 53, 62, 70: switch + caption
    inner.removeFromLeft (kGapX);

    separatorArea = inner.removeFromLeft (1);                                      // 163, 53, 1, 70
    inner.removeFromLeft (kGapX);                                                  // 174, 53, 247, 70

    auto right = inner.reduced (0, 4);                                             // 174, 57, 247, 62
    meters.setBounds (right.removeFromTop (nsp::ui::MeterView::kHeight));          // 174, 57, 247, 37
    right.removeFromTop (5);
    monitorButton.setBounds (right.removeFromLeft (kMonitorWidth));                // 174, 99, 130, 20
    readoutArea = right;                                                           // 304, 99, 117, 20
}

void NoiseSuppressionEditor::paint (juce::Graphics& g)
{
    using namespace nsp::ui;
    g.fillAll (colours::bg);

    g.setColour (rateSupported ? colours::text : colours::warn);
    g.setFont (titleFont());
    g.drawFittedText (titleText(), titleArea, juce::Justification::centredLeft, 1, 0.8f);

    const auto box = boxArea.toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.fillRect (separatorArea);

    if (readout.isNotEmpty() && readoutWarning)
    {
        // e.g. "Model failed to load": whole text in the warn colour, squeezed (min 80 %) into the 117 px area
        g.setColour (colours::warn);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (readout, readoutArea, juce::Justification::centredRight, 1, 0.8f);
    }
    else if (readout.isNotEmpty())
    {
        // "Floor -62.4": label dim, value in the text colour
        const int split = readout.lastIndexOfChar (' ') + 1;
        juce::AttributedString s;
        s.append (readout.substring (0, split), monoFont (11.0f), colours::dim);
        s.append (readout.substring (split), monoFont (11.0f), colours::text);
        s.setJustification (juce::Justification::centredRight);
        s.setWordWrap (juce::AttributedString::none);
        s.draw (g, readoutArea.toFloat());
    }
}
