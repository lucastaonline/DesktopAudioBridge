#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgeEditor(BridgeProcessor&);
    ~BridgeEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshDevices();
    void deviceChosen();

    BridgeProcessor& proc;

    juce::Label deviceLabel { {}, "Bridge output (Windows)" };
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "Refresh" };
    juce::Label statusLabel;

    juce::Slider gainSlider, bufferSlider;
    juce::Label gainLabel { {}, "Gain (dB)" }, bufferLabel { {}, "Buffer (ms)" };
    juce::ToggleButton muteButton { "Mute" }, autoButton { "Auto buffer (smallest glitch-free)" };

    std::vector<dab::DeviceInfo> devices;
    float meterLevel = 0.0f;
    juce::Rectangle<int> meterArea;
    int tickCount = 0;
    bool updatingBox = false;

    juce::AudioProcessorValueTreeState::SliderAttachment gainAttach, bufferAttach;
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAttach, autoAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BridgeEditor)
};
