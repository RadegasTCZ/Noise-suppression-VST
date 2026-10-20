#pragma once

#include <JuceHeader.h>

namespace nsp
{
/** Turns any loaded state tree into a complete v2 tree for apvts.replaceState.
    Returns an invalid tree if `loaded` is not a "Parameters" tree (caller keeps current state).
    - stateVersion present: known parameter values copied, missing ones set to their defaults.
    - stateVersion absent (v1.1.2): engine = Classic, classicSuppress = noisesuppress (clamped -40..-5),
      bypass = !enableDenoise, everything else default; v1.1.2-only parameters dropped.
    - engine index outside [0, engineNames().size()) -> SNS.
    Every parameter is always written: APVTS::replaceState leaves absent parameters unchanged. */
juce::ValueTree migrateState (const juce::ValueTree& loaded);
} // namespace nsp
