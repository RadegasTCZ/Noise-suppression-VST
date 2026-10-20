#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/UI/Theme.h"
#include <cstdlib>

namespace nsp
{
class UiThemeTests final : public juce::UnitTest
{
public:
    UiThemeTests() : juce::UnitTest ("Theme", "UiTheme") {}

    static int accentPixels (juce::Component& c)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto p = img.getPixelAt (x, y);
                if (std::abs (p.getRed() - 0xf0) + std::abs (p.getGreen() - 0xa0) + std::abs (p.getBlue() - 0x50) <= 30
                    && p.getAlpha() > 200)
                    ++n;
            }
        return n;
    }

    static bool near (juce::Colour a, juce::Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
             + std::abs (a.getBlue() - b.getBlue()) <= 12;
    }

    void runTest() override
    {
        beginTest ("colours are the theme's exact values");
        {
            expect (ui::colours::bg     == juce::Colour (0xff16181b));
            expect (ui::colours::panel  == juce::Colour (0xff1f2226));
            expect (ui::colours::well   == juce::Colour (0xff111316));
            expect (ui::colours::line   == juce::Colour (0xff2d3137));
            expect (ui::colours::text   == juce::Colour (0xffe6e9ed));
            expect (ui::colours::dim    == juce::Colour (0xff8a919a));
            expect (ui::colours::accent == juce::Colour (0xfff0a050));
            expect (ui::colours::level  == juce::Colour (0xff4ea4e6));
            expect (ui::colours::knob   == juce::Colour (0xff2a2e33));
        }

        beginTest ("rotary 0..100: 0 draws no arc, 50 half, 100 the full arc");
        {
            ui::Theme theme;
            juce::Slider s (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            s.setLookAndFeel (&theme);
            s.setRange (0.0, 100.0, 1.0);
            s.setSize (60, 60);
            s.setValue (0.0, juce::dontSendNotification);
            expectEquals (accentPixels (s), 0);
            s.setValue (50.0, juce::dontSendNotification);
            const int halfArc = accentPixels (s);
            s.setValue (100.0, juce::dontSendNotification);
            const int fullArc = accentPixels (s);
            expectGreaterThan (halfArc, 50);
            expectGreaterThan (fullArc, halfArc + 50);
            s.setLookAndFeel (nullptr);
        }

        beginTest ("toggle pill: on = accent with the knob right, off = line colour with the knob left");
        {
            ui::Theme theme;
            juce::ToggleButton t ("Bypass");
            t.setLookAndFeel (&theme);
            t.setSize (76, 20);
            t.setToggleState (true, juce::dontSendNotification);
            auto img = t.createComponentSnapshot (t.getLocalBounds(), true, 1.0f);
            expect (near (img.getPixelAt (5, 10), ui::colours::accent), "on: pill is accent");
            expect (near (img.getPixelAt (19, 10), juce::Colours::white), "on: knob at the right");
            t.setToggleState (false, juce::dontSendNotification);
            img = t.createComponentSnapshot (t.getLocalBounds(), true, 1.0f);
            expect (near (img.getPixelAt (20, 10), ui::colours::line), "off: pill is the line colour");
            expect (near (img.getPixelAt (7, 10), juce::Colours::white), "off: knob at the left");
            t.setLookAndFeel (nullptr);
        }
    }
};

static UiThemeTests uiThemeTests;
} // namespace nsp
