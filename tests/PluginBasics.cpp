#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <PluginEditor.h>
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

TEST_CASE ("Editor constructs without crashing", "[editor]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);

    auto editor = plugin.createEditorIfNeeded();
    REQUIRE (editor != nullptr);
    CHECK (plugin.getActiveEditor() == editor);

    // Simulate the full editor lifecycle the boot benchmark exercises.
    for (int i = 0; i < 3; ++i)
    {
        plugin.editorBeingDeleted (editor);
        delete editor;
        REQUIRE (plugin.getActiveEditor() == nullptr);
        editor = plugin.createEditorIfNeeded();
        REQUIRE (editor != nullptr);
        CHECK (plugin.getActiveEditor() == editor);
    }

    plugin.editorBeingDeleted (editor);
    delete editor;
}

TEST_CASE ("Zero freeze = clean passthrough, no buffer mutation", "[dsp][identity]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (44100.0, 512);

    // explicitly zero the freeze amount
    auto& apvts = plugin.getAPVTS();
    apvts.getParameter (KintsugiParams::freezeAmountID)->setValueNotifyingHost (0.0f);

    const int N = 8192;
    juce::AudioBuffer<float> wet (2, N);
    juce::MidiBuffer midi;

    // deterministic fill, per-channel continuous phase
    double ph0 = 0.0, ph1 = 3.14159265358979;
    for (int i = 0; i < N; ++i)
    {
        wet.setSample (0, i, 0.5f * (float) std::sin (ph0));
        wet.setSample (1, i, 0.5f * (float) std::sin (ph1));
        ph0 += 2.0 * 3.14159265358979 * 440.0 / 44100.0;
        ph1 += 2.0 * 3.14159265358979 * 880.0 / 44100.0;
    }

    // snapshot of the input
    std::vector<float> dry0 (N), dry1 (N);
    for (int i = 0; i < N; ++i) { dry0[(size_t) i] = wet.getSample (0, i); dry1[(size_t) i] = wet.getSample (1, i); }

    // process in DAW-sized blocks
    for (int offset = 0; offset < N; offset += 512)
    {
        juce::AudioBuffer<float> block (2, 512);
        block.copyFrom (0, 0, wet, 0, offset, 512);
        block.copyFrom (1, 0, wet, 1, offset, 512);
        plugin.processBlock (block, midi);
        wet.copyFrom (0, offset, block, 0, 0, 512);
        wet.copyFrom (1, offset, block, 1, 0, 512);
    }

    float maxErr0 = 0.0f, maxErr1 = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        maxErr0 = std::max (maxErr0, std::abs (wet.getSample (0, i) - dry0[(size_t) i]));
        maxErr1 = std::max (maxErr1, std::abs (wet.getSample (1, i) - dry1[(size_t) i]));
    }

    std::fprintf (stderr, "IDENTITY ch0 maxErr=%.6f  ch1 maxErr=%.6f\n", maxErr0, maxErr1);
    REQUIRE (maxErr0 < 5.0e-3f);
    REQUIRE (maxErr1 < 5.0e-3f);
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

    double phase = 0.0;
    const double step = 2.0 * 3.14159265358979 * 440.0 / 44100.0;
    auto fill = [&] (juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* data = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                data[i] = 0.5f * (float) std::sin (phase);
                phase += step;
            }
        }
    };

    for (int block = 0; block < 8; ++block) { fill (buffer); plugin.processBlock (buffer, midi); }

    plugin.triggerFreeze();

    bool sawNonZero = false;
    for (int block = 0; block < 40; ++block)
    {
        fill (buffer);
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
