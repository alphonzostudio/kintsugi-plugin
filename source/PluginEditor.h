#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class PluginEditor : public juce::AudioProcessorEditor
                    , private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    PluginProcessor& processor;

    // NOTE: sliders must be declared BEFORE their attachments. C++ constructs
    // members in Declaration order (not init-list order), and each
    // SliderAttachment's constructor writes into its Slider
    // (valueFromTextFunction, etc.). If the attachment constructed first, it
    // would write into an unconstructed Slider -> null-vtable SEGV. (This is
    // why the boot benchmark crashed in SliderAttachment while the DSP tests,
    // which never call createEditor, passed.)
    juce::Slider sliderFreezeAmt;
    juce::Slider sliderShimmer;
    juce::Slider sliderResolution;
    juce::Slider sliderDecay;

    using Attach = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attach> attachFreezeAmt;
    std::unique_ptr<Attach> attachShimmer;
    std::unique_ptr<Attach> attachResolution;
    std::unique_ptr<Attach> attachDecay;


    juce::Label labelTitle;
    juce::Label labelSubTitle;
    juce::Label labelFreezeAmt;
    juce::Label labelShimmer;
    juce::Label labelResolution;
    juce::Label labelDecay;

    juce::TextButton freezeBtn;

    // Spectrum visualiser — smoothed history of the processor's live magnitude bins
    std::vector<float> spectrumHistory;
    std::vector<float> liveMagnitudes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
