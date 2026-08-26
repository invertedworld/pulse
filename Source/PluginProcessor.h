/*
    This file is part of Pulse, an LFO tremolo and resonant filter plugin.
    Copyright (C) 2026 Mark Hammond

    Pulse is free software: you can redistribute it and/or modify it under the
    terms of the GNU Affero General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    Pulse is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
    more details.

    You should have received a copy of the GNU Affero General Public License
    along with Pulse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Filter.h"

//==============================================================================
// Pulse — an LFO tremolo / attenuator. A configurable-shape LFO (sine, saw up,
// saw down, square) modulates the amplitude of the incoming signal. The LFO can
// run free (in Hz) or lock to the host tempo and transport (musical divisions).
//==============================================================================
class PulseAudioProcessor final : public juce::AudioProcessor
{
public:
    PulseAudioProcessor();
    ~PulseAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    // Thread-safe accessors for the editor's scrolling display (written on the
    // audio thread, read on the message thread).
    double getLfoPhase()  const { return lfoPhase_.load(std::memory_order_relaxed); }
    double getLfoFreqHz() const { return lfoFreqHz_.load(std::memory_order_relaxed); }
    double getHostBpm()   const { return hostBpm_.load(std::memory_order_relaxed); }
    bool   getHostSynced() const { return hostSynced_.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    double sampleRate_ = 44100.0;
    double phase_ = 0.0;            // continuous LFO phase (int part = cycle index)
    float  gainSmoothed_ = 1.0f;    // one-pole smoothed gain (softens square edges)
    float  filterSmoothed_ = 1.0f;  // one-pole smoothed cutoff position (1 = open)

    // The bus layout is limited to mono/stereo, so two is always enough; the
    // extra slots just keep the code safe if that ever changes.
    static constexpr int kMaxFilterChannels = 8;
    std::array<pulse::LowpassState, kMaxFilterChannels> filters_{};

    std::atomic<double> lfoPhase_{0.0};
    std::atomic<double> lfoFreqHz_{1.0};
    std::atomic<double> hostBpm_{120.0};
    std::atomic<bool>   hostSynced_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulseAudioProcessor)
};
