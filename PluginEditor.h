#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    juce::Slider gainSlider;             // UI control for the Gain parameter (APVTS-driven).
    juce::Slider pulseWidthSlider;       // UI control for the Pulse Width parameter.
    juce::Slider filterCutoffSlider;     // UI control for the Filter Cutoff parameter.
    juce::Label gainLabel;
    juce::Label pulseWidthLabel;
    juce::Label filterCutoffLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;        // Wires the Gain slider to the processor parameter.
    std::unique_ptr<SliderAttachment> pulseWidthAttachment;  // Wires the Pulse Width slider to the processor parameter.
    std::unique_ptr<SliderAttachment> filterAttachment;      // Wires the Filter Cutoff slider to the processor parameter.

    juce::MidiKeyboardComponent midiKeyboard;                // Built-in keyboard so users can trigger notes without external gear.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};