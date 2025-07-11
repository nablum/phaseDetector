#pragma once

#include <JuceHeader.h>

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

    //==============================================================================
    void updateSamplesPerBeatIfNeeded();
    bool detectBeatAndCalculateOffset(int numSamples, int& beatSampleOffset);
    void writeToCircularBuffer(const juce::AudioBuffer<float>& buffer, int numChannels, int numSamples);

private:
    //==============================================================================
    double lastPpqPosition = 0.0;
    bool beatJustOccurred = false;
    juce::AudioBuffer<float> beatBuffer; // Circular buffer to store one beat of audio
    int circularWritePosition = 0; // Current write index in the circular buffer
    int samplesPerBeat = 0; // Samples per beat, recomputed dynamically
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
