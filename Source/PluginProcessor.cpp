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
        // The ID stays "amount" so existing sessions and automation still bind;
        // only the display name changed.
        juce::ParameterID{"amount", 1}, "Amplitude",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 80.0f));

    // Low-pass filter envelope. Bipolar and centred (knob at 12 o'clock) on
    // zero, which is off. Positive opens the filter with the wave, negative
    // inverts it so the peak of the wave closes the filter.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filter", 1}, "Filter Env",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));

    // Cutoff is the ceiling the envelope sweeps down from. Fully open by
    // default, which keeps the plugin transparent until something is dialled in.
    juce::NormalisableRange<float> cutoffRange(static_cast<float>(pulse::kFiltMinHz),
                                               static_cast<float>(pulse::kFiltMaxHz));
    cutoffRange.setSkewForCentre(1000.0f);

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cutoff", 1}, "Cutoff", cutoffRange,
        static_cast<float>(pulse::kFiltMaxHz),
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int)
            {
                // The unit lives in the string rather than in a separate label,
                // so hosts and the (narrow) text box both read correctly.
                if (v >= 9950.0f) return juce::String(juce::roundToInt(v / 1000.0f)) + " kHz";
                if (v >= 1000.0f) return juce::String(v / 1000.0f, 1) + " kHz";
                return juce::String(juce::roundToInt(v)) + " Hz";
            })));

    // Whole percent: finer steps would only add noise to a knob this small.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"res", 1}, "Resonance",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f));

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
    filterSmoothed_ = 1.0f;
    for (auto& f : filters_)
        f.reset();
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
    const float filtAmt   = apvts.getRawParameterValue("filter")->load() / 100.0f;
    const float cutoffHz  = apvts.getRawParameterValue("cutoff")->load();
    const float resNorm   = apvts.getRawParameterValue("res")->load() / 100.0f;

    // Bypassed only when the filter cannot colour anything: no envelope and the
    // cutoff wide open. That keeps the default settings bit-transparent.
    const bool   filterOn = std::abs(filtAmt) > 1.0e-4f
                         || cutoffHz < static_cast<float>(pulse::kFiltMaxHz) - 1.0f;
    const double resQ     = pulse::resonanceQ(resNorm);

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

    // With the filter off the states are parked so that re-engaging it starts
    // clean. That is silent because the only way in is through the wide-open
    // corner — whichever control is moved first, the filter is still a
    // pass-through at the moment it switches on.
    const int numFiltChans = juce::jmin(numChannels, kMaxFilterChannels);
    if (!filterOn)
    {
        filterSmoothed_ = 1.0f;
        for (auto& f : filters_)
            f.reset();
    }

    pulse::LowpassCoeffs coeffs;

    double phase = phase_;
    float  g = gainSmoothed_;
    float  fPos = filterSmoothed_;
    for (int i = 0; i < numSamples; ++i)
    {
        const float openness = pulse::waveShape(waveIndex, phase, pw);
        const float target    = (1.0f - depth) + depth * openness;
        g += (target - g) * smoothCoef;

        if (filterOn)
        {
            fPos += (pulse::filterEnv(openness, filtAmt) - fPos) * smoothCoef;

            // tan() is far too costly per sample, so the coefficients refresh at
            // a control rate. Every 8 samples is still ~5.5 kHz at 44.1 kHz —
            // orders of magnitude above the LFO, and well inside the 1 ms
            // smoothing so the steps stay inaudible even at high resonance.
            if ((i & 7) == 0)
                coeffs.set(pulse::filterCutoffHz(fPos, cutoffHz), resQ, sampleRate_);

            for (int ch = 0; ch < numFiltChans; ++ch)
            {
                float* d = chans[static_cast<size_t>(ch)];
                d[i] = filters_[static_cast<size_t>(ch)].process(coeffs, d[i]);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            chans[static_cast<size_t>(ch)][i] *= g;

        phase += inc;   // continuous accumulator (not wrapped)
    }
    phase_ = phase;
    gainSmoothed_ = g;
    filterSmoothed_ = fPos;

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
