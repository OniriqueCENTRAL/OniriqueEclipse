#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../Params.h"

//==============================================================================
//  La courbe centrale : la somme des traitements parallèles du Mid (champagne) et du Side (argent), calculée par la même
//  fonction que le traitement (eclipse::Response, sur les vrais coefficients, interaction des bandes comprise), et
//  l'apport de chaque section en filigrane. Les quatre poignées se déplacent à la souris :
//      LOW / HIGH : fréquence ↔, renfort moins atténuation ↕       MID : fréquence ↔, gain ↕, molette = Q
//      CONTOUR : fréquence ↔, ↕ creuse (vers le bas) ou relève (vers le haut) le centre      double-clic : remise à zéro
//==============================================================================
namespace ui
{
class CurveDisplay : public juce::Component, private juce::Timer
{
public:
    CurveDisplay (juce::AudioProcessorValueTreeState& s, std::function<eclipse::EngineParams()> paramsFn, std::function<double()> rateFn)
        : state (s), getParams (std::move (paramsFn)), getRate (std::move (rateFn))
    {
        setTitle ("EQ curve");
        startTimerHz (40);
        recompute (true);
    }

    void setScale (float sc) { scale = sc; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto r = plot();
        // creux de l'écran
        g.setColour (juce::Colours::black.withAlpha (0.28f));   // le verre du panneau est dans le fond de la fenêtre
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f * scale);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.05f), r.getCentreX(), r.getY(), juce::Colours::transparentBlack, r.getCentreX(), r.getBottom(), false));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f * scale);

        // grille
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                          1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
        {
            const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
            g.setColour (juce::Colours::white.withAlpha (major ? 0.07f : 0.03f));
            g.drawVerticalLine ((int) xFor (f), r.getY(), r.getBottom());
        }
        for (int db = -15; db <= 15; db += 5)
        {
            g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.12f : 0.04f));
            g.drawHorizontalLine ((int) yFor (db), r.getX(), r.getRight());
            if (db != 0 && db % 5 == 0)
                text (g, (db > 0 ? "+" : "") + juce::String (db), { r.getRight() + 2.0f * scale, yFor (db) - 6 * scale, 24 * scale, 12 * scale },
                      font (Face::mono, 8.5f * scale), dim, juce::Justification::centredLeft);
        }
        for (auto [f, t] : { std::pair<double, const char*> { 30, "30" }, { 100, "100" }, { 300, "300" }, { 1000, "1k" }, { 3000, "3k" }, { 10000, "10k" } })
            text (g, t, { xFor (f) - 20 * scale, r.getBottom() + 2 * scale, 40 * scale, 12 * scale }, font (Face::mono, 8.5f * scale), dim);

        // HPF / LPF
        auto filterMark = [&] (double f, bool on)
        {
            if (! on) return;
            const float x = xFor (f);
            g.setColour (haze.withAlpha (0.3f));
            for (float y = r.getY(); y < r.getBottom(); y += 6 * scale) g.drawLine (x, y, x, y + 3 * scale, 1.0f);
        };
        filterMark (paramValue (params::hpfFreq), paramValue (params::hpfOn) > 0.5);
        filterMark (paramValue (params::lpfFreq), paramValue (params::lpfOn) > 0.5);

        // apport de chaque section (Mid)
        const juce::Colour bandCol[4] { onirique::look::emberPink, accent, onirique::da::mist, onirique::look::emberLit };
        for (int b = 0; b < 4; ++b)
        {
            juce::Path p;
            pathFor (p, shown.band[b]);
            g.setColour (bandCol[b].withAlpha (0.28f));
            g.strokePath (p, juce::PathStrokeType (1.0f * scale));
        }

        // Side (si différent du Mid)
        if (sideVisible)
        {
            juce::Path ps;
            pathFor (ps, shown.side);
            juce::Path dashed;
            const float dash[] { 5.0f * scale, 4.0f * scale };
            juce::PathStrokeType (1.4f * scale).createDashedStroke (dashed, ps, dash, 2);
            g.setColour (side.withAlpha (0.85f));
            g.fillPath (dashed);
        }

        // Mid : remplissage vers 0 dB et trait lumineux
        {
            juce::Path pm;
            pathFor (pm, shown.mid);
            juce::Path fill (pm);
            fill.lineTo (r.getRight(), yFor (0)); fill.lineTo (r.getX(), yFor (0)); fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.16f), 0, r.getY(), accent.withAlpha (0.03f), 0, yFor (0), false));
            g.fillPath (fill);
            g.setColour (accent.withAlpha (0.20f));
            g.strokePath (pm, juce::PathStrokeType (5.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.interpolatedWith (juce::Colours::white, 0.2f));
            g.strokePath (pm, juce::PathStrokeType (1.9f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // légende et interaction
        {
            auto lg = juce::Rectangle<float> (r.getX() + 8 * scale, r.getY() + 6 * scale, 200 * scale, 14 * scale);
            g.setColour (accent); g.fillRoundedRectangle (lg.getX(), lg.getCentreY() - 1.0f * scale, 14 * scale, 2.2f * scale, 1.0f);
            text (g, "MID", lg.withTrimmedLeft (19 * scale), font (Face::body, 9.5f * scale, 0.14f), haze, juce::Justification::centredLeft);
            if (sideVisible)
            {
                g.setColour (side); g.fillRoundedRectangle (lg.getX() + 52 * scale, lg.getCentreY() - 1.0f * scale, 14 * scale, 2.0f * scale, 1.0f);
                text (g, "SIDE", lg.withTrimmedLeft (71 * scale), font (Face::body, 9.5f * scale, 0.14f), haze, juce::Justification::centredLeft);
            }
            if (interaction < 0.995)
                text (g, "BAND INTERACTION  " + juce::String ((interaction - 1.0) * 100.0, 0) + " % BOOST",
                      { r.getRight() - 220 * scale, r.getY() + 6 * scale, 212 * scale, 14 * scale }, font (Face::mono, 9.0f * scale), haze,
                      juce::Justification::centredRight);
        }

        // poignées
        for (int i = 0; i < 4; ++i)
        {
            const auto c = handle (i);
            const bool hot = i == hover || i == dragging;
            g.setColour (bandCol[i].withAlpha (hot ? 0.35f : 0.18f));
            g.fillEllipse (juce::Rectangle<float> (22 * scale, 22 * scale).withCentre (c));
            g.setColour (ink.withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (15 * scale, 15 * scale).withCentre (c));
            g.setColour (bandCol[i].interpolatedWith (juce::Colours::white, hot ? 0.3f : 0.0f));
            g.drawEllipse (juce::Rectangle<float> (15 * scale, 15 * scale).withCentre (c), 1.4f * scale);
            text (g, juce::String::charToString ("LMCH"[i]), juce::Rectangle<float> (15 * scale, 15 * scale).withCentre (c),
                  font (Face::body, 8.5f * scale), bone);
        }
        if (dragging >= 0 || hover >= 0)
        {
            const int i = dragging >= 0 ? dragging : hover;
            const auto c = handle (i);
            static const char* names[4] { "LOW", "MID", "CONTOUR", "HIGH" };
            juce::String t = juce::String (names[i]) + "  " + params::hzText ((float) paramValue (freqId (i)));
            t << "  " << params::dbText ((float) handleDb (i));
            if (i == 1) t << "  Q " << juce::String (paramValue (params::midQ), 2);
            const auto f = font (Face::mono, 9.5f * scale);
            const float w = juce::GlyphArrangement::getStringWidth (f, t) + 16 * scale;
            auto box = juce::Rectangle<float> (w, 18 * scale).withCentre ({ c.x, c.y - 22 * scale });
            box = box.constrainedWithin (getLocalBounds().toFloat().reduced (4 * scale));
            g.setColour (ink.withAlpha (0.9f)); g.fillRoundedRectangle (box, 9 * scale);
            g.setColour (accent.withAlpha (0.5f)); g.drawRoundedRectangle (box, 9 * scale, 1.0f);
            text (g, t, box, f, bone);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = hit (e.position);
        if (dragging < 0) return;
        for (auto* id : idsFor (dragging)) if (auto* p = state.getParameter (*id)) p->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto r = plot();
        const double f = 20.0 * std::pow (1100.0, juce::jlimit (0.0, 1.0, (double) ((e.position.x - r.getX()) / r.getWidth())));
        const double db = juce::jlimit (-15.0, 15.0, 15.0 - 30.0 * (double) ((e.position.y - r.getY()) / r.getHeight()));
        set (freqId (dragging), f);
        switch (dragging)
        {
            case 0: shelf (params::lowBoost, params::lowAtten, db); break;
            case 1: set (params::midGain, db); break;
            case 2: set (params::contour, -db); break;
            default: shelf (params::highBoost, params::highAtten, db); break;
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0) return;
        for (auto* id : idsFor (dragging)) if (auto* p = state.getParameter (*id)) p->endChangeGesture();
        dragging = -1;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = hit (e.position);
        if (i < 0) return;
        for (auto* id : idsFor (i))
            if (*id != freqId (i))
                if (auto* p = state.getParameter (*id))
                {
                    p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
                }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (hit (e.position) != 1) return;
        if (auto* p = state.getParameter (params::midQ))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.12f));
            p->endChangeGesture();
        }
    }

