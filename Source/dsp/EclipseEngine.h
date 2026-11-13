#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>
#include <vector>
#include "EclipseNetwork.h"

//==============================================================================
//  ECLIPSE : la chaîne complète.
//
//    entrée (gain d'entrée) → encodage Mid / Side → suréchantillonnage ×2 (FIR à phase linéaire)
//      → réseau parallèle du Mid, réseau parallèle du Side (mêmes réglages + OFFSET), largeur sur le Side
//      → (en parallèle : l'écart traité - original, calculé dans le même domaine, pour DELTA)
//    → sous-échantillonnage → écoute (DELTA, SOLO M / S, MONO) → décodage L / R → sortie (gain, gain auto) → bypass.
//
//  Thread audio : aucune allocation (tout est réservé dans prepare), aucun verrou. Les réglages se lissent à la cadence
//  de contrôle (tous les 16 échantillons suréchantillonnés), les gains et l'écoute échantillon par échantillon.
//==============================================================================
namespace eclipse
{
struct EngineParams
{
    Settings base;
    double ofsLow = 0, ofsMid = 0, ofsCon = 0, ofsHigh = 0;
    int target = 0;              // 0 SIDE, 1 MID, 2 SPLIT : où va l'OFFSET
    bool link = true;            // M/S LINK : les deux canaux identiques
    bool analog = true;
    double xformer = 0.0;        // XFORMER : transformateurs d'entrée et de sortie, 0 … 1
    double character = 0.4;      // 0 … 1
    int solo = 0;                // 0 STEREO, 1 MID, 2 SIDE
    bool delta = false, mono = false;
    double width = 1.0;          // 0 … 2
    double inDb = 0, outDb = 0;
    bool autoGain = false;
    bool bypass = false;
};

/** Les réglages d'un canal : la base + l'OFFSET selon la cible (côté = 1 pour le Side). */
inline Settings channelSettings (const EngineParams& p, int side)
{
    Settings s = p.base;
    if (p.link) return s;
    static constexpr double share[3][2] { { 0.0, 1.0 }, { 1.0, 0.0 }, { -0.5, 0.5 } };
    const double k = share[std::clamp (p.target, 0, 2)][side];
    auto shelf = [] (double& boost, double& atten, double v)
    {
        // un renfort d'abord rendu par l'atténuation (et l'inverse) : -3 sur un renfort de 5 → renfort de 2
        double net = boost - atten + v;
        if (boost > 0 && atten > 0) { boost = std::clamp (boost + std::max (0.0, v), 0.0, 18.0); atten = std::clamp (atten + std::max (0.0, -v), 0.0, 18.0); return; }
        boost = std::clamp (std::max (0.0, net), 0.0, 18.0);
        atten = std::clamp (std::max (0.0, -net), 0.0, 18.0);
    };
    shelf (s.lowB, s.lowA, k * p.ofsLow);
    s.midG = std::clamp (s.midG + k * p.ofsMid, -18.0, 18.0);
    s.conV = std::clamp (s.conV + k * p.ofsCon, -15.0, 15.0);
    shelf (s.highB, s.highA, k * p.ofsHigh);
    return s;
}

/** Ce que l'interface lit (écrit par le thread audio). */
struct Meters
{
    std::atomic<float> inPeak[2] {}, inRms[2] {}, outPeak[2] {}, outRms[2] {}, midRms {}, sideRms {}, corr { 1.0f },
                       autoGainDb {}, interaction { 1.0f };
};

/** Gain auto et interaction des bandes pour des réglages donnés (thread audio ou interface). */
struct Targets
{
    Settings ch[2];
    double scale[2] { 1, 1 };
    double autoGainDb = 0;

    static Targets compute (const EngineParams& p, double fs)
    {
        Targets t;
        for (int c = 0; c < 2; ++c)
        {
            const auto raw = channelSettings (p, c);
            t.scale[c] = boostScale (raw, fs);
            t.ch[c] = scaled (raw, t.scale[c]);
        }
        const double pm = perceivedPower (Design::make (t.ch[0], fs), fs);
        const double ps = perceivedPower (Design::make (t.ch[1], fs), fs);
        const double w2 = p.width * p.width;
        const double power = 0.8 * pm + 0.2 * w2 * ps;   // à plat, largeur 100 % : 1
        t.autoGainDb = std::clamp (-10.0 * std::log10 (std::max (1.0e-6, power)) - p.inDb, -24.0, 24.0);
        return t;
    }
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        fsO = 2.0 * sampleRate;
        maxN = std::max (1, maxBlock);
        os = std::make_unique<juce::dsp::Oversampling<float>> (4, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxN);
        latency = (int) std::lround (os->getLatencyInSamples());
        work.setSize (4, maxN);
        raw.setSize (2, maxN);
        dry.assign (2, std::vector<float> ((size_t) std::max (1, latency) + 1, 0.0f));
        dryPos = 0;
        for (auto& n : net) { n.prepare (fsO); n.reset(); }
        for (auto* t : { &xin[0], &xin[1], &xout[0], &xout[1] }) t->prepare (fsO);
        xfA = 1.0 - std::exp (-1.0 / (0.03 * fsO));
        haveTargets = false;
        firstBlock = true;
        rmsA = 1.0 - std::exp (-1.0 / (0.3 * fs));
        smooth20 = 1.0 - std::exp (-1.0 / (0.02 * fs));
        ctrlA = 1.0 - std::exp (-(double) ctrlStep / (0.045 * fsO));
        driftA = 1.0 - std::exp (-(double) ctrlStep / (2.5 * fsO));
        for (auto& c : drift) for (auto& d : c) d = { 0.0, 0.0 };
        corrLR = corrLL = corrRR = 0;
        for (int c = 0; c < 2; ++c) inMs[c] = outMs[c] = 0;
        midMs = sideMs = 0;
    }

