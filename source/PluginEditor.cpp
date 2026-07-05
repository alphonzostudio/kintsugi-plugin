#include "PluginEditor.h"
#include <cmath>
#include <utility>

namespace KintsugiColours
{
    static const juce::Colour bgDark      { 0xFF0f0d0a };
    static const juce::Colour bgPanel     { 0xFF1c1814 };
    static const juce::Colour goldBright  { 0xFFd4a24e };
    static const juce::Colour goldSoft    { 0xFFb8863a };
    static const juce::Colour goldGlow    { 0xFFe8c27a };
    static const juce::Colour textPrimary { 0xFFede7dd };
    static const juce::Colour textMuted   { 0xFF6a6058 };
}

namespace
{
    constexpr int numSpectrumBars = 64;

    struct GoldLookAndFeel : juce::LookAndFeel_V4
    {
        GoldLookAndFeel()
        {
            setColour (juce::Slider::trackColourId,       KintsugiColours::bgDark);
            setColour (juce::Slider::thumbColourId,        KintsugiColours::goldBright);
            setColour (juce::Slider::textBoxTextColourId,  KintsugiColours::textPrimary);
            setColour (juce::Label::textColourId,          KintsugiColours::textPrimary);
        }
    };

    GoldLookAndFeel& sharedLookAndFeel()
    {
        static GoldLookAndFeel laf;
        return laf;
    }

    void styleParamLabel (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (9.0f, juce::Font::bold));
        l.setJustificationType (juce::Justification::bottomLeft);
        l.setColour (juce::Label::textColourId, KintsugiColours::textMuted);
    }
}

//====================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : juce::AudioProcessorEditor (p)
    , processor (p)
    , attachFreezeAmt  (std::make_unique<Attach> (p.getAPVTS(), KintsugiParams::freezeAmountID, sliderFreezeAmt))
    , attachShimmer    (std::make_unique<Attach> (p.getAPVTS(), KintsugiParams::shimmerID,       sliderShimmer))
    , attachResolution (std::make_unique<Attach> (p.getAPVTS(), KintsugiParams::resolutionID,    sliderResolution))
    , attachDecay      (std::make_unique<Attach> (p.getAPVTS(), KintsugiParams::decayID,          sliderDecay))
    , spectrumHistory (numSpectrumBars, 0.0f)
{
    setSize (460, 480);
    setLookAndFeel (&sharedLookAndFeel());

    for (auto* s : { &sliderFreezeAmt, &sliderShimmer, &sliderResolution, &sliderDecay })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setRange (0.0, 1.0);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible (s);
    }

    labelTitle.setText ("KINTSUGI", juce::dontSendNotification);
    labelTitle.setFont (juce::Font (26.0f, juce::Font::bold));
    labelTitle.setJustificationType (juce::Justification::centredTop);
    labelTitle.setColour (juce::Label::textColourId, KintsugiColours::goldBright);

    labelSubTitle.setText ("spectral freeze & repair", juce::dontSendNotification);
    labelSubTitle.setFont (juce::Font (10.0f));
    labelSubTitle.setJustificationType (juce::Justification::centredTop);
    labelSubTitle.setColour (juce::Label::textColourId, KintsugiColours::textMuted);

    styleParamLabel (labelFreezeAmt,  "FREEZE BLEND");
    styleParamLabel (labelShimmer,    "GOLD SHIMMER");
    styleParamLabel (labelResolution, "RESOLUTION");
    styleParamLabel (labelDecay,      "DECAY");

    freezeBtn.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x89")); // filled circle
    freezeBtn.setClickingTogglesState (false);
    freezeBtn.setColour (juce::TextButton::buttonColourId,       KintsugiColours::bgPanel);
    freezeBtn.setColour (juce::TextButton::buttonOnColourId,     KintsugiColours::goldBright);
    freezeBtn.setColour (juce::TextButton::textColourOnId,       KintsugiColours::bgDark);
    freezeBtn.setColour (juce::TextButton::textColourOffId,      KintsugiColours::goldBright);
    freezeBtn.onClick = [this]
    {
        processor.triggerFreeze();
    };

    addAndMakeVisible (labelTitle);
    addAndMakeVisible (labelSubTitle);
    addAndMakeVisible (labelFreezeAmt);
    addAndMakeVisible (labelShimmer);
    addAndMakeVisible (labelResolution);
    addAndMakeVisible (labelDecay);
    addAndMakeVisible (freezeBtn);

    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel (nullptr);
}

