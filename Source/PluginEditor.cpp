#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include "BinaryData.h"

namespace
{
const juce::String pluginName = "ONIRIQUE ECLIPSE";
using R = juce::Rectangle<float>;

// tailles des potards (composant entier : étiquette + bouton + valeur)
const juce::Point<float> big { 112, 140 }, medium { 86, 110 }, small { 72, 94 }, mini { 64, 76 };

// ---- façade RACK : trois unités empilées
namespace rack
{
const R unitA { 12, 58, 1176, 386 }, unitB { 12, 452, 1176, 92 }, unitC { 12, 552, 1176, 240 };
const R lowPanel { 160, 68, 220, 366 }, centrePanel { 390, 68, 420, 366 }, highPanel { 820, 68, 220, 366 };
const R soloPanel { 160, 460, 220, 76 }, msPanel { 390, 460, 420, 76 }, monitorPanel { 820, 460, 220, 76 };
const R displayArea { 24, 562, 890, 220 }, meterArea { 924, 562, 252, 220 };
constexpr float leftX = 86, rightX = 1114, bigY = 170, row2 = 286, row3 = 380;
constexpr float contourX = 462, midX = 738;
}

} // namespace

//==============================================================================
EclipseEditor::EclipseEditor (EclipseProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc),
      curve (proc.apvts, [&proc] { return proc.readParams(); }, [&proc] { return proc.getWorkingRate(); }),
      meterPanel (proc.meters()),
      lowBoost (p (params::lowBoost), "BOOST", knobLook), lowFreq (p (params::lowFreq), "FREQ", knobLook),
      lowAtten (p (params::lowAtten), "ATTEN", knobLook), lowBw (p (params::lowBw), "BW", knobLook),
      midQ (p (params::midQ), "Q", knobLook), midGain (p (params::midGain), "MID", knobLook, true), midFreq (p (params::midFreq), "FREQ", knobLook),
      contour (p (params::contour), "CONTOUR", knobLook, true), contourFreq (p (params::contourFreq), "FREQ", knobLook),
      contourBw (p (params::contourBw), "BW", knobLook),
      highBoost (p (params::highBoost), "BOOST", knobLook), highFreq (p (params::highFreq), "FREQ", knobLook),
      highAtten (p (params::highAtten), "ATTEN", knobLook), highBw (p (params::highBw), "BW", knobLook),
      hpfFreq (p (params::hpfFreq), "FREQ", knobLook), lpfFreq (p (params::lpfFreq), "FREQ", knobLook),
      msLink (p (params::msLink), "M/S LINK"),
      target (p (params::offsetTarget), { "SIDE", "MID", "SPLIT" }),
      analog (p (params::analog), "ANALOG"), character (p (params::character), "CHARACTER", knobLook),
      xformer (p (params::xformer), "XFORMER", knobLook),
      delta (p (params::delta), "DELTA"), mono (p (params::mono), "MONO"),
      width (p (params::width), "WIDTH", knobLook),
      inGain (p (params::inGain), "INPUT", knobLook, true), outGain (p (params::outGain), "OUTPUT", knobLook, true),
      autoGain (p (params::autoGain), "AUTO GAIN"),
      lowRing (p (params::lowOffset), 12.0, [this] { return sectionValues (0); }),
      conRing (p (params::contourOffset), 10.0, [this] { return sectionValues (2); }),
      midRing (p (params::midOffset), 12.0, [this] { return sectionValues (1); }),
      highRing (p (params::highOffset), 12.0, [this] { return sectionValues (3); }),
      vu (proc.meters()),
      power (p (params::bypass), "POWER", true), hpfLed (p (params::hpfOn), "HPF"), lpfLed (p (params::lpfOn), "LPF"),
      soloM (p (params::solo), 1, "SOLO M"), soloS (p (params::solo), 2, "SOLO S"),
      linkWatch (p (params::msLink), [this] (float v)
      {
          const bool linked = v > 0.5f;
          target.setAlpha (linked ? 0.38f : 1.0f);
          for (auto* r : { &lowRing, &conRing, &midRing, &highRing }) r->setLinked (linked);
      }),
      bar (proc.presets, proc.apvts, "ECLIPSE", {}, baseWidth, 1000)
{
    onirique::look::load();
    bar.onThemeChanged = [this]
    {
        renderBackground();
        std::function<void (juce::Component&)> all = [&all] (juce::Component& k) { k.repaint(); for (auto* ch : k.getChildren()) all (*ch); };
        all (*this);
    };
    {
        int size = 0;
        if (const char* data = BinaryData::getNamedResource ("logo_png", size))
            logo = juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &curve, &meterPanel, &lowBoost, &lowFreq, &lowAtten, &lowBw, &midQ, &midGain, &midFreq, &contour, &contourFreq, &contourBw,
             &highBoost, &highFreq, &highAtten, &highBw, &hpfFreq, &lpfFreq, &msLink, &target,
             &analog, &character, &xformer, &delta, &mono, &width, &inGain, &outGain, &autoGain,
             &vu, &power, &hpfLed, &lpfLed, &soloM, &soloS })
        addAndMakeVisible (c);
    // les anneaux passent devant leur potard (ils ne prennent la souris que sur leur moitié droite)
    for (auto* r : { &lowRing, &conRing, &midRing, &highRing }) addAndMakeVisible (r);
    linkWatch.sendInitialUpdate();

    addAndMakeVisible (bar);
    bar.setBrand (290, [this] (juce::Graphics& g, juce::Rectangle<float> area) { paintTitle (g, area); });

