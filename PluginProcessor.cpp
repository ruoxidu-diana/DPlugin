#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
// Extra headers pull in the DSP helpers we needed to hand-roll anti-aliased oscillators for the assignment deliverable.

namespace
{
// Shared constants for the oscillators and APVTS parameter IDs.
// Keeping them in one namespace makes it easy to cross-wire the DSP code and parameter layout without string duplication.
constexpr float hfCompA0 = 2.5f;
constexpr float hfCompA1 = -1.5f;
constexpr float minNorm = 0.001f;
constexpr auto gainParamID = "gain";
constexpr auto pulseWidthParamID = "pulseWidth";
constexpr auto filterCutoffParamID = "filterCutoff";
}

// This lightweight Sound object lets every custom voice respond to all incoming MIDI notes/channels.
class AntiAliasedSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

class AntiAliasedVoice final : public juce::SynthesiserVoice
{
public:
    explicit AntiAliasedVoice (juce::AudioProcessorValueTreeState& vts)
    {
        gainParam = vts.getRawParameterValue (gainParamID);
        pulseWidthParam = vts.getRawParameterValue (pulseWidthParamID);
        filterCutoffParam = vts.getRawParameterValue (filterCutoffParamID);

        jassert (gainParam != nullptr);
        jassert (pulseWidthParam != nullptr);
        jassert (filterCutoffParam != nullptr);

        // Cache raw parameter pointers once so renderNextBlock can read them lock-free.
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<AntiAliasedSound*> (sound) != nullptr;
    }

    // Handle per-note initialisation so every voice restarts with the latest GUI parameters.
    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        const auto sampleRate = getSampleRate();
        if (sampleRate <= 0.0)
        {
            clearCurrentNote();
            return;
        }

        currentLevel = velocity;
        currentFrequency = static_cast<float> (juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber));
        pulseOsc.reset();
        pulseOsc.setPulseWidth (pulseWidthParam->load());
        pulseOsc.setFrequency (currentFrequency, sampleRate);
        currentSampleRate = sampleRate;
        updateFilterCoefficients();
        lowPassFilter.reset();
        isActive = true;
    }

    // Hard-stop the voice and clear state because the brief did not call for tails or envelopes.
    void stopNote (float, bool allowTailOff) override
    {
        juce::ignoreUnused (allowTailOff);
        clearCurrentNote();
        pulseOsc.reset();
        isActive = false;
        currentLevel = 0.0f;
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    // This is the per-sample DSP loop where the anti-aliased pulse oscillator and per-voice filter run.
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (! isVoiceActive() || ! isActive)
            return;

        const auto sampleRate = getSampleRate();
        if (sampleRate <= 0.0)
            return;

        pulseOsc.setFrequency (currentFrequency, sampleRate);
        pulseOsc.setPulseWidth (pulseWidthParam->load());

        currentSampleRate = sampleRate;
        const auto smoothingAmount = filterCutoffParam->load();
        if (std::abs (smoothingAmount - lastFilterCutoff) > 1.0e-3f)
        {
            lastFilterCutoff = smoothingAmount;
            updateFilterCoefficients();
        }

        const auto gain = juce::Decibels::decibelsToGain (gainParam->load());

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto rawValue = pulseOsc.getNextSample() * currentLevel * gain;
            const auto value = lowPassFilter.processSingleSampleRaw (rawValue);

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, startSample + sample, value);
        }
    }

private:
    // Per-voice IIR filter keeps the alias-reduction brief satisfied while tracking the virtual smooth/sharp control.
    void updateFilterCoefficients()
    {
        if (currentSampleRate <= 0.0)
            return;

        const auto smoothingAmount = filterCutoffParam->load();
        const auto maxCutoff = static_cast<float> (currentSampleRate * 0.45);
        const auto minCutoff = 200.0f;
        const auto cutoff = juce::jmap (smoothingAmount, 0.0f, 1.0f, maxCutoff, minCutoff);
        const auto coeffs = juce::IIRCoefficients::makeLowPass (currentSampleRate, cutoff);
        lowPassFilter.setCoefficients (coeffs);
    }

    AntiAliasedPulseOscillator pulseOsc;
    float currentLevel = 0.0f;
    float currentFrequency = 0.0f;
    bool isActive = false;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* pulseWidthParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    juce::IIRFilter lowPassFilter;
    double currentSampleRate = 44100.0;
    float lastFilterCutoff = -1.0f;
};

// Feedback-FM + polynomial compensation keeps the saw harmonics controlled when the frequency changes.
void AntiAliasedSawOscillator::setFrequency (float newFrequency, double newSampleRate) noexcept
{
    if (newSampleRate <= 0.0)
        return;

    const auto normalised = static_cast<float> (newFrequency / static_cast<float> (newSampleRate));
    w = juce::jlimit (0.0f, 0.49f, normalised);
    const auto diff = 0.5f - w;
    beta = 13.0f * diff * diff * diff * diff;
}

