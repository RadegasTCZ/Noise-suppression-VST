#include <JuceHeader.h>
#include "../Source/Host/StateMigration.h"
#include "../Source/PluginProcessor.h"

namespace
{
const char* const allIds[] = { nsp::ids::engine, nsp::ids::bypass,
                               nsp::ids::monitorResidual, nsp::ids::classicSuppress, nsp::ids::snsSuppress,
                               nsp::ids::webrtcLevel, nsp::ids::rnnModel, nsp::ids::dfnStrength, nsp::ids::limiter };

void setParam (NoiseSuppressionProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

float raw (NoiseSuppressionProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

float valueOf (const juce::ValueTree& tree, const char* id)
{
    return (float) tree.getChildWithProperty ("id", id).getProperty ("value");
}

juce::MemoryBlock binaryFromXml (const juce::String& xmlText)
{
    juce::MemoryBlock block;

    if (auto xml = juce::parseXML (xmlText))
        juce::AudioProcessor::copyXmlToBinary (*xml, block);

    return block;
}

void load (NoiseSuppressionProcessor& p, const juce::MemoryBlock& block)
{
    p.setStateInformation (block.getData(), (int) block.getSize());
}

/** v1.1.2 session state. HAND-WRITTEN in the exact format v1.1.2's getStateInformation produced
    (APVTS root "Parameters", one PARAM per parameter with its denormalised value, binary-wrapped by
    copyXmlToBinary): v1.1.2 can no longer be built against JUCE 9, so this was not captured from a
    running v1.1.2. Parameter set = v1.1.2 createParameterLayout(). */
juce::String v112Xml (bool enableDenoise, float noiseSuppress)
{
    return juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Parameters>\n")
         + "  <PARAM id=\"enableDenoise\" value=\"" + (enableDenoise ? "1.0" : "0.0") + "\"/>\n"
         + "  <PARAM id=\"enableVAD\" value=\"0.0\"/>\n"
         + "  <PARAM id=\"enableLevelGate\" value=\"0.0\"/>\n"
         + "  <PARAM id=\"noisesuppress\" value=\"" + juce::String (noiseSuppress, 1) + "\"/>\n"
         + "  <PARAM id=\"probstart\" value=\"70.0\"/>\n"
         + "  <PARAM id=\"probcontinue\" value=\"70.0\"/>\n"
         + "  <PARAM id=\"vadThreshold\" value=\"-40.0\"/>\n"
         + "  <PARAM id=\"gateFloor\" value=\"-20.0\"/>\n"
         + "  <PARAM id=\"gateAttackMs\" value=\"2.0\"/>\n"
         + "  <PARAM id=\"gateReleaseMs\" value=\"80.0\"/>\n"
         + "</Parameters>\n";
}
} // namespace

class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("State", "State") {}

    void runTest() override
    {
        testRoundTrip();
        testV112();
        testV112DenoiseOff();
        testFutureEngine();
        testNonFiniteValues();
        testStep3Session();
        testParameterSet();
        testDfnParameters();
        testLimiterParameter();
        testStep4Session();
        testMigrateDirect();
        testLegacyMix();
        testGarbage();
    }

private:
    void testRoundTrip()
    {
        beginTest ("random parameter values survive get/set state on a second instance");

        juce::Random rng (1234);

        for (int iter = 0; iter < 20; ++iter)
        {
            NoiseSuppressionProcessor a, b;
            setParam (a, nsp::ids::engine, (float) rng.nextInt (nsp::engineNames().size()));
            setParam (a, nsp::ids::bypass, rng.nextBool() ? 1.0f : 0.0f);
            setParam (a, nsp::ids::monitorResidual, rng.nextBool() ? 1.0f : 0.0f);
            setParam (a, nsp::ids::classicSuppress, (float) (-40 + rng.nextInt (36)));
            setParam (a, nsp::ids::snsSuppress, (float) (-40 + rng.nextInt (36)));
            setParam (a, nsp::ids::webrtcLevel, (float) rng.nextInt (4));
            setParam (a, nsp::ids::rnnModel, (float) rng.nextInt (2));
            setParam (a, nsp::ids::dfnStrength, (float) rng.nextInt (3));
            setParam (a, nsp::ids::limiter, rng.nextBool() ? 1.0f : 0.0f);

            juce::MemoryBlock block;
            a.getStateInformation (block);
            load (b, block);

            for (auto* id : allIds)
                expectWithinAbsoluteError (raw (b, id), raw (a, id), 1.0e-4f, juce::String ("iteration ") + juce::String (iter) + " id " + id);

            const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            expect (xml != nullptr && xml->hasTagName (nsp::kStateType));
            if (xml != nullptr)
                expectEquals (xml->getIntAttribute (nsp::kStateVersionProperty), nsp::kStateVersion);
        }
    }

    void testV112()
    {
        beginTest ("v1.1.2 session loads as Classic, classicSuppress = noisesuppress, bypass off, rest default");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::monitorResidual, 1.0f);   // must be reset to default by the migration
        setParam (p, nsp::ids::limiter, 0.0f);           // v1.1.2 had no limiter: gets the default (on)
        load (p, binaryFromXml (v112Xml (true, -30.0f)));

        expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineClassic);
        expectEquals (raw (p, nsp::ids::classicSuppress), -30.0f);
        expectEquals (raw (p, nsp::ids::bypass), 0.0f);
        expectEquals (raw (p, nsp::ids::monitorResidual), 0.0f);
        expectEquals (raw (p, nsp::ids::snsSuppress), -20.0f);
        expectEquals (raw (p, nsp::ids::limiter), 1.0f);

        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        expect (xml != nullptr);
        if (xml != nullptr)
        {
            expectEquals (xml->getIntAttribute (nsp::kStateVersionProperty), nsp::kStateVersion);
            expect (xml->getChildByAttribute ("id", "enableVAD") == nullptr, "v1.1.2-only parameters are dropped");
        }
    }

    void testV112DenoiseOff()
    {
        beginTest ("v1.1.2 session with enableDenoise = 0 loads with bypass on");

        NoiseSuppressionProcessor p;
        load (p, binaryFromXml (v112Xml (false, -12.0f)));
        expectEquals (raw (p, nsp::ids::bypass), 1.0f);
        expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineClassic);
        expectEquals (raw (p, nsp::ids::classicSuppress), -12.0f);
    }

    void testFutureEngine()
    {
        beginTest ("unknown engine index from a future build (>= 5) loads as SNS");

        for (const char* engineValue : { "5.0", "7.0", "-1.0" })
        {
            NoiseSuppressionProcessor p;
            setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic);

            const juce::String xml = juce::String ("<Parameters stateVersion=\"2\">")
                + "<PARAM id=\"engine\" value=\"" + engineValue + "\"/>"
                + "<PARAM id=\"classicSuppress\" value=\"-12.0\"/>"
                + "<PARAM id=\"futureParam\" value=\"2.0\"/>"   // unknown future parameter: ignored
                + "</Parameters>";
            load (p, binaryFromXml (xml));

            expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineSns, juce::String ("engine value ") + engineValue);
            expectEquals (raw (p, nsp::ids::classicSuppress), -12.0f);
        }
    }

    void testNonFiniteValues()
    {
        beginTest ("non-finite saved values (nan / inf) load as the parameter default");

        for (const char* bad : { "nan", "inf", "-inf" })
        {
            NoiseSuppressionProcessor p;
            const juce::String xml = juce::String ("<Parameters stateVersion=\"2\">")
                + "<PARAM id=\"engine\" value=\"1.0\"/>"
                + "<PARAM id=\"snsSuppress\" value=\"" + bad + "\"/>"
                + "<PARAM id=\"classicSuppress\" value=\"" + bad + "\"/>"
                + "</Parameters>";
            load (p, binaryFromXml (xml));

            expectEquals (raw (p, nsp::ids::snsSuppress), -20.0f, juce::String ("snsSuppress ") + bad);
            expectEquals (raw (p, nsp::ids::classicSuppress), -20.0f, juce::String ("classicSuppress ") + bad);
            expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineSns);
        }
    }

    void testStep3Session()
    {
        beginTest ("a stateVersion 2 session (before WebRTC NS / RNNoise: no webrtcLevel / rnnModel) loads with their defaults");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::webrtcLevel, 3.0f);   // must be reset to the default by the load
        setParam (p, nsp::ids::rnnModel, 1.0f);

        // exactly what the stateVersion 2 getStateInformation wrote: its six parameters, values denormalised
        const juce::String xml = juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
            + "<Parameters stateVersion=\"2\">"
            + "<PARAM id=\"bypass\" value=\"0.0\"/>"
            + "<PARAM id=\"classicSuppress\" value=\"-27.0\"/>"
            + "<PARAM id=\"engine\" value=\"0.0\"/>"
            + "<PARAM id=\"mix\" value=\"80.0\"/>"
            + "<PARAM id=\"monitorResidual\" value=\"0.0\"/>"
            + "<PARAM id=\"snsSuppress\" value=\"-33.0\"/>"
            + "</Parameters>";
        load (p, binaryFromXml (xml));

        expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineClassic);
        expectEquals (raw (p, nsp::ids::classicSuppress), -27.0f);
        expectEquals (raw (p, nsp::ids::snsSuppress), -33.0f);
        expectEquals (raw (p, nsp::ids::webrtcLevel), 1.0f);   // 12 dB
        expectEquals (raw (p, nsp::ids::rnnModel), 0.0f);      // Standard
    }

    void testParameterSet()
    {
        beginTest ("WebRTC NS / RNNoise parameters: engine choices appended, webrtcLevel 6/12/18/21 dB default 12, rnnModel Standard/Light default Standard");

        const juce::StringArray engines { "Classic Speex", "SNS v2", "WebRTC NS", "RNNoise", "DeepFilterNet" };
        expect (nsp::engineNames() == engines);
        expectEquals ((int) nsp::kEngineWebRtc, 2);
        expectEquals ((int) nsp::kEngineRnnoise, 3);

        NoiseSuppressionProcessor p;
        auto* level = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (nsp::ids::webrtcLevel));
        auto* model = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (nsp::ids::rnnModel));
        auto* engine = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (nsp::ids::engine));
        expect (level != nullptr && model != nullptr && engine != nullptr);
        if (level == nullptr || model == nullptr || engine == nullptr)
            return;

        expect (level->choices == juce::StringArray { "6 dB", "12 dB", "18 dB", "21 dB" });
        expectEquals (level->getIndex(), 1);
        expectEquals (level->getVersionHint(), nsp::kParamVersion);
        expect (model->choices == juce::StringArray { "Standard", "Light" });
        expectEquals (model->getIndex(), 0);
        expectEquals (model->getVersionHint(), nsp::kParamVersion);
        expect (engine->choices == engines);
        expectEquals (engine->getIndex(), (int) nsp::kEngineSns);
    }

    void testDfnParameters()
    {
        beginTest ("DeepFilterNet parameters: engine choice 4 'DeepFilterNet' appended, dfnStrength Gentle/Normal/Strong default Normal");

        expectEquals ((int) nsp::kEngineDfn, 4);
        expectEquals (nsp::engineNames()[4], juce::String ("DeepFilterNet"));
        expect (nsp::dfnStrengthNames() == juce::StringArray { "Gentle", "Normal", "Strong" });

        NoiseSuppressionProcessor p;
        auto* strength = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (nsp::ids::dfnStrength));
        expect (strength != nullptr);
        if (strength == nullptr)
            return;

        expect (strength->choices == juce::StringArray { "Gentle", "Normal", "Strong" });
        expectEquals (strength->getIndex(), 1);
        expectEquals (strength->getVersionHint(), nsp::kParamVersion);
        expectEquals (strength->getName (64), juce::String ("DeepFilterNet Strength"));
    }

    void testStep4Session()
    {
        beginTest ("a step 4 session (no dfnStrength) loads with Normal");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::dfnStrength, 2.0f);   // must be reset to the default by the load
        setParam (p, nsp::ids::limiter, 0.0f);       // a v2 state without limiter gets the default (on)

        const juce::String xml = juce::String ("<Parameters stateVersion=\"2\">")
            + "<PARAM id=\"engine\" value=\"3.0\"/>"
            + "<PARAM id=\"rnnModel\" value=\"1.0\"/>"
            + "<PARAM id=\"webrtcLevel\" value=\"2.0\"/>"
            + "</Parameters>";
        load (p, binaryFromXml (xml));

        expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineRnnoise);
        expectEquals (raw (p, nsp::ids::rnnModel), 1.0f);
        expectEquals (raw (p, nsp::ids::dfnStrength), 1.0f);   // Normal
        expectEquals (raw (p, nsp::ids::limiter), 1.0f);       // on
    }

    void testLimiterParameter()
    {
        beginTest ("limiter parameter: bool 'Output Limiter', default on, appended after dfnStrength");

        NoiseSuppressionProcessor p;
        auto* limiter = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter (nsp::ids::limiter));
        expect (limiter != nullptr);
        if (limiter == nullptr)
            return;

        expectEquals (juce::String (nsp::ids::limiter), juce::String ("limiter"));
        expect (limiter->get());
        expectEquals (static_cast<juce::AudioProcessorParameter*> (limiter)->getDefaultValue(), 1.0f);
        expectEquals (limiter->getName (64), juce::String ("Output Limiter"));
        expectEquals (limiter->getVersionHint(), nsp::kParamVersion);

        const auto& params = p.getParameters();
        expectEquals (params.size(), 9);
        auto* last = dynamic_cast<juce::AudioProcessorParameterWithID*> (params.getLast());
        auto* beforeLast = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[params.size() - 2]);
        expect (last != nullptr && last->paramID == nsp::ids::limiter);
        expect (beforeLast != nullptr && beforeLast->paramID == nsp::ids::dfnStrength);
    }

    void testMigrateDirect()
    {
        beginTest ("migrateState: v2 values pass through, missing parameters get defaults");

        juce::ValueTree v2 (nsp::kStateType);
        v2.setProperty (nsp::kStateVersionProperty, 2, nullptr);
        juce::ValueTree engine ("PARAM");
        engine.setProperty ("id", nsp::ids::engine, nullptr);
        engine.setProperty ("value", 0.0, nullptr);
        v2.appendChild (engine, nullptr);
        juce::ValueTree classic ("PARAM");
        classic.setProperty ("id", nsp::ids::classicSuppress, nullptr);
        classic.setProperty ("value", -42.5, nullptr);
        v2.appendChild (classic, nullptr);

        const auto m = nsp::migrateState (v2);
        expect (m.isValid() && m.hasType (nsp::kStateType));
        expectEquals ((int) m.getProperty (nsp::kStateVersionProperty), nsp::kStateVersion);
        expectEquals (m.getNumChildren(), 9);
        expectEquals (valueOf (m, nsp::ids::engine), 0.0f);
        expectEquals (valueOf (m, nsp::ids::classicSuppress), -42.5f);
        expectEquals (valueOf (m, nsp::ids::bypass), 0.0f);
        expectEquals (valueOf (m, nsp::ids::monitorResidual), 0.0f);
        expectEquals (valueOf (m, nsp::ids::snsSuppress), -20.0f);
        expectEquals (valueOf (m, nsp::ids::webrtcLevel), 1.0f);
        expectEquals (valueOf (m, nsp::ids::rnnModel), 0.0f);
        expectEquals (valueOf (m, nsp::ids::dfnStrength), 1.0f);
        expectEquals (valueOf (m, nsp::ids::limiter), 1.0f);

        beginTest ("migrateState: v1.1.2 tree");
        const auto v1 = juce::ValueTree::fromXml (v112Xml (true, -30.0f));
        const auto m1 = nsp::migrateState (v1);
        expectEquals (valueOf (m1, nsp::ids::engine), (float) nsp::kEngineClassic);
        expectEquals (valueOf (m1, nsp::ids::classicSuppress), -30.0f);
        expectEquals (valueOf (m1, nsp::ids::bypass), 0.0f);
        expectEquals (valueOf (m1, nsp::ids::limiter), 1.0f);
        expect (! m1.getChildWithProperty ("id", "noisesuppress").isValid());

        beginTest ("migrateState: foreign or invalid trees are rejected");
        expect (! nsp::migrateState (juce::ValueTree()).isValid());
        expect (! nsp::migrateState (juce::ValueTree ("SomethingElse")).isValid());
    }

    void testLegacyMix()
    {
        beginTest ("a saved state containing PARAM mix loads without error and is ignored");

        NoiseSuppressionProcessor p;
        const juce::String xml = juce::String ("<Parameters stateVersion=\"2\">")
            + "<PARAM id=\"engine\" value=\"3.0\"/>"
            + "<PARAM id=\"mix\" value=\"42.5\"/>"
            + "<PARAM id=\"classicSuppress\" value=\"-27.0\"/>"
            + "</Parameters>";
        load (p, binaryFromXml (xml));

        expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineRnnoise);
        expectEquals (raw (p, nsp::ids::classicSuppress), -27.0f);
        expectEquals (p.getParameters().size(), 9);   // no mix parameter any more
        expect (p.apvts.getParameter ("mix") == nullptr);

        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        const auto out = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        expect (out != nullptr && out->getChildByAttribute ("id", "mix") == nullptr, "mix is not written back");
    }

    void testGarbage()
    {
        beginTest ("garbage, truncated and foreign state leave parameters unchanged, no crash");

        NoiseSuppressionProcessor p;
        setParam (p, nsp::ids::engine, (float) nsp::kEngineClassic);
        setParam (p, nsp::ids::classicSuppress, -11.0f);

        auto unchanged = [&] (const juce::String& what)
        {
            expectEquals (raw (p, nsp::ids::engine), (float) nsp::kEngineClassic, what);
            expectEquals (raw (p, nsp::ids::classicSuppress), -11.0f, what);
        };

        const char garbage[] = "this is not a plugin state at all";
        p.setStateInformation (garbage, (int) sizeof (garbage));
        unchanged ("garbage bytes");

        p.setStateInformation (nullptr, 0);
        unchanged ("empty");

        NoiseSuppressionProcessor other;
        setParam (other, nsp::ids::classicSuppress, -5.0f);
        juce::MemoryBlock full;
        other.getStateInformation (full);
        p.setStateInformation (full.getData(), (int) full.getSize() / 2);
        unchanged ("truncated");

        load (p, binaryFromXml ("<SomethingElse a=\"1\"/>"));
        unchanged ("foreign xml");
    }
};

static StateTests stateTests;
