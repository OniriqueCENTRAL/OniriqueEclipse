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
//  L'interface d'ECLIPSE (1200 × 800 points, redimensionnable sans déformation), dans la DA ONIRIQUE, avec deux façades
//  au choix (sélecteur STUDIO / RACK dans la barre du haut, retenu par projet et pour les nouvelles instances) :
//
//  STUDIO  la courbe interactive et les mesures en haut, les trois sections LOW · MID + CONTOUR · HIGH au centre,
//          FILTERS · M/S · ANALOG · MONITOR · OUTPUT en bas.
//  RACK    trois unités empilées comme un rack de mastering : l'égaliseur (POWER, XFORMER, INPUT | LOW | CONTOUR, VU,
//          MID | HIGH | DRIVE, ANALOG, OUTPUT), les outils (SOLO M, SOLO S, DELTA | M/S LINK, cible, AUTO GAIN | MONO,
//          WIDTH), puis la courbe et les mesures. Chaque grand potard porte son anneau MID·SIDE / OFFSET.
//
//  Les mêmes commandes servent aux deux façades (mêmes paramètres) ; seules quelques-unes n'existent que dans l'une.
//==============================================================================
class EclipseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EclipseEditor (EclipseProcessor&);
    ~EclipseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int baseWidth = 1200, baseHeight = 800;
    enum class View { studio, rack };

private:
    /** STUDIO | RACK, dans la barre du haut */
    struct ViewSwitch : juce::Component
    {
        std::function<void (View)> onChange;
        View view = View::studio;
        float scale = 1.0f;
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            onirique::da::glass (g, r, r.getHeight() * 0.5f, scale, false);
            const auto a = r.withWidth (r.getWidth() * 0.5f).reduced (3.0f * scale), b = a.translated (r.getWidth() * 0.5f, 0);
            onirique::da::seg (g, view == View::studio ? a : b, 1.0f, 0.0f, scale, a.getHeight() * 0.5f);
            ui::text (g, "STUDIO", a, ui::font (ui::Face::body, 9.5f * scale, 0.14f), view == View::studio ? ui::bone : ui::haze);
            ui::text (g, "RACK", b, ui::font (ui::Face::body, 9.5f * scale, 0.14f), view == View::rack ? ui::bone : ui::haze);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto v = e.position.x < (float) getWidth() * 0.5f ? View::studio : View::rack;
            if (v != view && onChange) onChange (v);
        }
    };

    juce::RangedAudioParameter& p (const juce::String& id) { return *processor.apvts.getParameter (id); }
    void setView (View v, bool remember);
    void layoutStudio (float s);
    void layoutRack (float s);
    void renderBackground();
    void paintStudio (juce::Graphics&);
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
    View view = View::studio;

    ui::CurveDisplay curve;
    ui::MeterPanel meterPanel;

    ui::Knob lowBoost, lowFreq, lowAtten, lowBw;
    ui::Knob midQ, midGain, midFreq, contour, contourFreq, contourBw;
    ui::Knob highBoost, highFreq, highAtten, highBw;
    ui::Switch hpfOn, lpfOn;
    ui::Knob hpfFreq, lpfFreq;
    ui::Switch msLink;
    ui::Segments target;
    ui::Knob lowOfs, midOfs, conOfs, highOfs;
    ui::Switch analog;
    ui::Knob character, xformer;
    ui::Segments solo;
    ui::Switch delta, mono;
    ui::Knob width;
    ui::Knob inGain, outGain;
    ui::Switch autoGain;

    // façade rack
    ui::OffsetRing lowRing, conRing, midRing, highRing;
    ui::VuMeter vu;
    ui::LedButton power, hpfLed, lpfLed;
    ui::ChoiceButton soloM, soloS;

    // OFFSET n'agit que si M/S LINK est éteint : ses commandes s'estompent quand il est allumé
    juce::ParameterAttachment linkWatch;

    onirique::nuit::Bar bar;
    ViewSwitch viewSwitch;
    bool sizeReady = false;
    int themeSeen = -1;

#if ECLIPSE_LICENCE
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EclipseEditor)
};