private:
    static constexpr int N = 240;
    struct Curves { float mid[N] {}, side[N] {}, band[4][N] {}; };

    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (12 * scale).withTrimmedRight (30 * scale).withTrimmedTop (10 * scale).withTrimmedBottom (18 * scale);
    }
    float xFor (double f) const { const auto r = plot(); return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1100.0)); }
    float yFor (double db) const { const auto r = plot(); return r.getY() + r.getHeight() * (float) ((15.0 - juce::jlimit (-17.0, 17.0, db)) / 30.0); }
    static double freqAt (int i) { return 20.0 * std::pow (1100.0, (double) i / (N - 1)); }

    void pathFor (juce::Path& p, const float* db) const
    {
        for (int i = 0; i < N; ++i)
        {
            const float x = xFor (freqAt (i)), y = yFor (db[i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
    }

    double paramValue (const juce::String& id) const { return (double) state.getRawParameterValue (id)->load(); }
    void set (const juce::String& id, double v)
    {
        if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    }
    /** une poignée de LOW / HIGH : sa hauteur = renfort - atténuation ; on bouge l'un sans toucher l'autre tant que possible */
    void shelf (const juce::String& boostId, const juce::String& attenId, double net)
    {
        const double b = paramValue (boostId), a = paramValue (attenId);
        if (net >= 0.0)
        {
            // au-dessus de zéro : le renfort bouge, l'atténuation reste (sauf s'il faut la réduire pour monter encore)
            const double nb = net + a;
            if (nb <= 12.0) set (boostId, nb);
            else { set (boostId, 12.0); set (attenId, std::max (0.0, 12.0 - net)); }
        }
        else
        {
            // en dessous : l'atténuation bouge, le renfort reste (même règle)
            const double na = b - net;
            if (na <= 12.0) set (attenId, na);
            else { set (attenId, 12.0); set (boostId, std::max (0.0, 12.0 + net)); }
        }
    }
    static const juce::String& freqId (int i)
    {
        static const juce::String* ids[4] { &params::lowFreq, &params::midFreq, &params::contourFreq, &params::highFreq };
        return *ids[i];
    }
    static std::vector<const juce::String*> idsFor (int i)
    {
        switch (i)
        {
            case 0: return { &params::lowFreq, &params::lowBoost, &params::lowAtten };
            case 1: return { &params::midFreq, &params::midGain };
            case 2: return { &params::contourFreq, &params::contour };
            default: return { &params::highFreq, &params::highBoost, &params::highAtten };
        }
    }
    double handleDb (int i) const
    {
        switch (i)
        {
            case 0: return paramValue (params::lowBoost) - paramValue (params::lowAtten);
            case 1: return paramValue (params::midGain);
            case 2: return -paramValue (params::contour);
            default: return paramValue (params::highBoost) - paramValue (params::highAtten);
        }
    }
    juce::Point<float> handle (int i) const { return { xFor (paramValue (freqId (i))), yFor (handleDb (i)) }; }
    int hit (juce::Point<float> p) const
    {
        int best = -1; float bestD = 13.0f * scale;
        for (int i = 0; i < 4; ++i) { const float d = handle (i).getDistanceFrom (p); if (d < bestD) { bestD = d; best = i; } }
        return best;
    }

    void timerCallback() override
    {
        recompute (false);
        if (onirique::look::changedSince (themeSeen)) repaint();
    }

    void recompute (bool jump)
    {
        const auto p = getParams();
        const double fs = std::max (44100.0, getRate());
        const auto m = eclipse::channelSettings (p, 0), s = eclipse::channelSettings (p, 1);
        const bool changed = ! (m == lastM && s == lastS && p.width == lastW && fs == lastFs);
        if (changed)
        {
            lastM = m; lastS = s; lastW = p.width; lastFs = fs;
            const auto t = eclipse::Targets::compute (p, fs);
            interaction = std::min (t.scale[0], t.scale[1]);
            const eclipse::Response r (fs);
            const auto dm = eclipse::Design::make (t.ch[0], fs), ds = eclipse::Design::make (t.ch[1], fs);
            // l'apport de chaque section seule (Mid), sans les plateaux HPF / LPF
            eclipse::Design only[4];
            for (int b = 0; b < 4; ++b)
            {
                eclipse::Settings x;
                const auto& c = t.ch[0];
                x.lowF = c.lowF; x.midF = c.midF; x.midQ = c.midQ; x.conF = c.conF; x.highF = c.highF;
                if (b == 0) { x.lowB = c.lowB; x.lowA = c.lowA; }
                if (b == 1) x.midG = c.midG;
                if (b == 2) x.conV = c.conV;
                if (b == 3) { x.highB = c.highB; x.highA = c.highA; }
                only[b] = eclipse::Design::make (x, fs);
            }
            const double sideGain = std::max (1.0e-6, p.width);
            for (int i = 0; i < N; ++i)
            {
                const double f = freqAt (i);
                target.mid[i] = (float) eclipse::gainToDb (std::abs (r.total (dm, f)));
                target.side[i] = (float) eclipse::gainToDb (std::abs (r.total (ds, f)) * sideGain);
                for (int b = 0; b < 4; ++b) target.band[b][i] = (float) eclipse::gainToDb (std::abs (1.0 + r.deviation (only[b], f)));
            }
            bool differ = false;
            for (int i = 0; i < N && ! differ; ++i) differ = std::abs (target.mid[i] - target.side[i]) > 0.05f;
            sideVisible = differ;
        }
        // la courbe affichée rejoint la cible en douceur
        bool moving = false;
        auto ease = [&] (float* a, const float* b)
        {
            for (int i = 0; i < N; ++i)
            {
                const float d = b[i] - a[i];
                if (jump || std::abs (d) < 0.01f) a[i] = b[i]; else { a[i] += 0.35f * d; moving = true; }
            }
        };
        ease (shown.mid, target.mid); ease (shown.side, target.side);
        for (int b = 0; b < 4; ++b) ease (shown.band[b], target.band[b]);
        if (moving || changed) repaint();
    }

    juce::AudioProcessorValueTreeState& state;
    std::function<eclipse::EngineParams()> getParams;
    std::function<double()> getRate;
    Curves shown, target;
    eclipse::Settings lastM, lastS;
    double lastW = -1, lastFs = 0, interaction = 1.0;
    bool sideVisible = false;
    int hover = -1, dragging = -1, themeSeen = -1;
    float scale = 1.0f;
};

//==============================================================================
/** Mesures : crête et RMS d'entrée et de sortie (L / R), maintien des crêtes, corrélation (compatibilité mono), niveaux
    Mid / Side et compensation du gain auto. */
class MeterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MeterPanel (eclipse::Meters& m) : meters (m) { startTimerHz (30); setInterceptsMouseClicks (true, false); }
    void setScale (float sc) { scale = sc; repaint(); }

    void mouseDown (const juce::MouseEvent&) override { for (auto& h : hold) h = -100.0f; repaint(); }   // remise à zéro des crêtes

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (b, 10 * scale);
        auto r = b.reduced (10 * scale, 8 * scale);
        auto bars = r.removeFromTop (r.getHeight() * 0.64f);
        auto scaleCol = bars.removeFromLeft (22 * scale);
        const float top = bars.getY() + 14 * scale, bottom = bars.getBottom() - 2 * scale;
        auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (-48.0f, 6.0f, db), -48.0f, 6.0f, bottom, top); };
        for (int db : { 6, 0, -6, -12, -18, -24, -36, -48 })
        {
            text (g, juce::String (db), { scaleCol.getX(), yFor ((float) db) - 5 * scale, scaleCol.getWidth() - 2 * scale, 10 * scale },
                  font (Face::mono, 8.0f * scale), db == 0 ? haze : dim, juce::Justification::centredRight);
        }
        const float colW = bars.getWidth() / 4.0f;
        const char* names[4] { "L", "R", "L", "R" };
        for (int i = 0; i < 4; ++i)
        {
            auto col = juce::Rectangle<float> (bars.getX() + colW * (float) i, top, colW, bottom - top).reduced (colW * 0.2f, 0);
            if (i >= 2) col = col.translated (4 * scale, 0);
            g.setColour (ink);
            g.fillRoundedRectangle (col, 2 * scale);
            const float rmsY = yFor (rms[i]), pkY = yFor (peak[i]), hY = yFor (hold[i]);
            auto fill = col.withTop (rmsY);
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.95f), 0, top, accent.withAlpha (0.35f), 0, bottom, false));
            g.fillRect (fill);
            if (rms[i] > 0.0f) { g.setColour (juce::Colour (0xffff5a4a)); g.fillRect (col.withBottom (yFor (0.0f))); }
            g.setColour (bone.withAlpha (0.75f));
            g.fillRect (col.getX(), pkY - 0.5f, col.getWidth(), 1.2f * scale);
            g.setColour ((hold[i] > 0.0f ? juce::Colour (0xffff5a4a) : accentLit).withAlpha (0.9f));
            g.fillRect (col.getX(), hY - 0.5f, col.getWidth(), 1.0f * scale);
            text (g, names[i], { col.getX() - 4 * scale, bottom + 1 * scale, col.getWidth() + 8 * scale, 10 * scale }, font (Face::mono, 8.0f * scale), dim);
        }
        text (g, "IN", { bars.getX(), bars.getY(), colW * 2, 12 * scale }, font (Face::body, 9.0f * scale, 0.16f), haze);
        text (g, "OUT", { bars.getX() + colW * 2 + 4 * scale, bars.getY(), colW * 2, 12 * scale }, font (Face::body, 9.0f * scale, 0.16f), haze);
        text (g, juce::String (hold[2] > -99 ? juce::String (std::max (hold[2], hold[3]), 1) : "-inf") + " PK",
              { bars.getX() + colW * 2, bottom + 11 * scale, colW * 2 + 8 * scale, 11 * scale }, font (Face::mono, 8.0f * scale), haze);

        r.removeFromTop (16 * scale);
        // corrélation
        auto corrRow = r.removeFromTop (30 * scale);
        text (g, "MONO COMPAT", corrRow.removeFromTop (12 * scale), font (Face::body, 8.5f * scale, 0.14f), haze, juce::Justification::centredLeft);
        auto bar = corrRow.reduced (0, 5 * scale);
        g.setColour (ink); g.fillRoundedRectangle (bar, 3 * scale);
        g.setColour (juce::Colours::white.withAlpha (0.15f)); g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());
        {
            const float x = juce::jmap (corr, -1.0f, 1.0f, bar.getX(), bar.getRight());
            const auto col = corr < 0.0f ? juce::Colour (0xffff5a4a) : accent;
            g.setColour (col.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (x, bar.getCentreX()), bar.getY(), std::max (x, bar.getCentreX()), bar.getBottom()));
            g.setColour (col); g.fillRect (x - 1.5f * scale, bar.getY() - 1, 3 * scale, bar.getHeight() + 2);
        }
        text (g, "-1", bar.withWidth (14 * scale).translated (0, 9 * scale), font (Face::mono, 7.5f * scale), dim, juce::Justification::centredLeft);
        text (g, "+1", bar.withTrimmedLeft (bar.getWidth() - 14 * scale).translated (0, 9 * scale), font (Face::mono, 7.5f * scale), dim, juce::Justification::centredRight);

        r.removeFromTop (10 * scale);
        // Mid / Side
        auto msRow = [&] (const char* name, float db, juce::Colour c)
        {
            auto row = r.removeFromTop (14 * scale);
            text (g, name, row.removeFromLeft (30 * scale), font (Face::body, 8.5f * scale, 0.12f), haze, juce::Justification::centredLeft);
            auto bb = row.reduced (0, 4 * scale);
            g.setColour (ink); g.fillRoundedRectangle (bb, 2 * scale);
            g.setColour (c.withAlpha (0.85f));
            g.fillRect (bb.withWidth (bb.getWidth() * juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, 1.0f)));
        };
        msRow ("MID", midDb, accent);
        msRow ("SIDE", sideDb, side);
        r.removeFromTop (6 * scale);
        text (g, "AUTO GAIN  " + (std::abs (ag) < 0.05f ? juce::String ("0.0") : (ag > 0 ? "+" : "") + juce::String (ag, 1)) + " dB",
              r.removeFromTop (14 * scale), font (Face::mono, 8.8f * scale), std::abs (ag) < 0.05f ? dim : accent, juce::Justification::centredLeft);
    }

