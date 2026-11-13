#pragma once

#include <cmath>
#include <complex>
#include <algorithm>

//==============================================================================
//  ECLIPSE : le réseau d'égalisation parallèle d'un canal (Mid ou Side).
//
//  Toutes les sections reçoivent le MÊME signal (après les filtres HPF / LPF en plateau) et ne rendent qu'un écart :
//  la sortie est  y = x + Σ écarts.  Une section au neutre rend un écart nul, donc le réseau entier au neutre rend
//  exactement x (pas de rotation de phase, pas de filtre traversé).
//
//      LOW      plateau grave : renfort (passe-bas 2e ordre amorti + 1er ordre décalé) et atténuation (1er ordre),
//               deux courbes de forme différente sur la même fréquence : poussées ensemble, elles se creusent l'une
//               l'autre au-dessus du plateau (le « creux » des égaliseurs passifs).
//      MID      cloche (passe-bande normalisé) + dépassement : deux petites cloches de signe opposé, une octave et
//               quart de part et d'autre, proportionnelles au réglage en dB.
//      CONTOUR  ondulation large : une zone centrale et deux zones de part et d'autre (à /3 et ×3), de signes
//               opposés ; positif = creuse le centre et relève les flancs, négatif = l'inverse.
//      HIGH     plateau aigu, symétrique du LOW.
//      HPF/LPF  plateaux (shelving) de -24 dB, 12 dB/oct, sans résonance : pas de vrai coupe-bas qui tourne la phase.
//
//  Les filtres sont des SVF / 1er ordre en TPT (bilinéaire avec préwarping) : leur réponse numérique est exactement
//  celle du prototype analogique évalué en s = j·tan(πF/fs) / g. La courbe affichée utilise la même fonction
//  (response) que le traitement : elle est calculée sur les vrais coefficients.
//==============================================================================
namespace eclipse
{
using cplx = std::complex<double>;
constexpr double pi = 3.14159265358979323846;

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) { return 20.0 * std::log10 (std::max (1.0e-9, g)); }

//==============================================================================
/** SVF de Simper / Zavalishin (TPT). lp, bp (crête Q), hp ; bpn = k·bp (crête 1). */
struct Svf
{
    double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    double lp = 0, bp = 0, hp = 0;

    void set (double g, double q)
    {
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = lp = bp = hp = 0; }
    inline void tick (double x)
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    double bpn() const { return k * bp; }
};

/** 1er ordre TPT : lp et hp. */
struct OnePole
{
    double G = 0, s = 0, lp = 0, hp = 0;
    void set (double g) { G = g / (1.0 + g); }
    void reset() { s = lp = hp = 0; }
    inline void tick (double x)
    {
        const double v = (x - s) * G;
        lp = v + s;
        s = lp + v;
        hp = x - lp;
    }
};

/** Plateau (shelf) en SVF : y = m0·x + m1·bp + m2·lp. */
struct Shelf
{
    Svf f;
    double m0 = 1, m1 = 0, m2 = 0;
    void reset() { f.reset(); }
    inline double tick (double x) { f.tick (x); return m0 * x + m1 * f.bp + m2 * f.lp; }
};

//==============================================================================
/** Les réglages d'un canal (Mid ou Side), en unités « humaines ». */
struct Settings
{
    double lowF = 60, lowB = 0, lowA = 0;
    double midF = 1200, midG = 0, midQ = 0.8;
    double conF = 1000, conV = 0;
    double highF = 10000, highB = 0, highA = 0;
    double hpfDb = 0, hpfF = 30;    // hpfDb : 0 (éteint) … -24 (allumé), lissé
    double lpfDb = 0, lpfF = 20000;
    double lowQ = 0.55, highQ = 0.55, conW = 3.0;   // BANDWIDTH de LOW et HIGH (Q du 2e ordre), écart du CONTOUR (×)

    bool operator== (const Settings& o) const
    {
        return lowF == o.lowF && lowB == o.lowB && lowA == o.lowA && midF == o.midF && midG == o.midG && midQ == o.midQ
            && conF == o.conF && conV == o.conV && highF == o.highF && highB == o.highB && highA == o.highA
            && hpfDb == o.hpfDb && hpfF == o.hpfF && lpfDb == o.lpfDb && lpfF == o.lpfF
            && lowQ == o.lowQ && highQ == o.highQ && conW == o.conW;
    }
    bool operator!= (const Settings& o) const { return ! (*this == o); }
};

