// Banc de mesure du moteur ECLIPSE (console). Sans argument : tous les essais ; sinon les noms donnés.
//   null       réglages neutres, ANALOG éteint : la sortie = l'entrée retardée de la latence
//   curve      réponse mesurée (impulsion + FFT) contre la courbe affichée (Response), plusieurs réglages
//   ms         OFFSET : le Mid ne bouge pas quand l'offset va au Side ; largeur 0 = mono ; solo
//   delta      DELTA + original retardé = traité
//   interact   deux renforts qui se recouvrent : pic limité à 3 dB au-dessus du plus fort seul
//   autogain   bruit rose : écart de niveau avec GAIN AUTO
//   harm       harmoniques selon la sollicitation de la bande et le caractère
//   robust     réglages aléatoires à chaque bloc, entrées extrêmes : rien d'infini, rien d'énorme
//   clicks     sauts de réglages : pas de discontinuité
//   cpu        temps de calcul
#include <juce_dsp/juce_dsp.h>
#include "dsp/EclipseEngine.h"
#include <cstdio>
#include <chrono>

using namespace eclipse;

static int failures = 0;
static void check (bool ok, const char* what, double value)
{
    std::printf ("  [%s] %s : %.4f\n", ok ? "OK" : "ECHEC", what, value);
    if (! ok) ++failures;
}

static constexpr double fs = 48000.0;
static constexpr int block = 512;

static EngineParams flat()
{
    EngineParams p;
    p.analog = false;
    return p;
}

/** fait passer in (stéréo) dans un moteur neuf, bloc par bloc */
static void run (Engine& e, juce::AudioBuffer<float>& buf, const EngineParams& p)
{
    for (int start = 0; start < buf.getNumSamples(); start += block)
    {
        const int len = std::min (block, buf.getNumSamples() - start);
        juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, start, len);
        e.process (part, p);
    }
}

static std::vector<double> magnitudeDb (const float* x, int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    for (int i = 0; i < n; ++i) data[(size_t) i] = x[i];
    fft.performFrequencyOnlyForwardTransform (data.data());
    std::vector<double> out ((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) out[(size_t) i] = gainToDb (data[(size_t) i]);
    return out;
}

static void testNull()
{
    std::puts ("null");
    Engine e; e.prepare (fs, block);
    const int L = e.getLatency();
    juce::AudioBuffer<float> buf (2, 48000), ref (2, 48000);
    juce::Random r (1);
    // 40 sinus de 30 Hz à 18 kHz, phases au hasard : la bande audio (l'aller-retour ×2 coupe au-delà de ~21 kHz)
    ref.clear();
    for (int k = 0; k < 40; ++k)
    {
        const double f = 30.0 * std::pow (600.0, k / 39.0), ph = r.nextDouble() * 2 * pi;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) ref.addSample (c, i, 0.02f * (float) std::sin (2 * pi * f * i / fs + ph + c));
    }
    buf.makeCopyOf (ref);
    run (e, buf, flat());
    double err = 0, sig = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 2000; i < 48000; ++i)
        {
            const double d = buf.getSample (c, i) - ref.getSample (c, i - L);
            err += d * d; sig += (double) ref.getSample (c, i - L) * ref.getSample (c, i - L);
        }
    std::printf ("  latence : %d échantillons\n", L);
    check (10.0 * std::log10 (err / sig) < -60.0, "écart au neutre, 40 sinus 30 Hz - 18 kHz (dB, < -60)", 10.0 * std::log10 (err / sig));

    // sinus 1 kHz : le cœur de la bande, l'écart doit être infime
    Engine e2; e2.prepare (fs, block);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) ref.setSample (c, i, 0.5f * (float) std::sin (2.0 * pi * 1000.0 * i / fs + c));
    buf.makeCopyOf (ref);
    run (e2, buf, flat());
    err = sig = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 2000; i < 48000; ++i)
        {
            const double d = buf.getSample (c, i) - ref.getSample (c, i - L);
            err += d * d; sig += (double) ref.getSample (c, i - L) * ref.getSample (c, i - L);
        }
    check (10.0 * std::log10 (err / sig) < -80.0, "écart au neutre, sinus 1 kHz (dB, < -80)", 10.0 * std::log10 (err / sig));
}