#if ECLIPSE_LICENCE
    licenseOverlay = std::make_unique<onirique::license::Overlay> (pluginName, processor.license);
    addAndMakeVisible (*licenseOverlay);
   #if ONIRIQUE_UI_TEST
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("nolicense"))
        licenseOverlay->setVisible (false);
   #endif
#endif

    // la taille gardée se lit AVANT de poser les limites (setResizeLimits redimensionne déjà l'éditeur)
    auto heightFor = [] (int w) { return juce::roundToInt ((float) w * (float) baseHeight / (float) baseWidth); };
    const auto size = onirique::window::openingSize (processor.apvts.state, pluginName, { 1000, heightFor (1000) }, { 780, heightFor (780) },
                                                     { 1800, heightFor (1800) });
    setResizable (true, true);
    setResizeLimits (780, heightFor (780), 1800, heightFor (1800));
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setSize (size.x, heightFor (size.x));
    sizeReady = true;
    onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
   #if ONIRIQUE_UI_TEST
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo"))
        setSize (baseWidth, baseHeight);
   #endif
    // les noms de la façade
    lowBoost.setLabel ("BOOST"); highBoost.setLabel ("BOOST");
    for (auto* k : { &lowBw, &highBw, &contourBw, &midQ }) k->setLabel ("BANDWIDTH");
    hpfFreq.setLabel ("HPF");
    lpfFreq.setLabel ("LPF");
    character.setLabel ("DRIVE");
    startTimerHz (4);

    // essais locaux : ONIRIQUE_UI_PNG=fichier → l'interface s'écrit elle-même en PNG (rendu logiciel)
    if (auto* png = std::getenv ("ONIRIQUE_UI_PNG"))
    {
        const juce::String path (png);
        juce::Component::SafePointer<EclipseEditor> safe (this);
        if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("browser"))
            juce::Timer::callAfterDelay (500, [safe] { if (safe != nullptr) safe->bar.openBrowser(); });
        juce::Timer::callAfterDelay (1500, [safe, path]
        {
            if (safe == nullptr) return;
            juce::Image img (juce::Image::ARGB, safe->getWidth(), safe->getHeight(), true, juce::SoftwareImageType());
            { juce::Graphics g (img); safe->paintEntireComponent (g, true); }
            juce::File f (path); f.deleteFile();
            if (auto os = f.createOutputStream()) juce::PNGImageFormat().writeImageToStream (img, *os);
        });
    }
}

EclipseEditor::~EclipseEditor()
{
    onirique::window::rememberOnThisComputer (pluginName, getWidth(), getHeight());
}

