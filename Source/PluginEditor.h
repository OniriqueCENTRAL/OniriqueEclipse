#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/RackControls.h"
#include "ui/Display.h"
#include "ui/OniriqueBar.h"
#if ECLIPSE_LICENCE
 #include "License/LicenseOverlay.h"
#endif

//==============================================================================
//  L'interface d'ECLIPSE (1200 × 800 points, redimensionnable sans déformation), dans la DA ONIRIQUE, en façade
//  « rack » : trois unités empilées comme un rack de mastering.
//    l'égaliseur  POWER, XFORMER, INPUT | LOW | CONTOUR, VU, MID | HIGH | DRIVE, ANALOG, OUTPUT
//    les outils   SOLO M, SOLO S, DELTA | M/S LINK, cible de l'OFFSET, AUTO GAIN | MONO, WIDTH
//    la courbe    la somme des sections parallèles (Mid et Side), interactive, et les mesures
//  Chaque grand potard porte son anneau MID·SIDE / OFFSET.
//==============================================================================
class EclipseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EclipseEditor (EclipseProcessor&);
    ~EclipseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1200, baseHeight = 800;

private:
    juce::RangedAudioParameter& p (const juce::String& id) { return *processor.apvts.getParameter (id); }
    void layoutRack (float s);
    void renderBackground();
    void paintRack (juce::Graphics&);
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;
    void timerCallback() override;
    float scale() const { return (float) getWidth() / (float) baseWidth; }
    /** ce que reçoivent le Mid et le Side dans une section (0 LOW, 1 MID, 2 CONTOUR, 3 HIGH), en dB */
    std::pair<double, double> sectionValues (int section) const;

    EclipseProcessor& processor;
    ui::FontScope fonts;
    ui::KnobLook knobLook;
    juce::Image background, logo;

    ui::CurveDisplay curve;
    ui::MeterPanel meterPanel;

    ui::Knob lowBoost, lowFreq, lowAtten, lowBw;
    ui::Knob midQ, midGain, midFreq, contour, contourFreq, contourBw;
    ui::Knob highBoost, highFreq, highAtten, highBw;
    ui::Knob hpfFreq, lpfFreq;
    ui::Switch msLink;
    ui::Segments target;
    ui::Switch analog;
    ui::Knob character, xformer;
    ui::Switch delta, mono;
    ui::Knob width;
    ui::Knob inGain, outGain;
    ui::Switch autoGain;

    ui::OffsetRing lowRing, conRing, midRing, highRing;
    ui::VuMeter vu;
    ui::LedButton power, hpfLed, lpfLed;
    ui::ChoiceButton soloM, soloS;

    // OFFSET n'agit que si M/S LINK est éteint : ses commandes s'estompent quand il est allumé
    juce::ParameterAttachment linkWatch;

    onirique::nuit::Bar bar;
    bool sizeReady = false;
    int themeSeen = -1;

#if ECLIPSE_LICENCE
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EclipseEditor)
};