static double curveError (const EngineParams& p, bool sideOnly, double fMax = 16000.0, bool print = false)
{
    Engine e; e.prepare (fs, block);
    constexpr int order = 15;
    const int n = 1 << order;
    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    // impulsion : L = R (Mid seul) ou L = -R (Side seul)
    buf.setSample (0, 64, 0.25f);
    buf.setSample (1, 64, sideOnly ? -0.25f : 0.25f);
    run (e, buf, p);
    const auto mag = magnitudeDb (buf.getReadPointer (0), order);
    const auto t = Targets::compute (p, 2.0 * fs);
    const auto d = Design::make (t.ch[sideOnly ? 1 : 0], 2.0 * fs);
    const Response r (2.0 * fs);
    double worst = 0;
    for (double f = 25.0; f <= fMax; f *= 1.06)
    {
        const int bin = (int) std::lround (f * n / fs);
        const double measured = mag[(size_t) bin] - gainToDb (0.25) + (sideOnly ? 0.0 : 0.0);
        double expected = gainToDb (std::abs (r.total (d, (double) bin * fs / n)));
        if (sideOnly) expected += gainToDb (p.width);
        worst = std::max (worst, std::abs (measured - expected));
        if (print) std::printf ("    %7.0f Hz  mesuré %+6.2f  courbe %+6.2f\n", f, measured, expected);
    }
    return worst;
}

static void testCurve()
{
    std::puts ("curve");
    struct Case { const char* name; EngineParams p; };
    std::vector<Case> cases;
    { auto p = flat(); p.base.lowB = 8; p.base.lowA = 5; p.base.lowF = 60; cases.push_back ({ "LOW boost + atten", p }); }
    { auto p = flat(); p.base.midG = 9; p.base.midF = 1500; p.base.midQ = 1.4; cases.push_back ({ "MID +9 (dépassement)", p }); }
    { auto p = flat(); p.base.midG = -10; p.base.midF = 400; cases.push_back ({ "MID -10", p }); }
    { auto p = flat(); p.base.conV = 6; p.base.conF = 800; cases.push_back ({ "CONTOUR +6", p }); }
    { auto p = flat(); p.base.conV = -7; p.base.conF = 2500; cases.push_back ({ "CONTOUR -7", p }); }
    { auto p = flat(); p.base.highB = 8; p.base.highA = 3; p.base.highF = 8000; cases.push_back ({ "HIGH boost + atten", p }); }
    { auto p = flat(); p.base.hpfDb = -24; p.base.hpfF = 80; p.base.lpfDb = -24; p.base.lpfF = 9000; cases.push_back ({ "HPF + LPF", p }); }
    { auto p = flat(); p.base.lowB = 10; p.base.lowF = 150; p.base.midG = 10; p.base.midF = 300; cases.push_back ({ "LOW + MID (interaction)", p }); }
    for (auto& c : cases) check (curveError (c.p, false) < 0.25, c.name, curveError (c.p, false));
    { auto p = flat(); p.link = false; p.target = 0; p.ofsHigh = 6; p.base.highF = 6000; p.width = 1.3;
      check (curveError (p, true) < 0.25, "Side : OFFSET HIGH +6, largeur 130 %", curveError (p, true)); }
}

