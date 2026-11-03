#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LoopbackCapture.h"

class BridgeProcessor : public juce::AudioProcessor
{
public:
    BridgeProcessor();
    ~BridgeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DesktopAudioBridge"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Message-thread API
    void setDevice(const juce::String& id, const juce::String& name);
    juce::String getDeviceId() const;
    juce::String getDeviceName() const;
    void retryIfUnavailable();
    dab::CaptureStatus getCaptureStatus() const noexcept { return capture.getStatus(); }
    float getMeterPeak() const noexcept { return meterPeak.load(); }
    std::atomic<int> underruns { 0 }, resyncs { 0 };
    std::atomic<float> fillMs { 0.0f }, effectiveMs { 0.0f };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void restartCapture();

    dab::StereoRing ring;
    dab::LoopbackCapture capture { ring };

    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* muteParam = nullptr;
    std::atomic<float>* bufferMs = nullptr;
    std::atomic<float>* autoBuffer = nullptr;
    double autoMs = 25.0, cleanSeconds = 0.0, shrinkInterval = 5.0;

    // Audio-thread state
    double hostRate = 48000.0;
    uint32_t seenGeneration = 0;
    bool playing = false;
    double readPos = 0.0;
    double smoothFill = 0.0;
    double ratioCorrection = 0.0;
    int64_t primeBase = -1;
    juce::SmoothedValue<float> gainSmoothed;
    std::vector<float> tmpL, tmpR;
    std::atomic<float> meterPeak { 0.0f };

    void renderBridge(int numSamples, float* l, float* r);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeProcessor)
};
