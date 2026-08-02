#pragma once

#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
class PulseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit PulseAudioProcessorEditor(PulseAudioProcessor&);
    ~PulseAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    class PulseLookAndFeel;
    class WaveDisplay;
    class WaveSelector;
    class HelpButton;

    PulseAudioProcessor& processorRef;
    std::unique_ptr<PulseLookAndFeel> lnf_;
    std::unique_ptr<WaveDisplay> display_;
    std::unique_ptr<WaveSelector> waveSelector_;
    std::unique_ptr<HelpButton> helpButton_;
    juce::TooltipWindow tooltips_ { this, 700 };

    juce::Slider     widthKnob_;
    juce::Slider     rateKnob_;
    juce::TextButton syncButton_ { "SYNC" };
    juce::Slider     amountKnob_;
    juce::Slider     filterKnob_;
    juce::Slider     cutoffKnob_;   // smaller pair under the filter knob
    juce::Slider     resKnob_;

    juce::Label waveLabel_, widthLabel_, rateLabel_, amountLabel_, filterLabel_;
    juce::Label cutoffLabel_, resLabel_;
    juce::Label rateValue_;   // shows "1.00 Hz" or "1/4" under the rate knob

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   widthAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   rateAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   syncAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   amountAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   filterAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   cutoffAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   resAtt_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulseAudioProcessorEditor)
};