static void testMs()
{
    std::puts ("ms");
    // offset vers le Side : le Mid doit rester neutre
    auto p = flat(); p.link = false; p.target = 0; p.ofsLow = 8; p.ofsHigh = -6; p.ofsMid = 5;
    const auto t = Targets::compute (p, 2.0 * fs);
    check (t.ch[0] == Settings {}, "offset SIDE : Mid intact (1 = oui)", t.ch[0] == Settings {} ? 1.0 : 0.0);
    check (std::abs (t.ch[1].lowB - 8) < 1e-9, "offset SIDE : LOW du Side +8", t.ch[1].lowB);
    // largeur 0 : L = R
    Engine e; e.prepare (fs, block);
    juce::AudioBuffer<float> buf (2, 24000);
    juce::Random r (3);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 24000; ++i) buf.setSample (c, i, (float) (r.nextDouble() - 0.5));
    auto q = flat(); q.width = 0;
    run (e, buf, q);
    double diff = 0;
    for (int i = 4000; i < 24000; ++i) diff = std::max (diff, (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
    check (diff < 1e-5, "largeur 0 % : L - R (max)", diff);
    // solo SIDE sur un signal mono : silence
    Engine e2; e2.prepare (fs, block);
    for (int i = 0; i < 24000; ++i) { const float v = (float) (r.nextDouble() - 0.5); buf.setSample (0, i, v); buf.setSample (1, i, v); }
    auto s = flat(); s.solo = 2;
    run (e2, buf, s);
    double peak = 0;
    for (int i = 4000; i < 24000; ++i) peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
    check (peak < 1e-5, "solo SIDE d'un signal mono (crête)", peak);
}

static void testDelta()
{
    std::puts ("delta");
    juce::AudioBuffer<float> a (2, 48000), b (2, 48000);
    juce::Random r (5);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) a.setSample (c, i, 0.3f * (float) std::sin (2 * pi * 220 * i / fs + c) + 0.1f * (float) (r.nextDouble() - 0.5));
    b.makeCopyOf (a);
    auto ref = a;
    auto p = flat(); p.analog = true; p.character = 0.8; p.base.lowB = 6; p.base.midG = -4; p.base.conV = 4; p.base.highB = 5;
    Engine e1, e2, e3; e1.prepare (fs, block); e2.prepare (fs, block); e3.prepare (fs, block);
    run (e1, a, p);
    auto pd = p; pd.delta = true;
    run (e2, b, pd);
    run (e3, ref, flat());   // l'original dans le même aller-retour ×2, rien d'autre
    // traité - delta doit rendre exactement l'original (même chemin de suréchantillonnage) : DELTA est bien calé
    double err = 0, sig = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = 4000; i < 48000; ++i)
        {
            const double orig = a.getSample (c, i) - b.getSample (c, i);
            const double d = orig - ref.getSample (c, i);
            err += d * d; sig += (double) ref.getSample (c, i) * ref.getSample (c, i);
        }
    check (10 * std::log10 (err / sig) < -90, "traité - delta = original (écart, dB, < -90)", 10 * std::log10 (err / sig));
}

static void testInteract()
{
    std::puts ("interact");
    Settings s; s.lowB = 10; s.lowF = 150; s.midG = 10; s.midF = 300; s.midQ = 0.7;
    const double k = boostScale (s, 2 * fs);
    const Response r (2 * fs);
    const auto d = Design::make (scaled (s, k), 2 * fs);
    double peak = 0;
    for (int i = 0; i < Grid::n; ++i) peak = std::max (peak, gainToDb (std::abs (1.0 + r.deviation (d, Grid::freq (i)))));
    std::printf ("  facteur des renforts : %.3f\n", k);
    check (peak <= 13.2, "pic LOW +10 @150 & MID +10 @300 (dB, ≤ 13)", peak);
    // recouvrement franc : la cloche dans le plateau
    Settings o; o.lowB = 10; o.lowF = 250; o.midG = 9; o.midF = 70; o.midQ = 0.6;
    const double ko = boostScale (o, 2 * fs);
    const auto dO = Design::make (scaled (o, ko), 2 * fs);
    double rawPeak = 0, peakO = 0;
    const auto dRaw = Design::make (o, 2 * fs);
    for (int i = 0; i < Grid::n; ++i)
    {
        rawPeak = std::max (rawPeak, gainToDb (std::abs (1.0 + r.deviation (dRaw, Grid::freq (i)))));
        peakO = std::max (peakO, gainToDb (std::abs (1.0 + r.deviation (dO, Grid::freq (i)))));
    }
    std::printf ("  MID +9 @70 dans LOW +10 @250 : %.2f dB sans interaction, %.2f dB avec (facteur %.3f)\n", rawPeak, peakO, ko);
    check (peakO <= 13.05 && rawPeak > 13.1, "recouvrement : ramené à 3 dB au-dessus du plus fort seul", peakO);
    Settings apart; apart.lowB = 6; apart.lowF = 50; apart.highB = 6; apart.highF = 12000;
    check (boostScale (apart, 2 * fs) == 1.0, "LOW et HIGH éloignés : rien réduit", boostScale (apart, 2 * fs));
}