/** Les grandeurs dérivées (gains linéaires des écarts, fréquences bornées) : ce qui fixe les coefficients. */
struct Design
{
    // fréquences (Hz)
    double fLow2, fLow1b, fLow1a, fLowV, fMid, fMidLo, fMidHi, fCon, fConLo, fConHi, fHigh2, fHigh1b, fHigh1a, fHighV, fHpf, fLpf;
    double qMid, qSkirt, qLow, qHigh, qConC, qConF;
    // gains des écarts
    double gLowB, cLowA, gLowV, gMid, oMid, cCon, fCon_, gHighB, cHighA, gHighV;
    // plateaux HPF / LPF
    double hpfA, lpfA;
    // part de chaque section, 0 … 1 (pour la couleur analogique)
    double amtLow, amtMid, amtCon, amtHigh;

    static constexpr double qShelf = 0.6, qVoice = 0.9;
    static constexpr double w2 = 0.7, w1 = 0.3;   // mélange 2e / 1er ordre des renforts LOW et HIGH

    static Design make (const Settings& s, double fs)
    {
        Design d {};
        const double top = 0.45 * fs;
        auto clampF = [top] (double f) { return std::clamp (f, 5.0, top); };
        d.fLow2 = clampF (s.lowF);  d.fLow1b = clampF (s.lowF * 0.8);  d.fLow1a = clampF (s.lowF);  d.fLowV = clampF (s.lowF * 1.9);
        d.fMid = clampF (s.midF);   d.fMidLo = clampF (s.midF / 2.4);   d.fMidHi = clampF (s.midF * 2.4);
        d.fCon = clampF (s.conF);   d.fConLo = clampF (s.conF / s.conW);   d.fConHi = clampF (s.conF * s.conW);
        d.fHigh2 = clampF (s.highF); d.fHigh1b = clampF (s.highF * 1.25); d.fHigh1a = clampF (s.highF); d.fHighV = clampF (s.highF / 1.9);
        d.fHpf = clampF (s.hpfF);   d.fLpf = clampF (s.lpfF);
        d.qMid = s.midQ;
        d.qSkirt = s.midQ * 1.2;
        d.qLow = s.lowQ;
        d.qHigh = s.highQ;
        // un CONTOUR plus large garde la même forme : ses trois bandes s'élargissent ensemble
        const double spread = std::sqrt (3.0 / s.conW);
        d.qConC = 0.65 * spread;
        d.qConF = 0.8 * spread;

        d.gLowB = dbToGain (std::max (0.0, s.lowB)) - 1.0;
        d.cLowA = 1.0 - dbToGain (-std::max (0.0, s.lowA));
        // 3e bande du LOW, la voix de la courbe : un petit creux au-dessus du plateau qui se creuse avec le renfort, et
        // une petite bosse que l'atténuation relève (les deux réglages agissent sur elle, elle les fait interagir)
        d.gLowV = dbToGain (-0.12 * std::max (0.0, s.lowB) + 0.06 * std::max (0.0, s.lowA)) - 1.0;
        d.gMid = dbToGain (s.midG) - 1.0;
        d.oMid = dbToGain (-0.09 * s.midG) - 1.0;            // dépassement : ~1 dB de signe opposé à ±12 dB
        // positif : centre creusé de conV dB, flancs relevés de la moitié ; négatif : l'inverse
        d.cCon = dbToGain (-s.conV) - 1.0;
        d.fCon_ = dbToGain (0.5 * s.conV) - 1.0;
        d.gHighB = dbToGain (std::max (0.0, s.highB)) - 1.0;
        d.cHighA = 1.0 - dbToGain (-std::max (0.0, s.highA));
        d.gHighV = dbToGain (-0.10 * std::max (0.0, s.highB) + 0.05 * std::max (0.0, s.highA)) - 1.0;   // symétrique, sous le plateau
        d.hpfA = std::pow (10.0, std::min (0.0, s.hpfDb) / 40.0);
        d.lpfA = std::pow (10.0, std::min (0.0, s.lpfDb) / 40.0);

        d.amtLow = std::min (1.0, std::max (s.lowB, s.lowA) / 12.0);
        d.amtMid = std::min (1.0, std::abs (s.midG) / 12.0);
        d.amtCon = std::min (1.0, std::abs (s.conV) / 10.0);
        d.amtHigh = std::min (1.0, std::max (s.highB, s.highA) / 12.0);
        return d;
    }
};