void EclipseEditor::timerCallback()
{
    // la couleur d'interface choisie dans une autre fenêtre de ce plugin
    if (onirique::look::changedSince (themeSeen) && bar.onThemeChanged) bar.onThemeChanged();
}

std::pair<double, double> EclipseEditor::sectionValues (int section) const
{
    const auto prm = processor.readParams();
    const auto m = eclipse::channelSettings (prm, 0), s = eclipse::channelSettings (prm, 1);
    auto v = [section] (const eclipse::Settings& x)
    {
        switch (section)
        {
            case 0: return x.lowB - x.lowA;
            case 1: return x.midG;
            case 2: return -x.conV;   // vers le haut : le centre relevé, comme sur la courbe
            default: return x.highB - x.highA;
        }
    };
    return { v (m), v (s) };
}

//==============================================================================
void EclipseEditor::resized()
{
    const float s = scale();
    layoutRack (s);

    // la barre du haut et la pastille de licence
#if ECLIPSE_LICENCE
    const int pillW = licenseOverlay != nullptr ? licenseOverlay->pillWidth() : 0;
#else
    const int pillW = 0;
#endif
    bar.setPillWidth (pillW);
    bar.place (s, getLocalBounds());
#if ECLIPSE_LICENCE
    if (licenseOverlay != nullptr)
        licenseOverlay->fit (getLocalBounds(), s, bar.getPillArea(), juce::Justification::centredRight);
#endif
    renderBackground();
    if (sizeReady) onirique::window::remember (processor.apvts.state, getWidth(), getHeight());
}

void EclipseEditor::layoutRack (float s)
{
    using namespace rack;
    auto place = [s] (juce::Component& c, R r) { c.setBounds (R (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s).toNearestInt()); };
    auto knob = [&] (ui::Knob& k, float cx, float cy, juce::Point<float> sz) { place (k, R (sz.x, sz.y).withCentre ({ cx, cy })); k.setScale (s); };
    auto ring = [&] (ui::OffsetRing& r, float cx, float cy) { place (r, R (240, 200).withCentre ({ cx, cy })); r.setScale (s); };
    auto sw = [&] (ui::Switch& b, R r) { place (b, r); b.setScale (s); };

    // unité A : l'égaliseur
    place (power, R (48, 48).withCentre ({ leftX, 112 })); power.setScale (s);
    knob (xformer, leftX, 240, medium);
    knob (inGain, leftX, 366, medium);

    const float lowX = lowPanel.getCentreX(), highX = highPanel.getCentreX();
    knob (lowBoost, lowX, bigY, big);           ring (lowRing, lowX, bigY);
    knob (lowAtten, lowPanel.getX() + 56, row2, small);
    knob (lowBw, lowPanel.getRight() - 56, row2, small);
    knob (hpfFreq, lowPanel.getX() + 56, row3, small);
    knob (lowFreq, lowPanel.getRight() - 56, row3, small);
    place (hpfLed, R (26, 26).withCentre ({ lowX, row3 - 6 })); hpfLed.setScale (s);

    knob (contour, contourX, bigY, big);        ring (conRing, contourX, bigY);
    knob (contourBw, contourX, row2, small);
    knob (contourFreq, contourX, row3, small);
    knob (midGain, midX, bigY, big);            ring (midRing, midX, bigY);
    knob (midQ, midX, row2, small);
    knob (midFreq, midX, row3, small);
    place (vu, R (600 - 62, 80, 124, 150)); vu.setScale (s);

    knob (highBoost, highX, bigY, big);         ring (highRing, highX, bigY);
    knob (highBw, highPanel.getX() + 56, row2, small);
    knob (highAtten, highPanel.getRight() - 56, row2, small);
    knob (highFreq, highPanel.getX() + 56, row3, small);
    knob (lpfFreq, highPanel.getRight() - 56, row3, small);
    place (lpfLed, R (26, 26).withCentre ({ highX, row3 - 6 })); lpfLed.setScale (s);

    knob (character, rightX, 150, medium);
    sw (analog, R (rightX - 48, 228, 96, 24));
    knob (outGain, rightX, 366, medium);

    // unité B : les outils
    const float by = 485;
    place (soloM, R (soloPanel.getX() + 10, by, 64, 28)); soloM.setScale (s);
    place (soloS, R (soloPanel.getX() + 78, by, 64, 28)); soloS.setScale (s);
    sw (delta, R (soloPanel.getX() + 146, by, 66, 28));
    sw (msLink, R (msPanel.getX() + 12, by, 112, 28));
    place (target, R (msPanel.getX() + 132, by + 1, 160, 26)); target.setScale (s);
    sw (autoGain, R (msPanel.getRight() - 120, by, 108, 28));
    sw (mono, R (monitorPanel.getX() + 12, by, 90, 28));
    knob (width, monitorPanel.getRight() - 48, monitorPanel.getCentreY(), mini);

    // unité C : la courbe et les mesures
    place (curve, displayArea); curve.setScale (s);
    place (meterPanel, meterArea); meterPanel.setScale (s);
}