static void testAutoGain()
{
    std::puts ("autogain");
    // bruit rose (Voss) à travers plusieurs réglages : niveau RMS entrée / sortie avec gain auto
    auto pink = [] (juce::AudioBuffer<float>& b, int seed)
    {
        juce::Random r (seed);
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double w = r.nextDouble() * 2 - 1;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double v = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.05;
            b6 = w * 0.115926;
            b.setSample (0, i, (float) v); b.setSample (1, i, (float) v);
        }
    };
    struct Case { const char* name; EngineParams p; };
    std::vector<Case> cases;
    { auto p = flat(); p.base.lowB = 8; p.base.lowF = 80; p.base.highB = 6; cases.push_back ({ "LOW +8, HIGH +6", p }); }
    { auto p = flat(); p.base.midG = -9; p.base.midF = 800; p.base.midQ = 0.5; cases.push_back ({ "MID -9 large", p }); }
    { auto p = flat(); p.base.conV = 8; cases.push_back ({ "CONTOUR +8", p }); }
    { auto p = flat(); p.inDb = 6; p.base.highB = 4; cases.push_back ({ "INPUT +6, HIGH +4", p }); }
    for (auto& c : cases)
    {
        juce::AudioBuffer<float> b (2, 96000);
        pink (b, 9);
        auto in = b;
        c.p.autoGain = true;
        Engine e; e.prepare (fs, block);
        run (e, b, c.p);
        // sonie : pondération K (BS.1770) par de vrais filtres, indépendants du moteur
        auto loud = [] (const juce::AudioBuffer<float>& x)
        {
            juce::dsp::IIR::Filter<float> shelf, hp;
            shelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, 1681.97f, 0.7072f, juce::Decibels::decibelsToGain (4.0f));
            hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (fs, 38.135f, 0.5003f);
            double s = 0;
            for (int i = 0; i < x.getNumSamples(); ++i)
            {
                const float v = hp.processSample (shelf.processSample (x.getSample (0, i)));
                if (i >= 8000) s += (double) v * v;
            }
            return s;
        };
        const double diff = 10 * std::log10 (loud (b) / loud (in));
        check (std::abs (diff) < 1.5, c.name, diff);
    }
}