inline double warp (double f, double fs) { return std::tan (pi * f / fs); }

//==============================================================================
/** Réponse en fréquence du réseau (partie linéaire) à la fréquence F : exactement celle des filtres du traitement. */
struct Response
{
    double fs;
    explicit Response (double sampleRate) : fs (sampleRate) {}

    cplx sAt (double F, double fc) const { return cplx (0.0, warp (std::min (F, 0.4999 * fs), fs) / warp (fc, fs)); }
    static cplx den2 (cplx s, double q) { return s * s + s / q + 1.0; }
    cplx lp2 (double F, double fc, double q) const { const auto s = sAt (F, fc); return 1.0 / den2 (s, q); }
    cplx hp2 (double F, double fc, double q) const { const auto s = sAt (F, fc); return s * s / den2 (s, q); }
    cplx bpn (double F, double fc, double q) const { const auto s = sAt (F, fc); return (s / q) / den2 (s, q); }
    cplx lp1 (double F, double fc) const { const auto s = sAt (F, fc); return 1.0 / (1.0 + s); }
    cplx hp1 (double F, double fc) const { const auto s = sAt (F, fc); return s / (1.0 + s); }

    /** plateau grave (A² en dB·2) : g préwarpé divisé par √A, comme dans Shelf. */
    cplx lowShelf (double F, double fc, double A, double q) const
    {
        const auto s = cplx (0.0, warp (std::min (F, 0.4999 * fs), fs) / (warp (fc, fs) / std::sqrt (A)));
        const auto den = den2 (s, q);
        return 1.0 + (1.0 / q) * (A - 1.0) * (s / den) + (A * A - 1.0) / den;
    }
    cplx highShelf (double F, double fc, double A, double q) const
    {
        const auto s = cplx (0.0, warp (std::min (F, 0.4999 * fs), fs) / (warp (fc, fs) * std::sqrt (A)));
        const auto den = den2 (s, q);
        return A * A + (1.0 / q) * (1.0 - A) * A * (s / den) + (1.0 - A * A) / den;
    }

    /** filtre anti-continu des écarts (1er ordre, 4 Hz) */
    cplx dcBlock (double F) const
    {
        const double R = std::exp (-2.0 * pi * 4.0 / fs);
        const cplx z1 = std::polar (1.0, -2.0 * pi * std::min (F, 0.4999 * fs) / fs);
        return (1.0 - z1) / (1.0 - R * z1);
    }

    /** les écarts des quatre sections, sans les plateaux HPF / LPF */
    cplx deviation (const Design& d, double F) const
    {
        cplx dev = d.gLowB * (Design::w2 * lp2 (F, d.fLow2, d.qLow) + Design::w1 * lp1 (F, d.fLow1b)) - d.cLowA * lp1 (F, d.fLow1a)
                 + d.gLowV * bpn (F, d.fLowV, Design::qVoice);
        dev += d.gMid * bpn (F, d.fMid, d.qMid) + d.oMid * (bpn (F, d.fMidLo, d.qSkirt) + bpn (F, d.fMidHi, d.qSkirt));
        dev += d.cCon * bpn (F, d.fCon, d.qConC) + d.fCon_ * (bpn (F, d.fConLo, d.qConF) + bpn (F, d.fConHi, d.qConF));
        dev += d.gHighB * (Design::w2 * hp2 (F, d.fHigh2, d.qHigh) + Design::w1 * hp1 (F, d.fHigh1b)) - d.cHighA * hp1 (F, d.fHigh1a)
             + d.gHighV * bpn (F, d.fHighV, Design::qVoice);
        return dev;
    }

