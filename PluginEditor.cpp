#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      midiKeyboard (processorRef.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setSize (360, 220); // Compact footprint to fit three faders plus the built-in keyboard.

    const auto configureSlider = [] (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::LinearVertical); // Use fader-style sliders per assignment requirements.
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
        slider.setPopupDisplayEnabled (true, false, nullptr);
    };

    configureSlider (gainSlider);
    configureSlider (pulseWidthSlider);
    configureSlider (filterCutoffSlider);

    const auto configureLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    };

    configureLabel (gainLabel, "Gain");
    configureLabel (pulseWidthLabel, "Pulse Width");
    configureLabel (filterCutoffLabel, "Virtual Filter");

    gainSlider.setTooltip ("Control overall output gain");           // Helps testers understand each control quickly.
    pulseWidthSlider.setTooltip ("Blend between thin and wide pulse timbres");
    filterCutoffSlider.setTooltip ("0 = smooth/open, 1 = sharp/filtered");

    addAndMakeVisible (gainSlider);
    addAndMakeVisible (pulseWidthSlider);
    addAndMakeVisible (filterCutoffSlider);
    addAndMakeVisible (midiKeyboard);

    midiKeyboard.setAvailableRange (36, 96); // Limit to a practical register for testing.
    midiKeyboard.setVelocity (0.8f, true);   // Default velocity so users can audition without hardware keyboards.

    auto& valueTree = processorRef.getValueTreeState();
    gainAttachment = std::make_unique<SliderAttachment> (valueTree, "gain", gainSlider);             // Binds slider to APVTS.
    pulseWidthAttachment = std::make_unique<SliderAttachment> (valueTree, "pulseWidth", pulseWidthSlider);
    filterAttachment = std::make_unique<SliderAttachment> (valueTree, "filterCutoff", filterCutoffSlider);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (20.0f);
    g.drawFittedText ("Anti-Aliased Synth", getLocalBounds().removeFromTop (30), juce::Justification::centred, 1); // Simple title banner.
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto headerArea = area.removeFromTop (40); // Reserve space for the title text.
    juce::ignoreUnused (headerArea);

    auto controlArea = area.removeFromTop (110); // Three faders live in this row.

    auto keyboardArea = area;
    keyboardArea.removeFromTop (10); // Small gap between faders and keyboard.
    midiKeyboard.setBounds (keyboardArea);

    const int columnWidth = controlArea.getWidth() / 3;

    const auto layoutColumn = [] (juce::Rectangle<int> bounds, juce::Label& label, juce::Slider& slider)
    {
        auto labelArea = bounds.removeFromTop (24);
        label.setBounds (labelArea);
        slider.setBounds (bounds.reduced (10));
    };

    auto firstColumn = controlArea.removeFromLeft (columnWidth);
    layoutColumn (firstColumn, gainLabel, gainSlider);

    auto secondColumn = controlArea.removeFromLeft (columnWidth);
    layoutColumn (secondColumn, pulseWidthLabel, pulseWidthSlider);

    layoutColumn (controlArea, filterCutoffLabel, filterCutoffSlider);
}