    int getLatency() const { return latency; }
    Meters meters;

    void process (juce::AudioBuffer<float>& buffer, const EngineParams& p)
    {
        const int n = buffer.getNumSamples();
        const int nch = std::min (2, buffer.getNumChannels());
        if (n <= 0 || nch <= 0 || os == nullptr) return;
        if (n > maxN)
        {
            // un hôte qui dépasse la taille annoncée : traité par morceaux
            for (int start = 0; start < n; start += maxN)
            {
                const int len = std::min (maxN, n - start);
                juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
                process (part, p);
            }
            return;
        }

        updateTargets (p);
        if (firstBlock) jumpToTargets (p);

        float* L = buffer.getWritePointer (0);
        float* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

        // mesure d'entrée et copie pour le bypass (retardée de la latence)
        meterIn (L, R, n);
        const float inG = (float) dbToGain (p.inDb);
        for (int i = 0; i < n; ++i)
        {
            inGain += smooth20 * (inG - inGain);
            const float l = L[i], r = R != nullptr ? R[i] : l;
            raw.setSample (0, i, l);
            raw.setSample (1, i, r);
            const float gl = l * (float) inGain, gr = r * (float) inGain;
            work.setSample (0, i, 0.5f * (gl + gr));
            work.setSample (1, i, 0.5f * (gl - gr));
        }
        work.copyFrom (2, 0, work, 0, 0, n);
        work.copyFrom (3, 0, work, 1, 0, n);

        juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 4, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int nO = (int) up.getNumSamples();
        float* m = up.getChannelPointer (0);
        float* s = up.getChannelPointer (1);
        float* dm = up.getChannelPointer (2);
        float* ds = up.getChannelPointer (3);
        const double analogT = p.analog ? 1.0 : 0.0;
        const double xfT = std::clamp (p.xformer, 0.0, 1.0);
        for (int i = 0; i < nO; ++i)
        {
            if (ctrlCount-- <= 0) { ctrlCount = ctrlStep - 1; controlStep (analogT, p.character); }
            width += widthA * (p.width - width);
            xf += xfA * (xfT - xf);
            double mi = m[i], si = s[i];
            if (xf > 1.0e-6)
            {
                // transformateur d'entrée, sur L et R (là où il est dans un vrai circuit)
                const double l = xin[0].tick (mi + si, xf), r = xin[1].tick (mi - si, xf);
                mi = 0.5 * (l + r); si = 0.5 * (l - r);
            }
            else { xin[0].tick (mi + si, 0.0); xin[1].tick (mi - si, 0.0); }
            double ym = net[0].tick (mi);
            double ys = net[1].tick (si) * width;
            {
                // transformateur de sortie, un peu moins poussé
                const double k = 0.6 * xf;
                const double l = xout[0].tick (ym + ys, k), r = xout[1].tick (ym - ys, k);
                if (k > 1.0e-6) { ym = 0.5 * (l + r); ys = 0.5 * (l - r); }
            }
            dm[i] = (float) (ym - dm[i]);   // DELTA : traité - original, dans le même domaine
            ds[i] = (float) (ys - ds[i]);
            m[i] = (float) ym;
            s[i] = (float) ys;
        }
        os->processSamplesDown (block);

        // écoute, décodage, sortie
        const double soloT[3][3] { { 1, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };   // M ← aM·M + aS·S, S ← bS·S
        const auto& st = soloT[std::clamp (p.solo, 0, 2)];
        const double deltaT = p.delta ? 1.0 : 0.0, monoT = p.mono ? 1.0 : 0.0, bypT = p.bypass ? 1.0 : 0.0;
        const double outT = dbToGain (p.outDb + (p.autoGain ? targets.autoGainDb : 0.0));
        const float* M = work.getReadPointer (0);
        const float* S = work.getReadPointer (1);
        const float* DM = work.getReadPointer (2);
        const float* DS = work.getReadPointer (3);
        for (int i = 0; i < n; ++i)
        {
            aM += smooth20 * (st[0] - aM); aS += smooth20 * (st[1] - aS); bS += smooth20 * (st[2] - bS);
            deltaAmt += smooth20 * (deltaT - deltaAmt);
            monoAmt += smooth20 * (monoT - monoAmt);
            bypAmt += smooth20 * (bypT - bypAmt);
            outGain += smooth20 * (outT - outGain);

            const double mm = M[i] + deltaAmt * (DM[i] - M[i]);
            const double ss = S[i] + deltaAmt * (DS[i] - S[i]);
            const double mo = aM * mm + aS * ss;
            const double so = bS * ss * (1.0 - monoAmt);
            double l = (mo + so) * outGain, r = (mo - so) * outGain;

            float dl, dr;
            popDry (i, dl, dr);
            l += bypAmt * (dl - l);
            r += bypAmt * (dr - r);
            L[i] = (float) l;
            if (R != nullptr) R[i] = (float) r;
            else L[i] = (float) (0.5 * (l + r));
        }
        advanceDry (n);
        meterOut (L, R, n);
        meters.autoGainDb.store ((float) (p.autoGain ? targets.autoGainDb : 0.0));
        meters.interaction.store ((float) std::min (targets.scale[0], targets.scale[1]));
    }

private:
    static constexpr int ctrlStep = 16;

    void updateTargets (const EngineParams& p)
    {
        const auto m = channelSettings (p, 0), s = channelSettings (p, 1);
        if (haveTargets && m == lastRaw[0] && s == lastRaw[1] && p.width == lastWidth && p.inDb == lastIn) return;
        lastRaw[0] = m; lastRaw[1] = s; lastWidth = p.width; lastIn = p.inDb;
        targets = Targets::compute (p, fsO);
        haveTargets = true;
    }

    void jumpToTargets (const EngineParams& p)
    {
        firstBlock = false;
        for (int c = 0; c < 2; ++c) cur[c] = targets.ch[c];
        analog = p.analog ? 1.0 : 0.0;
        width = p.width;
        xf = std::clamp (p.xformer, 0.0, 1.0);
        inGain = dbToGain (p.inDb);
        outGain = dbToGain (p.outDb + (p.autoGain ? targets.autoGainDb : 0.0));
        const double soloT[3][3] { { 1, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
        const auto& st = soloT[std::clamp (p.solo, 0, 2)];
        aM = st[0]; aS = st[1]; bS = st[2];
        deltaAmt = p.delta ? 1.0 : 0.0;
        monoAmt = p.mono ? 1.0 : 0.0;
        bypAmt = p.bypass ? 1.0 : 0.0;
        for (int c = 0; c < 2; ++c) net[c].design (cur[c], fsO);
        ctrlCount = 0;
    }

    /** lissage des réglages, dérives analogiques, coefficients : tous les 16 échantillons suréchantillonnés */
    void controlStep (double analogT, double character)
    {
        analog += ctrlA * (analogT - analog);
        const double E = std::clamp (character, 0.0, 1.0) * analog;
        widthA = 1.0 - std::exp (-1.0 / (0.02 * fsO));
        for (int c = 0; c < 2; ++c)
        {
            auto& a = cur[c];
            const auto& t = targets.ch[c];
            bool moving = false;
            auto lin = [&] (double& v, double target, double eps)
            {
                const double dv = target - v;
                if (std::abs (dv) < eps) { if (v != target) { v = target; moving = true; } return; }
                v += ctrlA * dv; moving = true;
            };
            auto logf = [&] (double& v, double target)
            {
                const double r = std::log (target / v);
                if (std::abs (r) < 1.0e-4) { if (v != target) { v = target; moving = true; } return; }
                v *= std::exp (ctrlA * r); moving = true;
            };
            logf (a.lowF, t.lowF); lin (a.lowB, t.lowB, 1e-4); lin (a.lowA, t.lowA, 1e-4);
            logf (a.midF, t.midF); lin (a.midG, t.midG, 1e-4); logf (a.midQ, t.midQ);
            logf (a.conF, t.conF); lin (a.conV, t.conV, 1e-4);
            logf (a.highF, t.highF); lin (a.highB, t.highB, 1e-4); lin (a.highA, t.highA, 1e-4);
            lin (a.hpfDb, t.hpfDb, 1e-3); logf (a.hpfF, t.hpfF);
            lin (a.lpfDb, t.lpfDb, 1e-3); logf (a.lpfF, t.lpfF);
            logf (a.lowQ, t.lowQ); logf (a.highQ, t.highQ); logf (a.conW, t.conW);

            if (E > 1.0e-4)
            {
                // instabilité contrôlée : marche aléatoire lente sur le gain et la fréquence de chaque section
                Settings w = a;
                for (auto& dd : drift[c])
                {
                    dd.first += driftA * ((rng.nextDouble() * 2.0 - 1.0) * 3.0 - dd.first);
                    dd.second += driftA * ((rng.nextDouble() * 2.0 - 1.0) * 3.0 - dd.second);
                }
                const double gd = 0.06 * E, fd = 0.004 * E;
                w.lowB += gd * drift[c][0].first * (w.lowB > 0);   w.lowF *= 1.0 + fd * drift[c][0].second;
                w.midG += gd * drift[c][1].first * (w.midG != 0);  w.midF *= 1.0 + fd * drift[c][1].second;
                w.conV += gd * drift[c][2].first * (w.conV != 0);  w.conF *= 1.0 + fd * drift[c][2].second;
                w.highB += gd * drift[c][3].first * (w.highB > 0); w.highF *= 1.0 + fd * drift[c][3].second;
                net[c].design (w, fsO);
            }
            else if (moving || lastE > 1.0e-4)
                net[c].design (a, fsO);
            net[c].colour (E);
        }
        lastE = E;
    }

    void popDry (int i, float& l, float& r)
    {
        const int size = (int) dry[0].size();
        // le tampon circulaire contient la latence ; l'échantillon i entre à dryPos + i, sort latency plus tard
        const int w = (dryPos + i) % size;
        const int rd = (dryPos + i - latency + size * 4) % size;
        dry[0][(size_t) w] = raw.getSample (0, i); dry[1][(size_t) w] = raw.getSample (1, i);
        l = dry[0][(size_t) rd]; r = dry[1][(size_t) rd];
    }
    void advanceDry (int n) { dryPos = (dryPos + n) % (int) dry[0].size(); }

    void meterIn (const float* L, const float* R, int n)
    {
        float pk[2] {};
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R != nullptr ? R[i] : l;
            pk[0] = std::max (pk[0], std::abs (l)); pk[1] = std::max (pk[1], std::abs (r));
            inMs[0] += rmsA * ((double) l * l - inMs[0]); inMs[1] += rmsA * ((double) r * r - inMs[1]);
        }
        for (int c = 0; c < 2; ++c)
        {
            meters.inPeak[c].store (std::max (meters.inPeak[c].load(), pk[c]));
            meters.inRms[c].store ((float) std::sqrt (inMs[c]));
        }
    }
    void meterOut (const float* L, const float* R, int n)
    {
        float pk[2] {};
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = R != nullptr ? R[i] : l;
            pk[0] = std::max (pk[0], (float) std::abs (l)); pk[1] = std::max (pk[1], (float) std::abs (r));
            outMs[0] += rmsA * (l * l - outMs[0]); outMs[1] += rmsA * (r * r - outMs[1]);
            const double mm = 0.5 * (l + r), ss = 0.5 * (l - r);
            midMs += rmsA * (mm * mm - midMs); sideMs += rmsA * (ss * ss - sideMs);
            corrLR += rmsA * (l * r - corrLR); corrLL += rmsA * (l * l - corrLL); corrRR += rmsA * (r * r - corrRR);
        }
        for (int c = 0; c < 2; ++c)
        {
            meters.outPeak[c].store (std::max (meters.outPeak[c].load(), pk[c]));
            meters.outRms[c].store ((float) std::sqrt (outMs[c]));
        }
        meters.midRms.store ((float) std::sqrt (midMs));
        meters.sideRms.store ((float) std::sqrt (sideMs));
        const double den = std::sqrt (corrLL * corrRR);
        meters.corr.store (den > 1.0e-9 ? (float) std::clamp (corrLR / den, -1.0, 1.0) : 1.0f);
    }

    double fs = 44100, fsO = 88200;
    int maxN = 512, latency = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> work, raw;
    std::vector<std::vector<float>> dry;
    int dryPos = 0;

    Network net[2];
    Transformer xin[2], xout[2];
    double xf = 0, xfA = 0.001;
    Settings cur[2], lastRaw[2];
    Targets targets;
    double lastWidth = -1, lastIn = 1.0e9;
    bool haveTargets = false, firstBlock = true;
    int ctrlCount = 0;
    double ctrlA = 0.1, driftA = 0.001, widthA = 0.001, smooth20 = 0.001, rmsA = 0.001;
    double analog = 1, lastE = 0, width = 1, inGain = 1, outGain = 1, aM = 1, aS = 0, bS = 1, deltaAmt = 0, monoAmt = 0, bypAmt = 0;
    std::pair<double, double> drift[2][4];
    juce::Random rng { 0x0ec11 };
    double inMs[2] {}, outMs[2] {}, midMs = 0, sideMs = 0, corrLR = 0, corrLL = 0, corrRR = 0;
};
} // namespace eclipse