    cplx total (const Design& d, double F) const
    {
        const cplx pre = lowShelf (F, d.fHpf, d.hpfA, Design::qShelf) * highShelf (F, d.fLpf, d.lpfA, Design::qShelf);
        return pre * (1.0 + dcBlock (F) * deviation (d, F));
    }
};

//==============================================================================
/** Grille logarithmique des calculs de contrôle (interaction des bandes, gain auto). */
struct Grid
{
    static constexpr int n = 48;
    static double freq (int i) { return 20.0 * std::pow (1100.0, (double) i / (n - 1)); }   // 20 Hz … 22 kHz
};

/** Interaction des bandes : en parallèle, deux renforts qui se recouvrent s'additionnent. On laisse au plus 3 dB au-dessus
    du plus fort renfort seul ; au-delà, les renforts (LOW, HIGH, MID positif) sont réduits ensemble, dans leur
    proportion. Rend le facteur appliqué aux dB de renfort (1 = rien à faire). */
inline double boostScale (const Settings& s, double fs)
{
    const double solo = std::max ({ s.lowB, s.highB, std::max (0.0, s.midG), s.conV > 0 ? 0.5 * s.conV : -s.conV });
    if (solo <= 0.5) return 1.0;
    const Response r (fs);
    auto peakFor = [&] (double k)
    {
        Settings t = s;
        t.lowB *= k; t.highB *= k; if (t.midG > 0) t.midG *= k;
        t.hpfDb = t.lpfDb = 0;
        const auto d = Design::make (t, fs);
        double peak = 0;
        for (int i = 0; i < Grid::n; ++i) peak = std::max (peak, std::abs (1.0 + r.deviation (d, Grid::freq (i))));
        return gainToDb (peak);
    };
    const double limit = solo + 3.0;
    if (peakFor (1.0) <= limit) return 1.0;
    double lo = 0.35, hi = 1.0;
    for (int it = 0; it < 7; ++it)
    {
        const double mid = 0.5 * (lo + hi);
        (peakFor (mid) > limit ? hi : lo) = mid;
    }
    return lo;
}

inline Settings scaled (Settings s, double k)
{
    s.lowB *= k; s.highB *= k; if (s.midG > 0) s.midG *= k;
    return s;
}

/** Pondération K de l'ITU-R BS.1770 (sonie LUFS), en puissance : plateau aigu +4 dB à 1681 Hz, passe-haut 38 Hz. */
inline double kWeight (double f)
{
    const cplx s1 (0.0, f / 1681.97), s2 (0.0, f / 38.135);
    const double A = std::pow (10.0, 4.0 / 40.0), q1 = 0.7072, q2 = 0.5003;
    // plateau aigu analogique (gain A² en haut)
    const cplx shelf = A * (A * s1 * s1 + (std::sqrt (A) / q1) * s1 + 1.0) / (s1 * s1 + (std::sqrt (A) / q1) * s1 + A);
    const cplx hp = s2 * s2 / (s2 * s2 + s2 / q2 + 1.0);
    return std::norm (shelf * hp);
}

/** Puissance relative perçue d'une courbe : bruit rose (même énergie par octave = grille logarithmique uniforme), pondéré
    K comme la mesure de sonie. 1 = même sonie que sans traitement. */
inline double perceivedPower (const Design& d, double fs)
{
    const Response r (fs);
    double sum = 0, wsum = 0;
    for (int i = 0; i < Grid::n; ++i)
    {
        const double f = Grid::freq (i);
        const double w = kWeight (f);
        sum += w * std::norm (r.total (d, f));
        wsum += w;
    }
    return sum / wsum;
}

//==============================================================================
/** Le réseau d'un canal, échantillon par échantillon. */
struct Network
{
    Shelf hpf, lpf;
    Svf low2, lowV, mid, midLo, midHi, con, conLo, conHi, high2, highV;
    OnePole low1b, low1a, high1b, high1a;
    Design d {};
    // couleur analogique par section
    static constexpr double headroom = 1.4;   // l'écart (relatif au signal) où la courbe commence vraiment
    double drive[4] { 1, 1, 1, 1 }, bias[4] {}, wet[4] {}, envDrive = 0, env = 0, envA = 0, envR = 0;
    bool anyWet = false;
    double dcX = 0, dcY = 0, dcR = 0.999;

