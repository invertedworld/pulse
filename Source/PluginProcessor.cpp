#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LfoEngine.h"
#include <algorithm>
#include <cmath>

//==============================================================================
PulseAudioProcessor::PulseAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

PulseAudioProcessor::~PulseAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout PulseAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray waveChoices;
    for (int i = 0; i < static_cast<int>(pulse::Wave::numWaves); ++i)
        waveChoices.add(pulse::waveNames()[i]);

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"wave", 1}, "Wave", waveChoices,
        static_cast<int>(pulse::Wave::sine)));

    // Single normalised rate control; interpreted as Hz (free) or a note
    // division (synced) depending on the sync switch.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"rate", 1}, "Rate",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"sync", 1}, "Host Sync", false));

    // Square-wave duty cycle (%). Ignored by the other shapes.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pw", 1}, "Pulse Width",
        juce::NormalisableRange<float>(1.0f, 99.0f, 0.1f), 50.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"amount", 1}, "Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 80.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String PulseAudioProcessor::getName() const { return JucePlugin_Name; }
bool PulseAudioProcessor::acceptsMidi() const { return false; }
bool PulseAudioProcessor::producesMidi() const { return false; }
bool PulseAudioProcessor::isMidiEffect() const { return false; }
double PulseAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int PulseAudioProcessor::getNumPrograms() { return 1; }
int PulseAudioProcessor::getCurrentProgram() { return 0; }
void PulseAudioProcessor::setCurrentProgram(int) {}
const juce::String PulseAudioProcessor::getProgramName(int) { return {}; }
void PulseAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void PulseAudioProcessor::prepareToPlay(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    phase_ = 0.0;
    gainSmoothed_ = 1.0f;
}

void PulseAudioProcessor::releaseResources() {}

bool PulseAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void PulseAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = getTotalNumInputChannels();
    const int numSamples  = buffer.getNumSamples();

    for (int i = numChannels; i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    const int   waveIndex = static_cast<int>(apvts.getRawParameterValue("wave")->load());
    const float rateNorm  = apvts.getRawParameterValue("rate")->load();
    const bool  sync      = apvts.getRawParameterValue("sync")->load() >= 0.5f;
    const float pw        = apvts.getRawParameterValue("pw")->load() / 100.0f;
    const float depth     = apvts.getRawParameterValue("amount")->load() / 100.0f;

    // --- Host transport (for sync) ---
    double bpm = 120.0, ppq = 0.0;
    bool gotBpm = false, gotPpq = false;
    bool isPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm())          { bpm = *b; gotBpm = true; }
            if (auto p = pos->getPpqPosition())  { ppq = *p; gotPpq = true; }
        }
    }
    if (gotBpm)
        hostBpm_.store(bpm, std::memory_order_relaxed);

    // --- Effective LFO frequency ---
    double freqHz;
    bool   synced = false;
    if (sync && gotBpm && bpm > 0.0)
    {
        const double beats = pulse::divisionFromNorm(rateNorm).beats;
        freqHz = bpm / (60.0 * beats);
        synced = true;

        // Lock phase to the host transport so cycles align to the beat grid,
        // but only while it is actually playing. When the transport is stopped
        // (Logic keeps streaming audio with a frozen play position), free-run at
        // the synced rate so the LFO keeps moving instead of freezing.
        // phase_ is a continuous accumulator: its integer part is the cycle
        // index (used by Sample & Hold), its fractional part is the phase.
        if (isPlaying && gotPpq)
            phase_ = ppq / beats;
    }
    else
    {
        freqHz = pulse::freeHzFromNorm(rateNorm);
    }

    hostSynced_.store(synced, std::memory_order_relaxed);
    lfoFreqHz_.store(freqHz, std::memory_order_relaxed);

    const double inc  = (sampleRate_ > 0.0) ? freqHz / sampleRate_ : 0.0;
    // One-pole smoothing (~1 ms) to soften square-wave discontinuities.
    const float smoothCoef = static_cast<float>(1.0 - std::exp(-1.0 / (0.001 * sampleRate_)));

    std::vector<float*> chans(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        chans[static_cast<size_t>(ch)] = buffer.getWritePointer(ch);

    double phase = phase_;
    float  g = gainSmoothed_;
    for (int i = 0; i < numSamples; ++i)
    {
        const float openness = pulse::waveShape(waveIndex, phase, pw);
        const float target    = (1.0f - depth) + depth * openness;
        g += (target - g) * smoothCoef;

        for (int ch = 0; ch < numChannels; ++ch)
            chans[static_cast<size_t>(ch)][i] *= g;

        phase += inc;   // continuous accumulator (not wrapped)
    }
    phase_ = phase;
    gainSmoothed_ = g;

    lfoPhase_.store(phase, std::memory_order_relaxed);
}

//==============================================================================
bool PulseAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* PulseAudioProcessor::createEditor()
{
    return new PulseAudioProcessorEditor(*this);
}

void PulseAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PulseAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseAudioProcessor();
}
