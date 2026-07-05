#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>
#include <algorithm>

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    freezeAmountParam = apvts.getRawParameterValue (KintsugiParams::freezeAmountID);
    shimmerParam      = apvts.getRawParameterValue (KintsugiParams::shimmerID);
    resolutionParam   = apvts.getRawParameterValue (KintsugiParams::resolutionID);
    decayParam        = apvts.getRawParameterValue (KintsugiParams::decayID);

    rebuildFFT (fftOrder);
}

PluginProcessor::~PluginProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        KintsugiParams::freezeAmountID,
        "Freeze Blend",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        KintsugiParams::shimmerID,
        "Gold Shimmer",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        KintsugiParams::resolutionID,
        "Resolution",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.5f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        KintsugiParams::decayID,
        "Decay",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.7f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }));

    return layout;
}

//====================================================================
// rebuildFFT — re-allocate buffers for the current FFT size. Not realtime-safe;
// only called from prepareToPlay() and when the Resolution parameter changes.
//====================================================================

void PluginProcessor::rebuildFFT (int newFftOrder)
{
    fftOrder    = juce::jlimit (minFFTOrder, maxFFTOrder, newFftOrder);
    fftSize     = 1 << fftOrder;
    hopSize     = fftSize / 4;
    fftBinCount = fftSize / 2 + 1;

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.assign (fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

    fftData.assign ((size_t) fftSize * 2, 0.0f);

    inputRing.assign (fftSize, 0.0f);
    ringWritePos = 0;
    hopCounter = 0;

    outputAccum.assign (fftSize, 0.0f);

    pendingOutput.assign (fftSize, 0.0f);
    pendingReadPos = 0;
    pendingWritePos = 0;

    liveMagnitude.assign ((size_t) fftBinCount, 0.0f);
    livePhase.assign ((size_t) fftBinCount, 0.0f);
    frozenMagnitude.assign ((size_t) fftBinCount, 0.0f);
    frozenPhase.assign ((size_t) fftBinCount, 0.0f);

    {
        const juce::SpinLock::ScopedLockType sl (magnitudeLock);
        sharedMagnitudeForUI.assign ((size_t) fftBinCount, 0.0f);
    }

    spectrumFrozen = false;
    shimmerLFOPhase = 0.0;

    // Steady-state overlap-add normalisation: sum the window sampled every hopSize,
    // which (by periodicity) equals the constant-overlap-add sum for this window/hop pair.
    float sum = 0.0f;
    for (int offset = 0; offset < fftSize; offset += hopSize)
        sum += window[(size_t) offset];
    olaNormalisation = sum > 1.0e-6f ? 1.0f / sum : 1.0f;

    setLatencySamples (fftSize);
}

//====================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    rebuildFFT (fftOrder);
}