//====================================================================
namespace
{
    void drawSpectrumBar (juce::Graphics& g, int barIndex, int totalBars,
                          float magnitude01, juce::Rectangle<int> area)
    {
        const float barGap    = 1.5f;
        const float totalBarW = (float) area.getWidth() / (float) totalBars;
        const float drawW     = totalBarW - barGap;

        if (drawW <= 0.0f)
            return;

        const float x    = (float) area.getX() + (float) barIndex * totalBarW + barGap * 0.5f;
        const float barH = juce::jlimit (1.0f, (float) area.getHeight(), magnitude01 * (float) area.getHeight());

        juce::Rectangle<float> barRect (x, (float) area.getBottom() - barH, drawW, barH);

        juce::ColourGradient grad (KintsugiColours::goldGlow, x, (float) area.getBottom(),
                                   KintsugiColours::goldSoft, x, (float) area.getBottom() - barH,
                                   false);

        g.setGradientFill (grad);
        g.fillRoundedRectangle (barRect, 1.0f);
    }
}

void PluginEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    g.fillAll (KintsugiColours::bgDark);

    const int m = 16;
    g.setColour (KintsugiColours::bgPanel);
    g.fillRoundedRectangle ((float) m, (float) m, (float) (w - m * 2), (float) (h - m * 2), 12.0f);

    g.setColour (KintsugiColours::goldSoft.withAlpha (0.15f));
    g.drawRoundedRectangle ((float) m, (float) m, (float) (w - m * 2), (float) (h - m * 2), 12.0f, 1.0f);

    g.setColour (KintsugiColours::goldBright.withAlpha (0.25f));
    g.drawHorizontalLine (72, 30.0f, (float) (w - 30));

    const int visX = m + 24;
    const int visY = 84;
    const int visW = w - m * 2 - 48;
    const int visH = 80;

    g.setColour (KintsugiColours::bgDark.withAlpha (0.6f));
    g.fillRoundedRectangle ((float) visX, (float) visY, (float) visW, (float) visH, 6.0f);

    g.setColour (KintsugiColours::goldSoft.withAlpha (0.06f));
    for (int i = 1; i < 4; ++i)
    {
        const float gY = (float) visY + (float) visH * (float) i / 4.0f;
        g.drawHorizontalLine ((int) gY, (float) visX + 4.0f, (float) (visX + visW) - 4.0f);
    }

    const juce::Rectangle<int> visArea (visX, visY, visW, visH);
    for (int i = 0; i < (int) spectrumHistory.size(); ++i)
        drawSpectrumBar (g, i, (int) spectrumHistory.size(), spectrumHistory[(size_t) i], visArea);

    g.setColour (KintsugiColours::textMuted.withAlpha (0.4f));
    g.setFont (juce::Font (7.0f));
    g.drawText ("SPECTRUM", visX + 4, visY + visH - 13, 60, 10, juce::Justification::bottomLeft);
}

//====================================================================
void PluginEditor::resized()
{
    const int w = getWidth();

    labelTitle.setBounds (16, 24, w - 32, 34);
    labelSubTitle.setBounds (16, 52, w - 32, 16);

    const float paramX      = 32;
    const float paramW      = (float) (w - 64);
    const float paramStartY = 190;
    const float paramRowH   = 40.0f;

    const std::pair<juce::Slider*, juce::Label*> rows[] = {
        { &sliderFreezeAmt,  &labelFreezeAmt },
        { &sliderShimmer,    &labelShimmer },
        { &sliderResolution, &labelResolution },
        { &sliderDecay,      &labelDecay },
    };

    for (size_t row = 0; row < std::size (rows); ++row)
    {
        const float y = paramStartY + (float) row * paramRowH;
        rows[row].second->setBounds ((int) paramX, (int) (y + 2), 120, 14);
        rows[row].first->setBounds ((int) (paramX + 4), (int) (y + 18), (int) paramW - 4, 16);
    }

    const int btnW = 80;
    const int btnH = 80;
    freezeBtn.setBounds (getWidth() / 2 - btnW / 2, 340, btnW, btnH);
}

//====================================================================
void PluginEditor::timerCallback()
{
    processor.getLiveMagnitudes (liveMagnitudes);

    if (liveMagnitudes.empty())
        return;

    const int numBins = (int) liveMagnitudes.size();
    const int numBars = (int) spectrumHistory.size();

    // Log-ish bucketing: later bars each cover more bins than earlier ones.
    for (int bar = 0; bar < numBars; ++bar)
    {
        const double t0 = std::pow ((double) bar / numBars, 2.0);
        const double t1 = std::pow ((double) (bar + 1) / numBars, 2.0);
        int binStart = juce::jlimit (0, numBins - 1, (int) (t0 * numBins));
        int binEnd   = juce::jlimit (binStart + 1, numBins, (int) (t1 * numBins));

        float sum = 0.0f;
        for (int b = binStart; b < binEnd; ++b)
            sum += liveMagnitudes[(size_t) b];
        const float avg = sum / (float) (binEnd - binStart);

        // Rough perceptual scaling — FFT magnitudes for musical material are usually small.
        const float target = juce::jlimit (0.0f, 1.0f, avg * 6.0f);

        auto& v = spectrumHistory[(size_t) bar];
        v = target > v ? target : v * 0.85f;
    }

    repaint();
}