    void reset()
    {
        for (auto* f : { &hpf, &lpf }) f->reset();
        for (auto* f : { &low2, &lowV, &mid, &midLo, &midHi, &con, &conLo, &conHi, &high2, &highV }) f->reset();
        for (auto* f : { &low1b, &low1a, &high1b, &high1a }) f->reset();
        env = dcX = dcY = 0;
    }

    void prepare (double fs)
    {
        dcR = std::exp (-2.0 * pi * 4.0 / fs);
        envA = 1.0 - std::exp (-1.0 / (0.005 * fs));
        envR = 1.0 - std::exp (-1.0 / (0.120 * fs));
    }

    /** coefficients d'après les réglages ; drift : petites dérives analogiques déjà appliquées aux réglages */
    void design (const Settings& s, double fs)
    {
        d = Design::make (s, fs);
        auto shelf = [fs] (Shelf& sh, double f, double A, bool low)
        {
            const double g = warp (f, fs) * (low ? 1.0 / std::sqrt (A) : std::sqrt (A));
            sh.f.set (g, Design::qShelf);
            const double k = 1.0 / Design::qShelf;
            if (low) { sh.m0 = 1.0; sh.m1 = k * (A - 1.0); sh.m2 = A * A - 1.0; }
            else     { sh.m0 = A * A; sh.m1 = k * (1.0 - A) * A; sh.m2 = 1.0 - A * A; }
        };
        shelf (hpf, d.fHpf, d.hpfA, true);
        shelf (lpf, d.fLpf, d.lpfA, false);
        low2.set (warp (d.fLow2, fs), d.qLow);
        high2.set (warp (d.fHigh2, fs), d.qHigh);
        lowV.set (warp (d.fLowV, fs), Design::qVoice);
        highV.set (warp (d.fHighV, fs), Design::qVoice);
        mid.set (warp (d.fMid, fs), d.qMid);
        midLo.set (warp (d.fMidLo, fs), d.qSkirt);
        midHi.set (warp (d.fMidHi, fs), d.qSkirt);
        con.set (warp (d.fCon, fs), d.qConC);
        conLo.set (warp (d.fConLo, fs), d.qConF);
        conHi.set (warp (d.fConHi, fs), d.qConF);
        low1b.set (warp (d.fLow1b, fs));  low1a.set (warp (d.fLow1a, fs));
        high1b.set (warp (d.fHigh1b, fs)); high1a.set (warp (d.fHigh1a, fs));
    }

    /** couleur analogique : E = caractère × ANALOG (0 … 1). Chaque section a son propre étage non linéaire, dont la part
        (wet) et l'attaque (drive) montent avec ce qu'on lui demande : au neutre, rien ; un peu, presque rien ; poussée,
        des harmoniques (paires par la polarisation, impaires par la courbe), une compression douce de ses crêtes. */
    void colour (double E)
    {
        const double amt[4] { d.amtLow, d.amtMid, d.amtCon, d.amtHigh };
        anyWet = false;
        for (int i = 0; i < 4; ++i)
        {
            wet[i] = std::min (1.0, 1.6 * E) * std::pow (amt[i], 0.8);
            drive[i] = 1.0 + 1.8 * E * std::pow (amt[i], 1.5);
            bias[i] = 0.12 * E * amt[i];
            anyWet = anyWet || wet[i] > 1.0e-6;
        }
        envDrive = 0.35 * E;
    }

    /** saturation douce d'un écart : pente 1 à l'origine (transparent à faible niveau), réserve de headroom avant la
        courbe, paire si bias ≠ 0. L'énergie de toutes les sections (env) pousse un peu l'attaque de chacune. */
    inline double sat (double x, int i) const
    {
        if (wet[i] <= 1.0e-6) return x;
        const double D = drive[i] * (1.0 + envDrive * std::min (2.0, env));
        const double u = x / headroom;
        const double t0 = std::tanh (D * bias[i]);
        const double y = headroom * (std::tanh (D * (u + bias[i])) - t0) / (D * (1.0 - t0 * t0));
        return x + wet[i] * (y - x);
    }

