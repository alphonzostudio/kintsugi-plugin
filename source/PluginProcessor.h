#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

//==============================================================================
// Kintsugi — Spectral Freeze / Repair Effect
//
// Captures a snapshot of the current audio spectrum and blends it back in via
// inverse FFT. Over time the frozen spectrum decays and shimmers, evoking the
// gold-veined repair of Japanese kintsugi pottery.
//==============================================================================

namespace KintsugiParams
{
    inline const juce::String freezeAmountID { "freezeAmount" };
    inline const juce::String shimmerID      { "shimmer" };
    inline const juce::String resolutionID   { "resolution" };
    inline const juce::String decayID        { "decay" };
}

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //====================================================================
    /** Called from the UI thread when the user presses the freeze button.
        The actual capture happens on the audio thread at the next analysis frame. */
    void triggerFreeze() noexcept { freezeRequested.store (true, std::memory_order_release); }

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /** Thread-safe copy of the most recent live-spectrum magnitudes, for the editor's visualiser.
        binCount() bins, roughly log-ish in the low end thanks to the FFT's natural spacing. */
    void getLiveMagnitudes (std::vector<float>& dest) const;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void rebuildFFT (int newFftOrder);
    void processFrame() noexcept;

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<float>* freezeAmountParam = nullptr;
    std::atomic<float>* shimmerParam      = nullptr;
    std::atomic<float>* resolutionParam   = nullptr;
    std::atomic<float>* decayParam        = nullptr;

    //====================================================================
    // STFT engine
    static constexpr int minFFTOrder = 11; // 2^11 = 2048
    static constexpr int maxFFTOrder = 14; // 2^14 = 16384

    int fftOrder    = 12;
    int fftSize     = 1 << fftOrder;
    int hopSize     = fftSize / 4;
    int fftBinCount = fftSize / 2 + 1;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;          // analysis + synthesis window (Hann), fftSize
    std::vector<float> fftData;         // 2 * fftSize scratch for forward/inverse transforms

    // Analysis: circular ring holding the most recent fftSize input samples
    std::vector<float> inputRing;
    int ringWritePos  = 0;
    int hopCounter    = 0;

    // Synthesis: linear overlap-add accumulator, shifted left by hopSize once per hop
    std::vector<float> outputAccum;

    // Finished output samples waiting to be doled out one-per-sample, circular
    std::vector<float> pendingOutput;
    int pendingReadPos  = 0;
    int pendingWritePos = 0;

    float olaNormalisation = 1.0f;

    std::vector<float> liveMagnitude, livePhase;
    std::vector<float> frozenMagnitude, frozenPhase;

    std::atomic<bool> freezeRequested { false };
    bool spectrumFrozen = false;
    double shimmerLFOPhase = 0.0;

    mutable juce::SpinLock magnitudeLock;
    std::vector<float> sharedMagnitudeForUI;

    double currentSampleRate = 44100.0;
};