//==============================================================================
void EclipseEditor::renderBackground()
{
    const float k = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * k)),
                              juce::jmax (1, juce::roundToInt ((float) getHeight() * k)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale() * k));

    // la DA ONIRIQUE : fond nuit et ses taches de braise (même dessin que VOCALBOX)
    {
        const float u = scale() * k;
        const auto night = onirique::da::backdrop (background.getWidth(), background.getHeight(), u);
        juce::Graphics::ScopedSaveState keep (g);
        g.addTransform (juce::AffineTransform::scale (1.0f / u));
        g.drawImageAt (night, 0, 0);
    }
    paintRack (g);
}

void EclipseEditor::paintRack (juce::Graphics& g)
{
    using namespace ui;
    using namespace rack;
    const float u1 = 1.0f;

    // les unités : verre, oreilles de rack (fentes) et vis
    auto unit = [&] (R r)
    {
        onirique::da::glass (g, r, 16.0f, u1);
        for (float x : { r.getX() + 16, r.getRight() - 16 })
            for (float y : { r.getY() + 18, r.getBottom() - 18 })
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (R (14, 8).withCentre ({ x, y }), 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.drawRoundedRectangle (R (14, 8).withCentre ({ x, y }), 4.0f, 0.8f);
            }
        for (float x : { r.getX() + 44, r.getRight() - 44 })
            for (float y : { r.getY() + 14, r.getBottom() - 14 })
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillEllipse (R (7, 7).withCentre ({ x, y }));
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawEllipse (R (7, 7).withCentre ({ x, y }), 0.8f);
            }
    };
    unit (unitA); unit (unitB); unit (unitC);

    // panneaux intérieurs (verre plus profond)
    auto inner = [&] (R r)
    {
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillRoundedRectangle (r, 12.0f);
        onirique::da::glass (g, r, 12.0f, u1, false);
    };
    for (const auto& r : { lowPanel, centrePanel, highPanel, soloPanel, msPanel, monitorPanel }) inner (r);

    const auto capFont = font (Face::body, 9.5f, 0.16f);
    auto caption = [&] (const juce::String& t, float cx, float y, juce::Colour c) { text (g, t, R (120, 14).withCentre ({ cx, y }), capFont, c); };
    caption ("POWER", leftX, 146, haze);
    caption ("ANALOG", rightX, 214, haze);
    caption ("HPF", lowPanel.getCentreX(), row3 + 16, dim);
    caption ("LPF", highPanel.getCentreX(), row3 + 16, dim);

    // titres des sections
    const auto tf = font (Face::display, 10.5f, 0.2f);
    text (g, "LOW", R (lowPanel.getX() + 14, lowPanel.getY() + 8, 80, 14), tf, bone, juce::Justification::centredLeft);
    text (g, "HIGH", R (highPanel.getRight() - 94, highPanel.getY() + 8, 80, 14), tf, bone, juce::Justification::centredRight);
    text (g, "CONTOUR", R (centrePanel.getX() + 14, centrePanel.getY() + 8, 100, 14), tf, onirique::look::emberPink, juce::Justification::centredLeft);
    text (g, "MID", R (centrePanel.getRight() - 94, centrePanel.getY() + 8, 80, 14), tf, bone, juce::Justification::centredRight);

    // au centre, sous le VU : le nom
    {
        const juce::Point<float> c (600, 270);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.35f), c.x, c.y, accent.withAlpha (0.0f), c.x + 24, c.y, true));
        g.fillEllipse (R (48, 48).withCentre (c));
        g.setColour (onirique::look::emberLit);
        g.drawEllipse (R (28, 28).withCentre (c), 1.4f);
        g.setColour (ink);
        g.fillEllipse (R (26, 26).withCentre (c.translated (3.5f, -1.5f)));
        text (g, "ONIRIQUE", R (200, 14).withCentre ({ 600, 306 }), font (Face::display, 10.0f, 0.3f), bone.withAlpha (0.75f));
        text (g, "ECLIPSE", R (220, 30).withCentre ({ 600, 330 }), font (Face::display, 26.0f, 0.1f), accent);
        text (g, "PARALLEL SATURATING MID/SIDE EQ", R (260, 12).withCentre ({ 600, 420 }), font (Face::body, 8.5f, 0.18f), dim);
    }

    // unité B
    text (g, "MONITOR", R (soloPanel.getX() + 12, soloPanel.getY() + 4, 120, 14), font (Face::body, 8.5f, 0.18f), dim, juce::Justification::centredLeft);
    text (g, "M/S", R (msPanel.getX() + 12, msPanel.getY() + 4, 120, 14), font (Face::body, 8.5f, 0.18f), dim, juce::Justification::centredLeft);
    text (g, "OFFSET TO", R (msPanel.getX() + 132, msPanel.getY() + 4, 160, 14), font (Face::body, 8.5f, 0.18f), dim);
    text (g, "IMAGE", R (monitorPanel.getX() + 12, monitorPanel.getY() + 4, 120, 14), font (Face::body, 8.5f, 0.18f), dim, juce::Justification::centredLeft);
    if (logo.isValid())
    {
        g.setOpacity (0.85f);
        g.drawImage (logo, R (36, 32).withCentre ({ leftX, unitB.getCentreY() }), juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }
}

