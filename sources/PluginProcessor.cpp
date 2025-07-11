#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

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
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
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
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::ignoreUnused (samplesPerBlock);

    // Calculate samples per beat based on BPM and sample rate
    // For now, we assume 120 BPM. We’ll update this dynamically using the host info in the next step.
    samplesPerBeat = static_cast<int>((60.0 / 120.0) * sampleRate);

    beatBuffer.setSize(getTotalNumInputChannels(), samplesPerBeat);
    beatBuffer.clear();

    circularWritePosition = 0;
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numChannels = juce::jmin(buffer.getNumChannels(), beatBuffer.getNumChannels());

    // --- Clear output channels that have no corresponding input ---
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // --- Update beat duration if BPM has changed ---
    updateSamplesPerBeatIfNeeded();

    // --- Check if a new beat occurred and calculate sample offset if so ---
    int beatSampleOffset = -1;
    beatJustOccurred = detectBeatAndCalculateOffset(numSamples, beatSampleOffset);

    // --- Write audio to circular beat buffer ---
    writeToCircularBuffer(buffer, numChannels, numSamples);

    // --- If a beat just occurred, send buffer with correct alignment to GUI ---
    if (beatJustOccurred && beatSampleOffset >= 0)
    {
        // Align the beat in the buffer using the offset relative to write pointer
        const int beatStartIndex = (circularWritePosition + samplesPerBeat - numSamples - beatSampleOffset) % samplesPerBeat;

        if (auto* editor = dynamic_cast<AudioPluginAudioProcessorEditor*>(getActiveEditor()))
            editor->pushBuffer(beatBuffer, beatStartIndex);
    }
}

void AudioPluginAudioProcessor::updateSamplesPerBeatIfNeeded()
{
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            if (pos->getBpm().hasValue())
            {
                const double bpm = *pos->getBpm();
                const double sampleRate = getSampleRate();
                const int newSamplesPerBeat = static_cast<int>((60.0 / bpm) * sampleRate);

                if (newSamplesPerBeat != samplesPerBeat)
                {
                    samplesPerBeat = newSamplesPerBeat;
                    beatBuffer.setSize(getTotalNumInputChannels(), samplesPerBeat);
                    beatBuffer.clear();
                    circularWritePosition = 0;
                }
            }
        }
    }
}

bool AudioPluginAudioProcessor::detectBeatAndCalculateOffset(int numSamples, int& beatSampleOffset)
{
    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            const auto& info = *pos;

            if (info.getIsPlaying() && info.getPpqPosition().hasValue() && info.getBpm().hasValue())
            {
                const double currentPpq = *info.getPpqPosition();
                const double bpm = *info.getBpm();
                const int currentBeat = static_cast<int>(std::floor(currentPpq));
                const int lastBeat = static_cast<int>(std::floor(lastPpqPosition));

                if ((currentBeat > lastBeat) || (currentPpq < lastPpqPosition))
                {
                    const double ppqPerSample = bpm / 60.0 / getSampleRate();
                    const double beatFraction = currentPpq - std::floor(currentPpq);
                    const double samplesSinceLastBeat = beatFraction / ppqPerSample;

                    beatSampleOffset = static_cast<int>(std::round(samplesSinceLastBeat));
                    beatSampleOffset = juce::jlimit(0, numSamples - 1, beatSampleOffset);
                    lastPpqPosition = currentPpq;
                    return true;
                }

                lastPpqPosition = currentPpq;
            }
        }
    }

    return false;
}

void AudioPluginAudioProcessor::writeToCircularBuffer(const juce::AudioBuffer<float>& buffer,
                                                      int numChannels, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* input = buffer.getReadPointer(ch);
            float* circular = beatBuffer.getWritePointer(ch);

            circular[circularWritePosition] = input[i];
        }

        circularWritePosition = (circularWritePosition + 1) % samplesPerBeat;
    }
}


//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
