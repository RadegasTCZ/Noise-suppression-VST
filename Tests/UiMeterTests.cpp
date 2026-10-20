#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/UI/MeterBallistics.h"
#include "../Source/UI/MeterView.h"
#include <cmath>
#include <limits>

namespace nsp
{
class UiMeterTests final : public juce::UnitTest
{
public:
    UiMeterTests() : juce::UnitTest ("Meter ballistics and MeterView", "UiMeters") {}

    void runTest() override
    {
        constexpr float frame = 1.0f / 30.0f;   // the editor's 30 Hz timer

        beginTest ("ballistics start at the floor; attack is instant");
        {
            MeterBallistics b;
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (-6.0f, frame);
            expectEquals (b.barDb(), -6.0f);
            expectEquals (b.holdDb(), -6.0f);
            for (int i = 0; i < 10; ++i) b.update (-100.0f, frame);   // bar falling
            b.update (-3.0f, frame);                                    // a louder peak shows at once
            expectEquals (b.barDb(), -3.0f);
            expectEquals (b.holdDb(), -3.0f);
        }

        beginTest ("bar falls 12 dB/s: 1 s after a -10 dB peak it shows -22 dB");
        {
            MeterBallistics b;
            b.update (-10.0f, frame);
            for (int i = 0; i < 30; ++i) b.update (-100.0f, frame);
            expectWithinAbsoluteError (b.barDb(), -22.0f, 1.0e-3f);
        }

        beginTest ("hold tick holds 1.5 s, then falls 20 dB/s, never below the bar");
        {
            MeterBallistics b;
            b.update (-10.0f, frame);
            int i = 0;
            for (; i < 44; ++i) b.update (-100.0f, frame);     // 1.467 s: still holding
            expectEquals (b.holdDb(), -10.0f);
            for (; i < 75; ++i) b.update (-100.0f, frame);     // 2.5 s: 1.0 s of tick fall
            expectWithinAbsoluteError (b.holdDb(), -30.0f, 0.01f);
            expectWithinAbsoluteError (b.barDb(), -40.0f, 0.01f);
            for (; i < 90; ++i) b.update (-100.0f, frame);     // 3.0 s
            expectWithinAbsoluteError (b.holdDb(), -40.0f, 0.01f);
            expectWithinAbsoluteError (b.barDb(), -46.0f, 0.01f);
            for (; i < 120; ++i) b.update (-100.0f, frame);    // 4.0 s: the tick caught the bar at 3.75 s
            expectWithinAbsoluteError (b.barDb(), -58.0f, 0.01f);
            expectWithinAbsoluteError (b.holdDb(), b.barDb(), 1.0e-4f);
        }

        beginTest ("floor clamp: nothing below -60 dB, silence settles exactly on the floor");
        {
            MeterBallistics b;
            b.update (-200.0f, frame);
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (std::numeric_limits<float>::quiet_NaN(), frame);
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            b.update (-10.0f, frame);
            for (int i = 0; i < 300; ++i) b.update (-100.0f, frame);   // 10 s
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (-5.0f, frame);
            b.reset();
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
        }

        beginTest ("MeterView: silence from the start never repaints");
        {
            PeakHold in, out;
            ui::MeterView mv (in, out);
            mv.setSize (247, ui::MeterView::kHeight);
            for (int i = 0; i < 10; ++i) mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), 0);
            expectEquals (mv.numberText (0), juce::String ("-inf"));
            expectEquals (mv.numberText (1), juce::String ("-inf"));
        }

        beginTest ("MeterView: a peak repaints; sub-pixel changes and a settled meter do not");
        {
            PeakHold in, out;
            ui::MeterView mv (in, out);
            mv.setSize (247, ui::MeterView::kHeight);
            const float half = 0.5f;                       // -6.02 dBFS
            in.push (&half, 1);
            mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), 1);
            expectEquals (mv.numberText (0), juce::String ("-6.0"));
            expectEquals (mv.numberText (1), juce::String ("-inf"));
            expectEquals (mv.ballistics (0).barDb(), 20.0f * std::log10 (0.5f));

            mv.tick (1.0e-4f);                             // bar moves 0.0012 dB: same pixel, same text
            expectEquals (mv.getRepaintRequestCount(), 1);

            for (int i = 0; i < 400; ++i) mv.tick (frame); // 13 s: bar and tick back on the floor
            expectGreaterThan (mv.getRepaintRequestCount(), 10);
            expectEquals (mv.numberText (0), juce::String ("-inf"));
            const int settled = mv.getRepaintRequestCount();
            mv.tick (frame);
            mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), settled);
        }
    }
};

static UiMeterTests uiMeterTests;
} // namespace nsp
