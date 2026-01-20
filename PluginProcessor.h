#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

// These custom oscillator types implement the anti-aliased saw/pulse algorithms required by the assignment.

//==============================================================================
class AntiAliasedSawOscillator final
{
public:
    void setFrequency (float newFrequency, double newSampleRate) noexcept;
    float getNextSample() noexcept;
    void reset() noexcept;

private:
    friend class AntiAliasedPulseOscillator;
    float phase = 0.0f;
    float osc = 0.0f;
    float previousInput = 0.0f;
    float w = 0.0f;
    float beta = 0.0f;
};

class AntiAliasedPulseOscillator final
{
public:
    void setFrequency (float newFrequency, double newSampleRate) noexcept;
    void setPulseWidth (float newPulseWidth) noexcept;
    float getNextSample() noexcept;
    void reset() noexcept;

private:
    AntiAliasedSawOscillator leadingEdge;
    AntiAliasedSawOscillator trailingEdge;
    float pulseWidth = 0.5f;
};

//==============================================================================
class AudioPluginAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }  // Expose shared parameter state for GUI bindings.
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }           // Allows the editor's on-screen keyboard to feed MIDI into the processor.
    float getGain() const noexcept;
    float getPulseWidth() const noexcept;
    float getFilterCutoff() const noexcept;

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;                         // Hosts Gain/Pulse Width/Filter Cutoff parameters.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::Synthesiser synth;                                               // Manages AntiAliasedVoice instances for polyphony.
    double lastSampleRate = 44100.0;                                       // Cached sample rate for safety checks in processBlock().
    juce::MidiKeyboardState keyboardState;                                 // Captures events from the built-in MIDI keyboard component.
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};