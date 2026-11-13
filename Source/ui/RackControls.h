#pragma once

#include "Controls.h"

//==============================================================================
//  Les commandes propres à la façade « rack » d'ECLIPSE (même DA ONIRIQUE) : boutons ronds à témoin, SOLO M / SOLO S,
//  anneau MID·SIDE / OFFSET des grands potards, VU-mètre.
//==============================================================================
namespace ui
{
//==============================================================================
/** Bouton rond à témoin (POWER, HPF, LPF) : verre, LED braise. inverted : allumé quand le paramètre est à 0
    (POWER = pas de bypass). */
class LedButton : public juce::Button, private juce::Timer
{
public:
    LedButton (juce::RangedAudioParameter& p, const juce::String& t, bool invertedParam = false)
        : juce::Button (t), inverted (invertedParam),
          attachment (p, [this] (float v) { on = (v > 0.5f) != inverted; startTimerHz (60); })
    {
        setTitle (t);
        attachment.sendInitialUpdate();
        lit = on ? 1.0f : 0.0f;
        onClick = [this] { attachment.setValueAsCompleteGesture ((! on) != inverted ? 1.0f : 0.0f); };
    }
    void setScale (float s) { scale = s; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f * scale);
        const float d = std::min (r.getWidth(), r.getHeight());
        const auto c = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
        onirique::da::glass (g, c, d * 0.5f, scale, false);
        if (over) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillEllipse (c); }
        if (down) { g.setColour (juce::Colours::black.withAlpha (0.2f)); g.fillEllipse (c); }
        onirique::da::led (g, c.getCentre(), d * 0.36f, lit);
    }

private:
    void timerCallback() override
    {
        const float t = on ? 1.0f : 0.0f;
        lit += (t - lit) * 0.3f;
        if (std::abs (t - lit) < 0.01f) { lit = t; stopTimer(); }
        repaint();
    }
    bool inverted, on = false;
    juce::ParameterAttachment attachment;
    float lit = 0.0f, scale = 1.0f;
};

//==============================================================================
/** Une position d'un paramètre à choix, en gélule : SOLO M (1), SOLO S (2). Un clic y va, un second revient à 0. */
class ChoiceButton : public juce::Button
{
public:
    ChoiceButton (juce::RangedAudioParameter& p, int position, const juce::String& t)
        : juce::Button (t), pos (position), attachment (p, [this] (float v) { index = (int) std::lround (v); repaint(); })
    {
        setTitle (t);
        attachment.sendInitialUpdate();
        onClick = [this] { attachment.setValueAsCompleteGesture (index == pos ? 0.0f : (float) pos); };
    }
    void setScale (float s) { scale = s; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        fullPill (g, r, index == pos ? 1.0f : 0.0f, over ? 1.0f : 0.0f, scale);
        ui::text (g, getButtonText(), r, font (Face::body, 10.5f * scale, 0.14f), index == pos ? bone : haze);
    }

private:
    int pos, index = 0;
    juce::ParameterAttachment attachment;
    float scale = 1.0f;
};

//==============================================================================
/** L'anneau d'un grand potard de la façade rack. À gauche, deux repères : ce que reçoivent vraiment le Mid (braise) et
    le Side (brume) dans cette section (réglage + OFFSET selon la cible et M/S LINK). À droite, l'OFFSET de la section :
    un arc qui part du milieu, à tirer de haut en bas (double-clic : 0). Seule la moitié droite de l'anneau prend la
    souris ; ailleurs, le clic va au potard. */
