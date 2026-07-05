#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>

TEST_CASE ("one is equal to one", "[dummy]")
{
    REQUIRE (1 == 1);
}

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor testPlugin;

    SECTION ("name")
    {
        CHECK_THAT (testPlugin.getName().toStdString(),
            Catch::Matchers::Equals ("Kintsugi"));
    }
}

TEST_CASE ("Freeze processing stays finite and bounded", "[dsp]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);

    auto& apvts = plugin.getAPVTS();
    apvts.getParameter (KintsugiParams::freezeAmountID)->setValueNotifyingHost (1.0f);
    apvts.getParameter (KintsugiParams::shimmerID)->setValueNotifyingHost (0.7f);
    apvts.getParameter (KintsugiParams::decayID)->setValueNotifyingHost (0.8f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    const auto fillWithSine = [] (juce::AudioBuffer<float>& b, double& phase)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            double p = phase;
            auto* data = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                data[i] = 0.5f * (float) std::sin (p);
                p += juce::MathConstants<double>::twoPi * 440.0 / 44100.0;
            }
        }
        phase += juce::MathConstants<double>::twoPi * 440.0 / 44100.0 * b.getNumSamples();
    };

    double phase = 0.0;

    // Warm up a few blocks so the STFT ring has real content, then freeze.
    for (int block = 0; block < 8; ++block)
    {
        fillWithSine (buffer, phase);
        plugin.processBlock (buffer, midi);
    }

    plugin.triggerFreeze();

    bool sawNonZero = false;
    for (int block = 0; block < 40; ++block)
    {
        fillWithSine (buffer, phase);
        plugin.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                REQUIRE (std::isfinite (data[i]));
                REQUIRE (std::abs (data[i]) < 10.0f);
                if (std::abs (data[i]) > 1.0e-6f)
                    sawNonZero = true;
            }
        }
    }

    CHECK (sawNonZero);
}


#ifdef PAMPLEJUCE_IPP
    #include <ipp.h>

TEST_CASE ("IPP version", "[ipp]")
{
    CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2022.2.0 (r0x42db1a66)"));
}
#endif
