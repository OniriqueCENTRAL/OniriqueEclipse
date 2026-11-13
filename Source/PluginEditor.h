#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/Display.h"
#include "ui/OniriqueBar.h"
#if ECLIPSE_LICENCE
 #include "License/LicenseOverlay.h"
#endif

//==============================================================================
//  L'interface d'ECLIPSE (1200 × 800 points, redimensionnable sans déformation) :
//    en haut  la barre ONIRIQUE (titre, presets, IN, A / B, menu ⋯)
//    centre   la courbe interactive (somme des sections parallèles, Mid et Side) et les mesures,
//             puis les trois grandes sections LOW · MID + CONTOUR · HIGH
//    en bas   FILTERS (HPF / LPF) · M/S (LINK, cible et OFFSET par section) · ANALOG · MONITOR (SOLO, DELTA, MONO, WIDTH)
//             · OUTPUT (INPUT, OUTPUT, AUTO GAIN)
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
    void renderBackground();
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;
    void timerCallback() override;
    float scale() const { return (float) getWidth() / (float) baseWidth; }

    EclipseProcessor& processor;
    ui::FontScope fonts;
    ui::KnobLook knobLook;
    juce::Image background, logo;

    ui::CurveDisplay curve;
    ui::MeterPanel meterPanel;

    ui::Knob lowBoost, lowFreq, lowAtten;
    ui::Knob midQ, midGain, midFreq, contour, contourFreq;
    ui::Knob highBoost, highFreq, highAtten;
    ui::Switch hpfOn, lpfOn;
    ui::Knob hpfFreq, lpfFreq;
    ui::Switch msLink;
    ui::Segments target;
    ui::Knob lowOfs, midOfs, conOfs, highOfs;
    ui::Switch analog;
    ui::Knob character;
    ui::Segments solo;
    ui::Switch delta, mono;
    ui::Knob width;
    ui::Knob inGain, outGain;
    ui::Switch autoGain;

    // OFFSET n'agit que si M/S LINK est éteint : les quatre potards s'estompent quand il est allumé
    juce::ParameterAttachment linkWatch;

    onirique::nuit::Bar bar;
    bool sizeReady = false;
    int themeSeen = -1;

#if ECLIPSE_LICENCE
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EclipseEditor)
};