class OffsetRing : public juce::Component, private juce::Timer
{
public:
    using Values = std::function<std::pair<double, double>()>;   // valeurs Mid / Side de la section, en dB
    OffsetRing (juce::RangedAudioParameter& offsetParam, double displayRange, Values v)
        : offset (offsetParam), range (displayRange), values (std::move (v)),
          attachment (offsetParam, [this] (float x) { ofs = x; repaint(); })
    {
        attachment.sendInitialUpdate();
        setTitle (offsetParam.getName (64));
        startTimerHz (20);
    }
    void setScale (float s) { scale = s; repaint(); }
    void setLinked (bool l) { linked = l; repaint(); }

    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float d = juce::Point<float> ((float) x, (float) y).getDistanceFrom (c), R = radius();
        return (float) x > c.x + R * 0.3f && d > R * 0.82f && d < R * 1.3f;
    }

    void paint (juce::Graphics& g) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const float R = radius();
        const float lw = 2.0f * scale;
        auto arc = [&] (float a0, float a1, juce::Colour col, float w)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, R, R, 0.0f, juce::degreesToRadians (a0), juce::degreesToRadians (a1), true);
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        auto dot = [&] (float a, juce::Colour col, float d)
        {
            juce::Path e; e.addEllipse (juce::Rectangle<float> (d, d).withCentre (polar (c, R, a)));
            onirique::da::softGlow (g, e, col.withAlpha (0.55f), d);
            g.setColour (col); g.fillPath (e);
        };
        // à gauche : de -150° (bas) à -30° (haut) ; à droite : de 30° (haut) à 150° (bas) ; le milieu = 0 dB
        arc (-150.0f, -30.0f, juce::Colours::white.withAlpha (0.10f), lw);
        arc (30.0f, 150.0f, juce::Colours::white.withAlpha (0.10f), lw);
        for (float a : { -90.0f, 90.0f })
        {
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.fillEllipse (juce::Rectangle<float> (3 * scale, 3 * scale).withCentre (polar (c, R, a)));
        }
        auto leftAngle = [&] (double v) { return -90.0f + 60.0f * (float) juce::jlimit (-1.0, 1.0, v / range); };   // + vers le haut
        dot (leftAngle (sideV), side, 6.5f * scale);
        dot (leftAngle (midV), accent, 6.5f * scale);

        const float top = offset.getNormalisableRange().end;
        const float a = 90.0f - 60.0f * juce::jlimit (-1.0f, 1.0f, ofs / top);
        const float alpha = linked ? 0.3f : 1.0f;
        if (std::abs (a - 90.0f) > 0.5f) arc (std::min (a, 90.0f), std::max (a, 90.0f), accent.withAlpha (0.9f * alpha), lw * 1.6f);
        dot (a, bone.withAlpha (alpha), (hover || dragging ? 8.0f : 6.0f) * scale);

        const auto f = font (Face::body, 8.5f * scale, 0.12f);
        auto label = [&] (const juce::String& t, float ang, float dx, float dy, juce::Colour col, juce::Font fo)
        {
            text (g, t, juce::Rectangle<float> (64 * scale, 12 * scale).withCentre (polar (c, R + 12 * scale, ang)).translated (dx * scale, dy * scale), fo, col);
        };
        label ("SIDE", -32.0f, -16.0f, -2.0f, side.withAlpha (0.85f), f);
        label ("MID", -150.0f, -10.0f, -10.0f, accent.withAlpha (0.9f), f);
        label ("OFFSET", 150.0f, 14.0f, -10.0f, haze.withAlpha (alpha), f);
        if (dragging || hover)
            label (offset.getCurrentValueAsText(), 32.0f, 20.0f, -2.0f, onirique::look::emberPink, font (Face::mono, 9.0f * scale));
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { dragging = true; startValue = ofs; startY = e.position.y; attachment.beginGesture(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto& r = offset.getNormalisableRange();
        attachment.setValueAsPartOfGesture (juce::jlimit (r.start, r.end, startValue - (e.position.y - startY) / (180.0f * scale) * (r.end - r.start)));
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; attachment.endGesture(); repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (0.0f); }

private:
    float radius() const { return 70.0f * scale; }   // autour du grand potard ; la zone est plus large pour les étiquettes
    void timerCallback() override
    {
        const auto v = values();
        if (std::abs (v.first - midV) > 0.01 || std::abs (v.second - sideV) > 0.01) { midV = v.first; sideV = v.second; repaint(); }
    }
    juce::RangedAudioParameter& offset;
    double range;
    Values values;
    juce::ParameterAttachment attachment;
    float ofs = 0.0f, startValue = 0.0f, startY = 0.0f, scale = 1.0f;
    double midV = 0.0, sideV = 0.0;
    bool hover = false, dragging = false, linked = true;
};

//==============================================================================
/** VU-mètre (300 ms, 0 VU = -18 dBFS RMS), sur l'entrée ou la sortie : clic sur IN / OUT. Cadran en verre, aiguille
    os, zone au-dessus de 0 VU en braise. */
