#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#if ECLIPSE_LICENCE
 #include "License/LicenseGuard.h"
#endif

class EclipseProcessor : public juce::AudioProcessor
{
public:
    EclipseProcessor();
    ~EclipseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // le bypass du DAW pilote le paramètre Bypass (IN de la barre du haut) : fondu propre, latence alignée
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (params::bypass); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ONIRIQUE ECLIPSE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** les réglages, lus comme le moteur les lit (pour la courbe de l'interface) */
    eclipse::EngineParams readParams() const { return reader.read(); }
    double getWorkingRate() const { return workingRate.load(); }
    eclipse::Meters& meters() { return engine.meters; }

    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetBank presets { apvts, "ECLIPSE", eclipsepresets::stockPresets(), eclipsepresets::tagNames(), params::notStored() };
#if ECLIPSE_LICENCE
    onirique::license::Guard license { "onirique-eclipse" };
#endif

private:
    void process (juce::AudioBuffer<float>&, bool bypassed);

    params::Reader reader { apvts };
    eclipse::Engine engine;
    std::atomic<double> workingRate { 88200.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EclipseProcessor)
};
