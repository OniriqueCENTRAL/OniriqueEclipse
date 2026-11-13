#include "PluginEditor.h"
#include "OniriqueWindowSize.h"
#include "BinaryData.h"

namespace
{
const juce::String pluginName = "ONIRIQUE ECLIPSE";
using R = juce::Rectangle<float>;

// la façade, en points (sous la barre de 50)
const R displayArea { 20, 62, 950, 270 }, meterArea { 982, 62, 198, 270 };
const R lowPanel { 20, 346, 300, 238 }, midPanel { 332, 346, 536, 238 }, highPanel { 880, 346, 300, 238 };
const R filterPanel { 20, 598, 220, 190 }, msPanel { 252, 598, 340, 190 }, analogPanel { 604, 598, 150, 190 },
        monitorPanel { 766, 598, 200, 190 }, outPanel { 978, 598, 202, 190 };
constexpr float titleH = 26.0f;

// tailles des potards (composant entier : étiquette + bouton + valeur)
const juce::Point<float> big { 112, 140 }, medium { 86, 110 }, small { 72, 94 };
} // namespace

//==============================================================================
EclipseEditor::EclipseEditor (EclipseProcessor& proc)
    : AudioProcessorEditor (proc), processor (proc),
      curve (proc.apvts, [&proc] { return proc.readParams(); }, [&proc] { return proc.getWorkingRate(); }),
      meterPanel (proc.meters()),
      lowBoost (p (params::lowBoost), "BOOST", knobLook), lowFreq (p (params::lowFreq), "FREQ", knobLook), lowAtten (p (params::lowAtten), "ATTEN", knobLook),
      midQ (p (params::midQ), "Q", knobLook), midGain (p (params::midGain), "MID", knobLook, true), midFreq (p (params::midFreq), "FREQ", knobLook),
      contour (p (params::contour), "CONTOUR", knobLook, true), contourFreq (p (params::contourFreq), "FREQ", knobLook),
      highBoost (p (params::highBoost), "BOOST", knobLook), highFreq (p (params::highFreq), "FREQ", knobLook), highAtten (p (params::highAtten), "ATTEN", knobLook),
      hpfOn (p (params::hpfOn), "HPF"), lpfOn (p (params::lpfOn), "LPF"),
      hpfFreq (p (params::hpfFreq), "FREQ", knobLook), lpfFreq (p (params::lpfFreq), "FREQ", knobLook),
      msLink (p (params::msLink), "M/S LINK"),
      target (p (params::offsetTarget), { "SIDE", "MID", "SPLIT" }),
      lowOfs (p (params::lowOffset), "LOW", knobLook, true), midOfs (p (params::midOffset), "MID", knobLook, true),
      conOfs (p (params::contourOffset), "CONTOUR", knobLook, true), highOfs (p (params::highOffset), "HIGH", knobLook, true),
      analog (p (params::analog), "ANALOG"), character (p (params::character), "CHARACTER", knobLook),
      solo (p (params::solo), { "STEREO", "MID", "SIDE" }),
      delta (p (params::delta), "DELTA"), mono (p (params::mono), "MONO"),
      width (p (params::width), "WIDTH", knobLook),
      inGain (p (params::inGain), "INPUT", knobLook, true), outGain (p (params::outGain), "OUTPUT", knobLook, true),
      autoGain (p (params::autoGain), "AUTO GAIN"),
      linkWatch (p (params::msLink), [this] (float v)
      {
          const bool linked = v > 0.5f;
          for (auto* k : { &lowOfs, &midOfs, &conOfs, &highOfs }) k->setAlpha (linked ? 0.38f : 1.0f);
          target.setAlpha (linked ? 0.38f : 1.0f);
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
             &curve, &meterPanel, &lowBoost, &lowFreq, &lowAtten, &midQ, &midGain, &midFreq, &contour, &contourFreq,
             &highBoost, &highFreq, &highAtten, &hpfOn, &lpfOn, &hpfFreq, &lpfFreq, &msLink, &target, &lowOfs, &midOfs, &conOfs, &highOfs,
             &analog, &character, &solo, &delta, &mono, &width, &inGain, &outGain, &autoGain })
        addAndMakeVisible (c);
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
    startTimerHz (4);

    // essais locaux : ONIRIQUE_UI_PNG=fichier → l'interface s'écrit elle-même en PNG (rendu logiciel)
    if (auto* png = std::getenv ("ONIRIQUE_UI_PNG"))
    {
        const juce::String path (png);
        juce::Component::SafePointer<EclipseEditor> safe (this);
        if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("browser"))
            juce::Timer::callAfterDelay (500, [safe] { if (safe != nullptr) safe->bar.openBrowser(); });
        juce::Timer::callAfterDelay (1200, [safe, path]
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

//==============================================================================
void EclipseEditor::resized()
{
    const float s = scale();
    auto place = [s] (juce::Component& c, R r) { c.setBounds (R (r.getX() * s, r.getY() * s, r.getWidth() * s, r.getHeight() * s).toNearestInt()); };
    // un potard centré sur (cx, cy) : centre du composant entier
    auto knob = [&] (ui::Knob& k, float cx, float cy, juce::Point<float> sz)
    {
        place (k, R (sz.x, sz.y).withCentre ({ cx, cy }));
        k.setScale (s);
    };
    auto sw = [&] (ui::Switch& b, R r) { place (b, r); b.setScale (s); };

    place (curve, displayArea); curve.setScale (s);
    place (meterPanel, meterArea); meterPanel.setScale (s);

    // trois grandes sections
    auto row3 = [&] (R panel, ui::Knob& left, ui::Knob& centre, ui::Knob& right)
    {
        const float cy = panel.getY() + titleH + (panel.getHeight() - titleH) * 0.5f;
        knob (left, panel.getX() + 54, cy + 16, medium);
        knob (centre, panel.getCentreX(), cy - 4, big);
        knob (right, panel.getRight() - 54, cy + 16, medium);
    };
    row3 (lowPanel, lowBoost, lowFreq, lowAtten);
    row3 (highPanel, highAtten, highFreq, highBoost);
    {
        const float cy = midPanel.getY() + titleH + (midPanel.getHeight() - titleH) * 0.5f;
        const float x0 = midPanel.getX();
        knob (midFreq, x0 + 52, cy + 16, medium);
        knob (midGain, x0 + 150, cy - 4, big);
        knob (midQ, x0 + 238, cy + 22, small);
        knob (contour, x0 + 380, cy - 4, big);
        knob (contourFreq, x0 + 476, cy + 16, medium);
    }

    // bas
    {
        const float top = filterPanel.getY() + titleH + 6;
        sw (hpfOn, R (filterPanel.getX() + 14, top, 90, 24));
        sw (lpfOn, R (filterPanel.getRight() - 104, top, 90, 24));
        knob (hpfFreq, filterPanel.getX() + 59, top + 92, medium);
        knob (lpfFreq, filterPanel.getRight() - 59, top + 92, medium);
    }
    {
        const float top = msPanel.getY() + titleH + 6;
        sw (msLink, R (msPanel.getX() + 14, top, 110, 24));
        place (target, R (msPanel.getX() + 134, top, 192, 24)); target.setScale (s);
        const float w = (msPanel.getWidth() - 20) / 4.0f;
        int i = 0;
        for (auto* k : { &lowOfs, &midOfs, &conOfs, &highOfs })
            knob (*k, msPanel.getX() + 10 + w * ((float) i++ + 0.5f), top + 88, small);
    }
    {
        const float top = analogPanel.getY() + titleH + 6;
        sw (analog, R (analogPanel.getX() + 20, top, analogPanel.getWidth() - 40, 24));
        knob (character, analogPanel.getCentreX(), top + 92, medium);
    }
    {
        const float top = monitorPanel.getY() + titleH + 6;
        place (solo, R (monitorPanel.getX() + 10, top, monitorPanel.getWidth() - 20, 24)); solo.setScale (s);
        sw (delta, R (monitorPanel.getX() + 10, top + 32, 86, 24));
        sw (mono, R (monitorPanel.getRight() - 96, top + 32, 86, 24));
        knob (width, monitorPanel.getCentreX(), top + 112, small);
    }
    {
        const float top = outPanel.getY() + titleH + 4;
        knob (inGain, outPanel.getX() + 52, top + 56, medium);
        knob (outGain, outPanel.getRight() - 52, top + 56, medium);
        sw (autoGain, R (outPanel.getX() + 36, top + 120, outPanel.getWidth() - 72, 24));
    }

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

//==============================================================================
void EclipseEditor::renderBackground()
{
    const float k = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    background = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * k)),
                              juce::jmax (1, juce::roundToInt ((float) getHeight() * k)), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale() * k));
    using namespace ui;

    // la DA ONIRIQUE : fond nuit et ses taches de braise (même dessin que VOCALBOX), puis les panneaux en verre
    {
        const float u = scale() * k;
        const auto night = onirique::da::backdrop (background.getWidth(), background.getHeight(), u);
        juce::Graphics::ScopedSaveState keep (g);
        g.addTransform (juce::AffineTransform::scale (1.0f / u));
        g.drawImageAt (night, 0, 0);
    }
    const float u1 = 1.0f;   // les panneaux se dessinent en points (le zoom est déjà dans la transformation)
    onirique::da::glass (g, displayArea, 14.0f, u1);
    onirique::da::glass (g, meterArea, 14.0f, u1);

    auto panel = [&] (R r, const juce::String& title, const juce::String& sub = {})
    {
        onirique::da::glass (g, r, 14.0f, u1);
        if (title.isEmpty()) return;
        const auto f = font (Face::display, 11.0f, 0.18f);
        text (g, title, R (r.getX() + 14, r.getY() + 6, 200, 16), f, bone, juce::Justification::centredLeft);
        const float tw = juce::GlyphArrangement::getStringWidth (f, title);
        g.setColour (accent);
        g.fillRoundedRectangle (r.getX() + 14, r.getY() + 23, std::min (tw, 22.0f), 2.0f, 1.0f);
        if (sub.isNotEmpty())
            text (g, sub, R (r.getX() + 22 + tw, r.getY() + 6, 260, 16), font (Face::body, 9.5f, 0.12f), dim, juce::Justification::centredLeft);
    };
    panel (lowPanel, "LOW", "PASSIVE SHELF");
    panel (midPanel, "MID", "BELL");
    panel (highPanel, "HIGH", "PASSIVE SHELF");
    panel (filterPanel, "FILTERS", "SHELVING");
    panel (msPanel, "M/S", "OFFSET");
    panel (analogPanel, "ANALOG");
    panel (monitorPanel, "MONITOR");
    panel (outPanel, "OUTPUT");

    // le CONTOUR, bien à part dans la section MID : son cadre braise et son titre
    {
        const R cr (midPanel.getX() + 292, midPanel.getY() + 30, 232, midPanel.getHeight() - 40);
        onirique::da::seg (g, cr, 0.32f, 0.0f, u1, 12.0f);
        text (g, "MID CONTOUR", R (cr.getX(), cr.getY() + 5, cr.getWidth(), 14), font (Face::display, 9.5f, 0.2f), onirique::look::emberPink);
        // la forme de l'ondulation, gravée
        juce::Path w;
        for (int i = 0; i <= 60; ++i)
        {
            const float t = (float) i / 60.0f, x = cr.getX() + 70 + t * 92;
            const float y = cr.getBottom() - 14 + 5.0f * std::cos (t * juce::MathConstants<float>::twoPi * 1.0f) * std::exp (-3.0f * (t - 0.5f) * (t - 0.5f));
            if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
        }
        g.setColour (onirique::look::emberPink.withAlpha (0.6f));
        g.strokePath (w, juce::PathStrokeType (1.2f));
    }
    // séparation entre le gain de la cloche et son Q
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine ((int) (midPanel.getX() + 284), midPanel.getY() + 34, midPanel.getBottom() - 10);
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
    // l'éclipse
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
    const auto smallFont = font (Face::display, 11.0f, 0.14f);
    text (g, "ONIRIQUE", R (x, cy - 16, 120, 14), smallFont, bone.withAlpha (0.75f), juce::Justification::centredLeft);
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