class VuMeter : public juce::Component, private juce::Timer
{
public:
    explicit VuMeter (eclipse::Meters& m) : meters (m) { startTimerHz (40); setTitle ("VU"); }
    void setScale (float s) { scale = s; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.position.y < (float) getHeight() * 0.22f) { showInput = e.position.x < (float) getWidth() * 0.5f; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        // IN / OUT
        auto sel = b.withHeight (20 * scale).withSizeKeepingCentre (96 * scale, 20 * scale);
        onirique::da::glass (g, sel, sel.getHeight() * 0.5f, scale, false);
        const auto inR = sel.withWidth (sel.getWidth() * 0.5f).reduced (0.5f * scale), outR = inR.translated (sel.getWidth() * 0.5f, 0);
        onirique::da::seg (g, showInput ? inR : outR, 1.0f, 0.0f, scale, inR.getHeight() * 0.5f);
        text (g, "IN", inR, font (Face::body, 9.0f * scale, 0.14f), showInput ? bone : haze);
        text (g, "OUT", outR, font (Face::body, 9.0f * scale, 0.14f), showInput ? haze : bone);

        // cadran
        const float d = std::min (b.getWidth(), b.getHeight() - 26 * scale);
        const auto face = juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getBottom() - d * 0.5f });
        {
            juce::Path p; p.addEllipse (face);
            onirique::da::softGlow (g, p, accent.withAlpha (0.18f), 18 * scale);
        }
        onirique::da::glass (g, face, d * 0.5f, scale, false);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.10f), face.getCentreX(), face.getY(), juce::Colours::transparentBlack, face.getCentreX(), face.getBottom(), false));
        g.fillEllipse (face);
        const auto c = face.getCentre().translated (0, d * 0.16f);
        const float R = d * 0.40f;
        auto angleFor = [] (float vu) { return juce::jmap (juce::jlimit (-20.0f, 3.0f, vu), -20.0f, 3.0f, -52.0f, 52.0f); };
        // la graduation n'est pas linéaire : on suit l'échelle VU (en tension)
        auto ang = [&] (float vu) { const float v = std::pow (10.0f, vu / 20.0f); return juce::jmap (v, std::pow (10.0f, -1.0f), std::pow (10.0f, 0.15f), -52.0f, 52.0f); };
        juce::ignoreUnused (angleFor);
        {
            juce::Path redZone;
            redZone.addCentredArc (c.x, c.y, R, R, 0.0f, juce::degreesToRadians (ang (0.0f)), juce::degreesToRadians (ang (3.0f)), true);
            g.setColour (accent.withAlpha (0.85f));
            g.strokePath (redZone, juce::PathStrokeType (3.0f * scale));
            juce::Path scaleArc;
            scaleArc.addCentredArc (c.x, c.y, R, R, 0.0f, juce::degreesToRadians (ang (-20.0f)), juce::degreesToRadians (ang (0.0f)), true);
            g.setColour (bone.withAlpha (0.5f));
            g.strokePath (scaleArc, juce::PathStrokeType (1.0f * scale));
        }
        for (float vu : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const float a = ang (vu);
            const bool major = vu == -20.0f || vu == -10.0f || vu == -5.0f || vu == 0.0f || vu == 3.0f || vu == -3.0f;
            g.setColour ((vu > 0.0f ? accent : bone).withAlpha (0.8f));
            g.drawLine ({ polar (c, R - (major ? 7.0f : 4.0f) * scale, a), polar (c, R + 1.0f * scale, a) }, 1.0f * scale);
            if (major)
                text (g, vu > 0 ? "+" + juce::String ((int) vu) : juce::String ((int) vu),
                      juce::Rectangle<float> (24 * scale, 10 * scale).withCentre (polar (c, R + 9 * scale, a)), font (Face::mono, 7.5f * scale),
                      vu > 0 ? accent : haze);
        }
        text (g, "VU", juce::Rectangle<float> (40 * scale, 14 * scale).withCentre (c.translated (0, -R * 0.42f)), font (Face::display, 10.0f * scale, 0.2f), bone.withAlpha (0.8f));
        text (g, showInput ? "INPUT" : "OUTPUT", juce::Rectangle<float> (60 * scale, 10 * scale).withCentre (c.translated (0, -R * 0.2f)), font (Face::body, 7.5f * scale, 0.16f), haze);
        // aiguille
        const float a = ang (needle);
        juce::Path n;
        n.startNewSubPath (polar (c, R * 0.12f, a));
        n.lineTo (polar (c, R * 1.04f, a));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (n, juce::PathStrokeType (2.6f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (bone);
        g.strokePath (n, juce::PathStrokeType (1.4f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff1e1614));
        g.fillEllipse (juce::Rectangle<float> (12 * scale, 12 * scale).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawEllipse (juce::Rectangle<float> (12 * scale, 12 * scale).withCentre (c), 1.0f);
    }

private:
    void timerCallback() override
    {
        const float rms = showInput ? 0.5f * (meters.inRms[0].load() + meters.inRms[1].load())
                                    : 0.5f * (meters.outRms[0].load() + meters.outRms[1].load());
        const float vu = (rms > 1.0e-6f ? 20.0f * std::log10 (rms) : -100.0f) + 18.0f;   // 0 VU = -18 dBFS RMS
        // ballistique VU : ~300 ms pour 99 % (2e ordre amorti, à 40 images/s)
        const float target = juce::jlimit (-22.0f, 4.0f, vu);
        vel += (target - needle) * 0.12f;
        vel *= 0.62f;
        needle += vel;
        if (std::abs (needle - shown) > 0.02f) { shown = needle; repaint(); }
    }
    eclipse::Meters& meters;
    bool showInput = false;
    float needle = -22.0f, vel = 0.0f, shown = -100.0f, scale = 1.0f;
};
} // namespace ui