private:
    void timerCallback() override
    {
        auto toDb = [] (float v) { return v > 1.0e-6f ? 20.0f * std::log10 (v) : -100.0f; };
        const float in[4] { meters.inPeak[0].exchange (0.0f), meters.inPeak[1].exchange (0.0f), meters.outPeak[0].exchange (0.0f), meters.outPeak[1].exchange (0.0f) };
        const float rm[4] { meters.inRms[0].load(), meters.inRms[1].load(), meters.outRms[0].load(), meters.outRms[1].load() };
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < 4; ++i)
        {
            const float pk = toDb (in[i]);
            peak[i] = pk > peak[i] ? pk : peak[i] - 1.2f;                       // retombée ~36 dB/s
            const float target = toDb (rm[i] * 1.41421356f);                   // RMS aligné sur la crête d'un sinus
            rms[i] += (target - rms[i]) * (target > rms[i] ? 0.5f : 0.25f);
            if (pk >= hold[i]) { hold[i] = pk; holdAt[i] = now; }
            else if (now - holdAt[i] > 1500) hold[i] = std::max (-100.0f, hold[i] - 0.8f);
        }
        corr += (meters.corr.load() - corr) * 0.25f;
        midDb += (toDb (meters.midRms.load() * 1.41421356f) - midDb) * 0.3f;
        sideDb += (toDb (meters.sideRms.load() * 1.41421356f) - sideDb) * 0.3f;
        ag = meters.autoGainDb.load();
        if (onirique::look::changedSince (themeSeen)) {}
        repaint();
    }

    eclipse::Meters& meters;
    float peak[4] { -100, -100, -100, -100 }, rms[4] { -100, -100, -100, -100 }, hold[4] { -100, -100, -100, -100 };
    juce::uint32 holdAt[4] {};
    float corr = 1.0f, midDb = -100.0f, sideDb = -100.0f, ag = 0.0f, scale = 1.0f;
    int themeSeen = -1;
};
} // namespace ui
