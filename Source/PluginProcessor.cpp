#include "PluginProcessor.h"
#include "PluginEditor.h"

EclipseProcessor::EclipseProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ECLIPSE", params::layout())
{
    apvts.state.setProperty ("preset", "INIT", nullptr);   // l'état de départ : les valeurs par défaut
}

bool EclipseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void EclipseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
#if ECLIPSE_LICENCE
    license.refresh();
#endif
    engine.prepare (sampleRate, samplesPerBlock);
    workingRate.store (2.0 * sampleRate);
    setLatencySamples (engine.getLatency());
}

void EclipseProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());
    // un hôte envoie parfois des NaN / inf : ils ne doivent jamais entrer dans les mémoires des filtres
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* s = buffer.getWritePointer (c);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (s[i])) s[i] = 0.0f;
    }
    auto p = reader.read();
    if (bypassed) p.bypass = true;
    engine.process (buffer, p);
#if ECLIPSE_LICENCE
    license.process (buffer, getSampleRate(), isNonRealtime());
#endif
}

void EclipseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, false); }
void EclipseProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer, true); }

void EclipseProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    presets.writeState (state);   // le côté A/B écouté, et l'autre
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void EclipseProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    presets.readState (state);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* EclipseProcessor::createEditor() { return new EclipseEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EclipseProcessor(); }