void PluginProcessor::releaseResources()
{
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

//====================================================================
// processFrame — runs once per hop: forward-FFT the latest analysis window,
// latch a freeze snapshot if one was requested, evolve the frozen spectrum
// (shimmer + decay), inverse-FFT it, and overlap-add it into outputAccum.
//====================================================================

void PluginProcessor::processFrame() noexcept
{
    for (int n = 0; n < fftSize; ++n)
    {
        const int idx = (ringWritePos + n) % fftSize;
        fftData[(size_t) n] = inputRing[(size_t) idx] * window[(size_t) n];
    }
    for (int n = fftSize; n < fftSize * 2; ++n)
        fftData[(size_t) n] = 0.0f;

    fft->performRealOnlyForwardTransform (fftData.data(), false);

    for (int bin = 0; bin < fftBinCount; ++bin)
    {
        const float re = fftData[(size_t) (2 * bin)];
        const float im = fftData[(size_t) (2 * bin + 1)];
        liveMagnitude[(size_t) bin] = std::sqrt (re * re + im * im);
        livePhase[(size_t) bin]     = std::atan2 (im, re);
    }

    {
        const juce::SpinLock::ScopedLockType sl (magnitudeLock);
        sharedMagnitudeForUI = liveMagnitude;
    }

    if (freezeRequested.exchange (false, std::memory_order_acq_rel))
    {
        frozenMagnitude = liveMagnitude;
        frozenPhase = livePhase;
        spectrumFrozen = true;
    }

    if (! spectrumFrozen)
        return;

    const float shimmerAmt = shimmerParam->load();
    const float decayAmt   = decayParam->load();

    // decayAmt 0 -> fast decay per frame, decayAmt 1 -> essentially held forever
    const float decayPerFrame = 1.0f - (1.0f - decayAmt) * 0.05f;

    const double hopSeconds = (double) hopSize / currentSampleRate;
    shimmerLFOPhase += hopSeconds * juce::MathConstants<double>::twoPi * (0.3 + shimmerAmt * 3.0);

    for (int bin = 0; bin < fftBinCount; ++bin)
    {
        const double bandPhase = bin * 0.05;
        const float shimmerMod = 0.5f + 0.5f * (float) std::sin (shimmerLFOPhase + bandPhase);
        frozenMagnitude[(size_t) bin] *= decayPerFrame * (1.0f - shimmerAmt * 0.3f * shimmerMod);
        frozenMagnitude[(size_t) bin] = juce::jmax (0.0f, frozenMagnitude[(size_t) bin]);
    }

    for (int bin = 0; bin < fftBinCount; ++bin)
    {
        const float mag = frozenMagnitude[(size_t) bin];
        const float ph  = frozenPhase[(size_t) bin];
        fftData[(size_t) (2 * bin)]     = mag * std::cos (ph);
        fftData[(size_t) (2 * bin + 1)] = mag * std::sin (ph);
    }
    fftData[1] = 0.0f;                                   // DC must be real
    fftData[(size_t) (2 * (fftBinCount - 1) + 1)] = 0.0f; // Nyquist must be real
    for (int bin = fftBinCount; bin < fftSize; ++bin)
    {
        const int mirror = fftSize - bin;
        fftData[(size_t) (2 * bin)]     =  fftData[(size_t) (2 * mirror)];
        fftData[(size_t) (2 * bin + 1)] = -fftData[(size_t) (2 * mirror + 1)];
    }

    fft->performRealOnlyInverseTransform (fftData.data());

    for (int n = 0; n < fftSize; ++n)
        outputAccum[(size_t) n] += fftData[(size_t) n] * window[(size_t) n] * olaNormalisation;

    // The front hopSize samples of outputAccum are now finished (no later frame's
    // window will ever reach back far enough to touch them again).
    for (int n = 0; n < hopSize; ++n)
    {
        pendingOutput[(size_t) pendingWritePos] = outputAccum[(size_t) n];
        pendingWritePos = (pendingWritePos + 1) % fftSize;
    }

    std::memmove (outputAccum.data(), outputAccum.data() + hopSize, sizeof (float) * (size_t) (fftSize - hopSize));
    std::fill (outputAccum.begin() + (fftSize - hopSize), outputAccum.end(), 0.0f);
}

//====================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    const float freezeAmt = freezeAmountParam->load();

    for (int i = 0; i < numSamples; ++i)
    {
        float monoIn = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            monoIn += buffer.getReadPointer (ch)[i];
        monoIn /= (float) numChannels;

        inputRing[(size_t) ringWritePos] = monoIn;
        ringWritePos = (ringWritePos + 1) % fftSize;

        const float frozenSample = pendingOutput[(size_t) pendingReadPos];
        pendingOutput[(size_t) pendingReadPos] = 0.0f;
        pendingReadPos = (pendingReadPos + 1) % fftSize;

        if (freezeAmt > 0.0001f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                data[i] = data[i] * (1.0f - freezeAmt) + frozenSample * freezeAmt;
            }
        }

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            processFrame();
        }
    }
}

//====================================================================
void PluginProcessor::getLiveMagnitudes (std::vector<float>& dest) const
{
    const juce::SpinLock::ScopedLockType sl (magnitudeLock);
    dest = sharedMagnitudeForUI;
}

//====================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

bool PluginProcessor::hasEditor() const { return true; }

const juce::String PluginProcessor::getName() const { return "Kintsugi"; }

bool PluginProcessor::acceptsMidi() const { return false; }
bool PluginProcessor::producesMidi() const { return false; }
bool PluginProcessor::isMidiEffect() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 2.0; }

int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram (int) {}
const juce::String PluginProcessor::getProgramName (int) { return "Default"; }
void PluginProcessor::changeProgramName (int, const juce::String&) {}

//====================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//====================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