    inline double tick (double x)
    {
        x = lpf.tick (hpf.tick (x));
        low2.tick (x); low1b.tick (x); low1a.tick (x); lowV.tick (x);
        mid.tick (x); midLo.tick (x); midHi.tick (x);
        con.tick (x); conLo.tick (x); conHi.tick (x);
        high2.tick (x); high1b.tick (x); high1a.tick (x); highV.tick (x);

        double dl = d.gLowB * (Design::w2 * low2.lp + Design::w1 * low1b.lp) - d.cLowA * low1a.lp + d.gLowV * lowV.bpn();
        double dm = d.gMid * mid.bpn() + d.oMid * (midLo.bpn() + midHi.bpn());
        double dc = d.cCon * con.bpn() + d.fCon_ * (conLo.bpn() + conHi.bpn());
        double dh = d.gHighB * (Design::w2 * high2.hp + Design::w1 * high1b.hp) - d.cHighA * high1a.hp + d.gHighV * highV.bpn();

        if (anyWet)
        {
            dl = sat (dl, 0); dm = sat (dm, 1); dc = sat (dc, 2); dh = sat (dh, 3);
        }
        const double sum = dl + dm + dc + dh;
        // interaction : l'énergie de toutes les sections pousse un peu chacune (voir sat)
        const double a = std::abs (sum);
        env += (a > env ? envA : envR) * (a - env);

        // anti-continu sur les écarts seulement (la part paire de la saturation), jamais sur le signal direct
        const double y = sum - dcX + dcR * dcY;
        dcX = sum; dcY = y;
        return x + y;
    }
};
//==============================================================================
/** Le transformateur (XFORMER), à l'entrée et à la sortie, sur L et R. Ce n'est pas un saturateur posé sur tout le
    signal : comme le noyau d'un transformateur, c'est le grave qui sature (la part sous ~120 Hz passe par une courbe
    douce, légèrement asymétrique pour les harmoniques paires), le reste passe droit ; l'aigu s'arrondit un peu (1er
    ordre, d'autant plus bas que le transformateur est poussé). À 0, la sortie est exactement l'entrée. */
struct Transformer
{
    OnePole core, hf;
    double dcX = 0, dcY = 0, dcR = 0.999, fs = 88200, lastK = -1;

    void prepare (double sampleRate) { fs = sampleRate; dcR = std::exp (-2.0 * pi * 5.0 / fs); core.set (warp (120.0, fs)); reset(); }
    void reset() { core.reset(); hf.reset(); dcX = dcY = 0; lastK = -1; }

    static double hfCorner (double k, double fs) { return std::min (0.45 * fs, 72000.0 * (1.0 - 0.45 * k)); }

    /** k : 0 … 1 (lissé par l'appelant) */
    inline double tick (double x, double k)
    {
        core.tick (x);
        hf.tick (x);   // tourne toujours : pas de saut d'état quand k s'éloigne de 0
        if (k <= 1.0e-6) { dcX = dcY = 0; return x; }
        if (std::abs (k - lastK) > 1.0e-4) { hf.set (warp (hfCorner (k, fs), fs)); lastK = k; }
        const double lf = core.lp;
        const double D = 1.0 + 2.2 * k, b = 0.07 * k;
        const double t0 = std::tanh (D * b);
        const double sat = (std::tanh (D * (lf + b)) - t0) / (D * (1.0 - t0 * t0));
        const double dev = k * (sat - lf);
        const double dv = dev - dcX + dcR * dcY;   // la part continue de l'asymétrie ne passe pas
        dcX = dev; dcY = dv;
        const double y = x + dv;
        return y + k * (hf.lp - x);                 // arrondi de l'aigu (linéaire)
    }

    /** la partie linéaire (l'arrondi de l'aigu), pour la courbe affichée */
    static cplx response (double F, double k, double fs)
    {
        if (k <= 1.0e-6) return 1.0;
        const cplx s (0.0, warp (std::min (F, 0.4999 * fs), fs) / warp (hfCorner (k, fs), fs));
        return 1.0 + k * (1.0 / (1.0 + s) - 1.0);
    }
};
} // namespace eclipse