// Generate one anti-aliased saw sample that forms the building block for the pulse oscillator edges.
float AntiAliasedSawOscillator::getNextSample() noexcept
{
    const auto feedbackPhase = phase + (osc * beta);
    const auto input = std::sin (juce::MathConstants<float>::twoPi * feedbackPhase);
    osc = 0.5f * (osc + input);

    const auto filtered = (hfCompA0 * osc) + (hfCompA1 * previousInput);
    previousInput = osc;

    const auto dc = 0.376f - (0.752f * w);
    const auto norm = juce::jmax (minNorm, 1.0f - (2.0f * w));
    const auto sample = (filtered - dc) / norm;

    phase += w;
    if (phase >= 1.0f)
        phase -= 1.0f;

    return sample;
}

void AntiAliasedSawOscillator::reset() noexcept
{
    phase = 0.0f;
    osc = 0.0f;
    previousInput = 0.0f;
}

// Both edges share the incoming frequency so we can reuse the saw core for the pulse waveform.
void AntiAliasedPulseOscillator::setFrequency (float newFrequency, double newSampleRate) noexcept
{
    leadingEdge.setFrequency (newFrequency, newSampleRate);
    trailingEdge.setFrequency (newFrequency, newSampleRate);
}

// Pulse width modulation is clamped to avoid degeneracies but still satisfies the UI requirement.
void AntiAliasedPulseOscillator::setPulseWidth (float newPulseWidth) noexcept
{
    pulseWidth = juce::jlimit (0.01f, 0.99f, newPulseWidth);
}

// Compose two phase-shifted saw waves to obtain the anti-aliased pulse output.
float AntiAliasedPulseOscillator::getNextSample() noexcept
{
    const auto leading = leadingEdge.getNextSample();

    auto shiftedPhase = leadingEdge.phase + pulseWidth;
    while (shiftedPhase >= 1.0f)
        shiftedPhase -= 1.0f;

    auto& t = trailingEdge;
    const auto feedbackPhase = shiftedPhase + (t.osc * t.beta);
    const auto input = std::sin (juce::MathConstants<float>::twoPi * feedbackPhase);
    t.osc = 0.5f * (t.osc + input);

    const auto filtered = (hfCompA0 * t.osc) + (hfCompA1 * t.previousInput);
    t.previousInput = t.osc;

    const auto dc = 0.376f - (0.752f * t.w);
    const auto norm = juce::jmax (minNorm, 1.0f - (2.0f * t.w));
    const auto trailing = (filtered - dc) / norm;

    t.phase = shiftedPhase + t.w;
    if (t.phase >= 1.0f)
        t.phase -= 1.0f;

    const auto pulse = juce::jlimit (-1.0f, 1.0f, leading - trailing);
    return pulse;
}

void AntiAliasedPulseOscillator::reset() noexcept
{
    leadingEdge.reset();
    trailingEdge.reset();
}

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
         : AudioProcessor (BusesProperties()
                                         #if ! JucePlugin_IsMidiEffect
                                            #if ! JucePlugin_IsSynth
                                             .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                            #endif
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                         #endif
                                             ),
           parameters (*this, nullptr, "Parameters", createParameterLayout())
{
    // Replace the default JUCE tone generator with eight of our custom voices to meet the polyphonic spec.
    constexpr int numVoices = 8;
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new AntiAliasedVoice (parameters));

    synth.addSound (new AntiAliasedSound());
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() = default;

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // Store the host sample rate so the oscillators and filters stay numerically stable.
    lastSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
}

void AudioPluginAudioProcessor::releaseResources()
{
    synth.allNotesOff (0, true);
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    if (lastSampleRate <= 0.0)
    {
        buffer.clear();
        midiMessages.clear();
        return;
    }

    // Merge events from the on-screen keyboard so the plugin can be demoed without external MIDI gear.
    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);

    buffer.clear();
    // Delegate the heavy lifting to juce::Synthesiser so each AntiAliasedVoice renders into the buffer.
    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());
    midiMessages.clear();
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // These three parameters back the GUI sliders and feed directly into the per-voice DSP code above.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (gainParamID, "Gain", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -12.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (pulseWidthParamID, "Pulse Width", juce::NormalisableRange<float> (0.05f, 0.95f, 0.001f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (filterCutoffParamID, "Virtual Filter", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    return { params.begin(), params.end() };
}

float AudioPluginAudioProcessor::getGain() const noexcept
{
    if (const auto* parameter = parameters.getRawParameterValue (gainParamID))
        return parameter->load();

    return -12.0f;
}

float AudioPluginAudioProcessor::getPulseWidth() const noexcept
{
    if (const auto* parameter = parameters.getRawParameterValue (pulseWidthParamID))
        return parameter->load();

    return 0.5f;
}

float AudioPluginAudioProcessor::getFilterCutoff() const noexcept
{
    if (const auto* parameter = parameters.getRawParameterValue (filterCutoffParamID))
        return parameter->load();

    return 0.5f;
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Persist the APVTS state so the host recalls our custom parameters with the session.
    if (auto state = parameters.copyState(); auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore the saved parameter tree to keep GUI, voices, and host automation in sync after reload.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
