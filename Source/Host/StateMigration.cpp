#include "StateMigration.h"
#include "../Params.h"

namespace nsp
{
namespace
{
    /** APVTS PARAM values are denormalised (Choice = index, Bool = 0/1, Float = own units). */
    float readValue (const juce::ValueTree& tree, const juce::String& id, float fallback)
    {
        const auto child = tree.getChildWithProperty ("id", id);

        if (child.isValid() && child.hasProperty ("value"))
        {
            const auto value = (float) child.getProperty ("value");

            if (std::isfinite (value))   // "nan" / "inf" parse, and NaN passes every range clamp
                return value;
        }

        return fallback;
    }

    void writeValue (juce::ValueTree& tree, const juce::String& id, float value)
    {
        auto child = tree.getChildWithProperty ("id", id);
        jassert (child.isValid());
        child.setProperty ("value", (double) value, nullptr);
    }
} // namespace

juce::ValueTree migrateState (const juce::ValueTree& loaded)
{
    if (! loaded.isValid() || ! loaded.hasType (kStateType))
        return {};

    const bool fromV112 = ! loaded.hasProperty (kStateVersionProperty);

    juce::ValueTree result (kStateType);
    result.setProperty (kStateVersionProperty, kStateVersion, nullptr);

    for (const auto& param : createParameters())
    {
        const float defaultValue = param->convertFrom0to1 (param->getDefaultValue());
        const float value = fromV112 ? defaultValue : readValue (loaded, param->paramID, defaultValue);

        juce::ValueTree child ("PARAM");
        child.setProperty ("id", param->paramID, nullptr);
        child.setProperty ("value", (double) value, nullptr);
        result.appendChild (child, nullptr);
    }

    if (fromV112)
    {
        writeValue (result, ids::engine, (float) kEngineClassic);
        writeValue (result, ids::classicSuppress,
                    juce::jlimit (-40.0f, -5.0f, readValue (loaded, "noisesuppress", -20.0f)));
        writeValue (result, ids::bypass, readValue (loaded, "enableDenoise", 1.0f) > 0.5f ? 0.0f : 1.0f);
    }

    const int engine = juce::roundToInt (readValue (result, ids::engine, (float) kEngineSns));

    if (engine < 0 || engine >= engineNames().size())
        writeValue (result, ids::engine, (float) kEngineSns);

    return result;
}
} // namespace nsp
