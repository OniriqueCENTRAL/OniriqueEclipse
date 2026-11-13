#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/EclipseEngine.h"

//==============================================================================
//  Paramètres d'ECLIPSE : identifiants stables (ne jamais les renommer, les projets les retrouvent par là).
//==============================================================================
namespace params
{
inline const juce::String lowFreq = "lowFreq", lowBoost = "lowBoost", lowAtten = "lowAtten",
                          midFreq = "midFreq", midGain = "midGain", midQ = "midQ",
                          contour = "contour", contourFreq = "contourFreq",
                          highFreq = "highFreq", highBoost = "highBoost", highAtten = "highAtten",
                          hpfOn = "hpfOn", hpfFreq = "hpfFreq", lpfOn = "lpfOn", lpfFreq = "lpfFreq",
                          msLink = "msLink", offsetTarget = "offsetTarget",
                          lowOffset = "lowOffset", midOffset = "midOffset", contourOffset = "contourOffset", highOffset = "highOffset",
                          analog = "analog", character = "character",
                          solo = "solo", delta = "delta", mono = "mono", width = "width",
                          inGain = "inGain", outGain = "outGain", autoGain = "autoGain", bypass = "bypass";

inline juce::String dbText (float v, int decimals = 1)
{
    if (std::abs (v) < 0.05f) v = 0.0f;
    return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + " dB";
}

inline juce::String hzText (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}

inline float parseNumber (const juce::String& t)
{
    const auto lower = t.toLowerCase();
    float v = lower.retainCharacters ("+-.0123456789").getFloatValue();
    if (lower.contains ("k")) v *= 1000.0f;
    return v;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto freq = [&] (const String& id, const String& name, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi, [] (float a, float b, float t) { return a * std::pow (b / a, t); },
                                    [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
                                    [] (float, float, float v) { return v; });
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                   AudioParameterFloatAttributes().withLabel ("Hz")
                       .withStringFromValueFunction ([] (float v, int) { return hzText (v); })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    };
    auto db = [&] (const String& id, const String& name, float lo, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.01f), def,
                   AudioParameterFloatAttributes().withLabel ("dB")
                       .withStringFromValueFunction ([] (float v, int) { return dbText (v); })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    };
    auto pct = [&] (const String& id, const String& name, float hi, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, hi, 0.1f), def,
                   AudioParameterFloatAttributes().withLabel ("%")
                       .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    };
    auto toggle = [&] (const String& id, const String& name, bool def, const String& on, const String& off)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def,
                   AudioParameterBoolAttributes().withStringFromValueFunction ([on, off] (bool b, int) { return b ? on : off; })));
    };
    auto choice = [&] (const String& id, const String& name, const StringArray& items, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

   #if ONIRIQUE_UI_TEST
    const bool demo = SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("demo");
   #else
    const bool demo = false;
   #endif

    freq (lowFreq, "Low Freq", 20.0f, 400.0f, demo ? 70.0f : 60.0f);
    db (lowBoost, "Low Boost", 0.0f, 12.0f, demo ? 4.5f : 0.0f);
    db (lowAtten, "Low Atten", 0.0f, 12.0f, demo ? 2.0f : 0.0f);
    freq (midFreq, "Mid Freq", 150.0f, 8000.0f, demo ? 2800.0f : 1200.0f);
    db (midGain, "Mid", -12.0f, 12.0f, demo ? 2.0f : 0.0f);
    {
        NormalisableRange<float> r (0.4f, 2.5f, 0.01f);
        r.setSkewForCentre (1.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { midQ, 1 }, "Mid Q", r, 0.8f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "Q " + String (v, 2); })
                       .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); })));
    }
    db (contour, "Mid Contour", -10.0f, 10.0f, demo ? 3.0f : 0.0f);
    freq (contourFreq, "Contour Freq", 200.0f, 6000.0f, demo ? 700.0f : 1000.0f);
    freq (highFreq, "High Freq", 1500.0f, 24000.0f, demo ? 12000.0f : 10000.0f);
    db (highBoost, "High Boost", 0.0f, 12.0f, demo ? 3.5f : 0.0f);
    db (highAtten, "High Atten", 0.0f, 12.0f, 0.0f);
    toggle (hpfOn, "HPF", demo, "ON", "OFF");
    freq (hpfFreq, "HPF Freq", 10.0f, 400.0f, demo ? 28.0f : 30.0f);
    toggle (lpfOn, "LPF", false, "ON", "OFF");
    freq (lpfFreq, "LPF Freq", 3000.0f, 30000.0f, 20000.0f);
    toggle (msLink, "M/S Link", ! demo, "LINKED", "OFFSET");
    choice (offsetTarget, "Offset Target", { "SIDE", "MID", "SPLIT" }, 0);
    db (lowOffset, "Low Offset", -12.0f, 12.0f, demo ? -2.0f : 0.0f);
    db (midOffset, "Mid Offset", -12.0f, 12.0f, 0.0f);
    db (contourOffset, "Contour Offset", -10.0f, 10.0f, 0.0f);
    db (highOffset, "High Offset", -12.0f, 12.0f, demo ? 2.0f : 0.0f);
    toggle (analog, "Analog", true, "ON", "OFF");
    pct (character, "Character", 100.0f, 40.0f);
    choice (solo, "Solo", { "STEREO", "MID", "SIDE" }, 0);
    toggle (delta, "Delta", false, "ON", "OFF");
    toggle (mono, "Mono", false, "ON", "OFF");
    pct (width, "Width", 200.0f, demo ? 112.0f : 100.0f);
    db (inGain, "Input", -18.0f, 18.0f, 0.0f);
    db (outGain, "Output", -18.0f, 18.0f, 0.0f);
    toggle (autoGain, "Auto Gain", false, "ON", "OFF");
    toggle (bypass, "Bypass", false, "BYPASSED", "IN");
    return l;
}