static double thd (const EngineParams& p, double freq, double amp)
{
    Engine e; e.prepare (fs, block);
    constexpr int order = 15;
    const int n = 1 << order;
    juce::AudioBuffer<float> b (2, n + 8192);
    for (int i = 0; i < b.getNumSamples(); ++i) { const float v = (float) (amp * std::sin (2 * pi * freq * i / fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
    run (e, b, p);
    // fenêtre de Hann sur la partie stable
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = b.getSample (0, i + 8192) * (float) (0.5 - 0.5 * std::cos (2 * pi * i / (n - 1)));
    const auto mag = magnitudeDb (x.data(), order);
    auto at = [&] (double f) { const int bin = (int) std::lround (f * n / fs); double m = -300; for (int k = bin - 3; k <= bin + 3; ++k) m = std::max (m, mag[(size_t) k]); return m; };
    const double f0 = at (freq);
    double h = 0;
    for (int k = 2; k <= 9; ++k) if (freq * k < fs / 2) h += std::pow (10.0, (at (freq * k) - f0) / 10.0);
    return 10.0 * std::log10 (std::max (1e-30, h));
}

static void testHarm()
{
    std::puts ("harm (dB des harmoniques 2 … 9 sous le fondamental, sinus 100 Hz à -6 dBFS)");
    auto p = flat(); p.analog = true; p.character = 0.6;
    const double neutral = thd (p, 100, 0.5);
    p.base.lowB = 3; p.base.lowF = 100; const double light = thd (p, 100, 0.5);
    p.base.lowB = 10; const double heavy = thd (p, 100, 0.5);
    p.character = 1.0; const double heavyMax = thd (p, 100, 0.5);
    p.analog = false; const double off = thd (p, 100, 0.5);
    std::printf ("  neutre %.1f  ·  LOW +3 %.1f  ·  LOW +10 %.1f  ·  LOW +10 caractère 100 %% %.1f  ·  ANALOG éteint %.1f\n", neutral, light, heavy, heavyMax, off);
    check (neutral < -100, "bande au neutre : aucune couleur", neutral);
    check (light < heavy && heavy < heavyMax, "la couleur monte avec la sollicitation et le caractère (heavy)", heavy);
    check (off < -100, "ANALOG éteint : linéaire", off);
}

static void testRobust()
{
    std::puts ("robust");
    Engine e; e.prepare (fs, block);
    juce::Random r (7);
    juce::AudioBuffer<float> b (2, block);
    double peak = 0;
    bool finite = true;
    for (int blk = 0; blk < 2000; ++blk)
    {
        EngineParams p;
        auto& s = p.base;
        s.lowF = 20 * std::pow (20.0, r.nextDouble()); s.lowB = r.nextDouble() * 12; s.lowA = r.nextDouble() * 12;
        s.midF = 150 * std::pow (53.0, r.nextDouble()); s.midG = r.nextDouble() * 24 - 12; s.midQ = 0.4 + r.nextDouble() * 2.1;
        s.conF = 200 * std::pow (30.0, r.nextDouble()); s.conV = r.nextDouble() * 20 - 10;
        s.highF = 1500 * std::pow (16.0, r.nextDouble()); s.highB = r.nextDouble() * 12; s.highA = r.nextDouble() * 12;
        s.hpfDb = r.nextBool() ? -24 : 0; s.hpfF = 10 * std::pow (40.0, r.nextDouble());
        s.lpfDb = r.nextBool() ? -24 : 0; s.lpfF = 3000 * std::pow (10.0, r.nextDouble());
        p.link = r.nextBool(); p.target = r.nextInt (3);
        p.ofsLow = r.nextDouble() * 24 - 12; p.ofsMid = r.nextDouble() * 24 - 12; p.ofsCon = r.nextDouble() * 20 - 10; p.ofsHigh = r.nextDouble() * 24 - 12;
        p.analog = r.nextBool(); p.character = r.nextDouble(); p.solo = r.nextInt (3); p.delta = r.nextBool(); p.mono = r.nextBool();
        p.width = r.nextDouble() * 2; p.inDb = r.nextDouble() * 36 - 18; p.outDb = r.nextDouble() * 36 - 18; p.autoGain = r.nextBool();
        p.bypass = r.nextInt (10) == 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
                b.setSample (c, i, blk % 50 == 0 ? (i % 2 ? 1.0f : -1.0f) : (float) (r.nextDouble() * 2 - 1));
        e.process (b, p);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, (double) std::abs (v));
            }
    }
    check (finite, "toutes les sorties finies (1 = oui)", finite ? 1.0 : 0.0);
    // entrée pleine échelle, +18 dB d'entrée, +18 de sortie, renforts au maximum : la crête reste bornée
    check (peak < 1000.0, "crête la plus forte (pleine échelle, +36 dB de gains possibles)", peak);
}

static void testClicks()
{
    std::puts ("clicks");
    // sinus 300 Hz, réglages qui sautent d'un bloc à l'autre : la plus forte variation d'un échantillon au suivant,
    // comparée à celle du sinus lui-même traité sans saut
    auto run1 = [] (bool jumps)
    {
        Engine e; e.prepare (fs, block);
        juce::AudioBuffer<float> b (2, block);
        double worst = 0, prev = 0;
        int t = 0;
        for (int blk = 0; blk < 300; ++blk)
        {
            EngineParams p; p.analog = true;
            if (jumps && blk >= 20)
            {
                const bool on = (blk / 10) % 2;
                p.base.lowB = on ? 12 : 0; p.base.midG = on ? -12 : 12; p.base.midF = on ? 300 : 3000;
                p.base.hpfDb = on ? -24 : 0; p.base.hpfF = 200; p.solo = (blk / 10) % 3; p.mono = on; p.delta = (blk / 20) % 2;
                p.width = on ? 2 : 0; p.bypass = (blk / 30) % 2; p.link = on; p.ofsHigh = 12;
            }
            for (int i = 0; i < block; ++i, ++t) { const float v = 0.3f * (float) std::sin (2 * pi * 300 * t / fs); b.setSample (0, i, v); b.setSample (1, i, 0.7f * v); }
            e.process (b, p);
            for (int i = 0; i < block; ++i)
            {
                const double v = b.getSample (0, i);
                if (blk > 4) worst = std::max (worst, std::abs (v - prev));
                prev = v;
            }
        }
        return worst;
    };
    const double still = run1 (false), moving = run1 (true);
    std::printf ("  pente max : sans saut %.4f, avec sauts %.4f\n", still, moving);
    check (moving < still * 6.0, "sauts de réglages : pente max / pente du sinus (< 6, gains jusqu'à +12 dB)", moving / still);
}

static void testCpu()
{
    std::puts ("cpu");
    Engine e; e.prepare (fs, block);
    juce::AudioBuffer<float> b (2, block);
    juce::Random r (11);
    auto p = flat(); p.analog = true; p.character = 0.7; p.base.lowB = 6; p.base.midG = 3; p.base.conV = 3; p.base.highB = 4;
    p.base.hpfDb = -24; p.link = false; p.ofsHigh = 3;
    const int blocks = (int) (20.0 * fs / block);
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int k = 0; k < blocks; ++k)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) b.setSample (c, i, (float) (r.nextDouble() - 0.5));
        e.process (b, p);
    }
    const double secs = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    std::printf ("  20 s d'audio stéréo en %.3f s : %.2f %% d'un cœur\n", secs, 100.0 * secs / 20.0);
}

int main (int argc, char** argv)
{
    std::vector<std::string> want;
    for (int i = 1; i < argc; ++i) want.push_back (argv[i]);
    auto on = [&] (const char* n) { return want.empty() || std::find (want.begin(), want.end(), n) != want.end(); };
    if (on ("null")) testNull();
    if (on ("curve")) testCurve();
    if (on ("ms")) testMs();
    if (on ("delta")) testDelta();
    if (on ("interact")) testInteract();
    if (on ("autogain")) testAutoGain();
    if (on ("harm")) testHarm();
    if (on ("robust")) testRobust();
    if (on ("clicks")) testClicks();
    if (on ("cpu")) testCpu();
    if (std::find (want.begin(), want.end(), "print") != want.end())
    {
        auto p = flat(); p.base.lowB = 8; p.base.lowA = 5; p.base.lowF = 60;
        curveError (p, false, 16000, true);
    }
    std::printf ("%s (%d échec%s)\n", failures == 0 ? "TOUT EST BON" : "DES ESSAIS ÉCHOUENT", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