// Le titre, dans la barre : le sigil, une éclipse (disque nuit sur sa couronne braise), ONIRIQUE, ECLIPSE
void EclipseEditor::paintTitle (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using namespace ui;
    const float cy = 25.0f;
    float x = area.getX();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (logo.isValid()) g.drawImage (logo, R (x, cy - 13, 28, 26), juce::RectanglePlacement::centred);
    x += 36;
    {
        const juce::Point<float> c (x + 12, cy);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.55f), c.x, c.y, accent.withAlpha (0.0f), c.x + 14, c.y, true));
        g.fillEllipse (R (28, 28).withCentre (c));
        g.setColour (accentLit);
        g.drawEllipse (R (17, 17).withCentre (c), 1.2f);
        g.setColour (ink);
        g.fillEllipse (R (16, 16).withCentre (c.translated (2.2f, -1.0f)));
    }
    x += 32;
    text (g, "ONIRIQUE", R (x, cy - 16, 120, 14), font (Face::display, 11.0f, 0.14f), bone.withAlpha (0.75f), juce::Justification::centredLeft);
    text (g, "ECLIPSE", R (x - 1, cy - 4, 160, 22), font (Face::display, 20.0f, 0.1f), accent, juce::Justification::centredLeft);
    const float w = juce::GlyphArrangement::getStringWidth (font (Face::display, 20.0f, 0.1f), "ECLIPSE");
    text (g, "PARALLEL M/S MASTERING EQ", R (x + w + 10, cy + 1, 170, 14), font (Face::body, 8.5f, 0.14f), dim, juce::Justification::centredLeft);
}

void EclipseEditor::paint (juce::Graphics& g)
{
    if (background.isValid())
        g.drawImage (background, getLocalBounds().toFloat());
    else
        g.fillAll (ui::ink);
}