/** Lecture des paramètres (thread audio : seulement des chargements atomiques). */
struct Reader
{
    explicit Reader (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        for (const auto* id : { &lowFreq, &lowBoost, &lowAtten, &midFreq, &midGain, &midQ, &contour, &contourFreq, &highFreq,
                                &highBoost, &highAtten, &hpfOn, &hpfFreq, &lpfOn, &lpfFreq, &msLink, &offsetTarget, &lowOffset,
                                &midOffset, &contourOffset, &highOffset, &analog, &character, &solo, &delta, &mono, &width,
                                &inGain, &outGain, &autoGain, &bypass })
            v.push_back (s.getRawParameterValue (*id));
    }

    eclipse::EngineParams read() const
    {
        int i = 0;
        auto f = [&] { return (double) v[(size_t) i++]->load(); };
        auto b = [&] { return v[(size_t) i++]->load() > 0.5f; };
        eclipse::EngineParams p;
        auto& s = p.base;
        s.lowF = f(); s.lowB = f(); s.lowA = f();
        s.midF = f(); s.midG = f(); s.midQ = f();
        s.conV = f(); s.conF = f();
        s.highF = f(); s.highB = f(); s.highA = f();
        s.hpfDb = b() ? -24.0 : 0.0; s.hpfF = f();
        s.lpfDb = b() ? -24.0 : 0.0; s.lpfF = f();
        p.link = b(); p.target = (int) std::lround (f());
        p.ofsLow = f(); p.ofsMid = f(); p.ofsCon = f(); p.ofsHigh = f();
        p.analog = b(); p.character = f() / 100.0;
        p.solo = (int) std::lround (f()); p.delta = b(); p.mono = b(); p.width = f() / 100.0;
        p.inDb = f(); p.outDb = f(); p.autoGain = b(); p.bypass = b();
        return p;
    }

    juce::AudioProcessorValueTreeState& state;
    std::vector<std::atomic<float>*> v;
};

/** Ce qu'un preset ou la mémoire A / B laissent tel quel : l'écoute (bypass, solo, delta, mono). */
inline juce::StringArray notStored() { return { bypass, solo, delta, mono }; }
} // namespace params
