#include "PluginEditor.h"

BridgeEditor::BridgeEditor(BridgeProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      gainAttach(p.apvts, "gain", gainSlider),
      bufferAttach(p.apvts, "bufferMs", bufferSlider),
      muteAttach(p.apvts, "mute", muteButton)
{
    setSize(420, 260);

    addAndMakeVisible(deviceLabel);
    addAndMakeVisible(deviceBox);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(gainSlider);
    addAndMakeVisible(bufferSlider);
    addAndMakeVisible(gainLabel);
    addAndMakeVisible(bufferLabel);
    addAndMakeVisible(muteButton);

    gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    bufferSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bufferSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    deviceBox.onChange = [this] { if (! updatingBox) deviceChosen(); };
    refreshButton.onClick = [this] { refreshDevices(); };

    refreshDevices();
    startTimerHz(15);
}

void BridgeEditor::refreshDevices()
{
    devices = dab::listRenderDevices();
    const auto savedId = proc.getDeviceId();
    const auto savedName = proc.getDeviceName();

    updatingBox = true;
    deviceBox.clear(juce::dontSendNotification);
    deviceBox.addItem("(none)", 1);
    int selected = 1;
    bool found = savedId.isEmpty();
    for (size_t i = 0; i < devices.size(); ++i)
    {
        deviceBox.addItem(devices[i].name, (int) i + 2);
        if (devices[i].id == savedId) { selected = (int) i + 2; found = true; }
    }
    if (! found)
    {
        const int id = (int) devices.size() + 2;
        deviceBox.addItem(savedName + " (unavailable)", id);
        selected = id;
    }
    deviceBox.setSelectedId(selected, juce::dontSendNotification);
    updatingBox = false;
}

void BridgeEditor::deviceChosen()
{
    const int id = deviceBox.getSelectedId();
    if (id == 1)
        proc.setDevice({}, {});
    else if (id - 2 < (int) devices.size())
        proc.setDevice(devices[(size_t) (id - 2)].id, devices[(size_t) (id - 2)].name);
}

void BridgeEditor::timerCallback()
{
    const float peak = proc.getMeterPeak();
    meterLevel = peak > meterLevel ? peak : meterLevel * 0.85f;

    if (++tickCount % 30 == 0)
    {
        if (! deviceBox.isPopupActive())
            refreshDevices();
        proc.retryIfUnavailable();
    }

    juce::String msg;
    juce::Colour col = juce::Colours::lightgreen;
    if (proc.getDeviceId().isEmpty())
    {
        msg = "No device selected.";
        col = juce::Colours::lightgrey;
    }
    else if (proc.getCaptureStatus() == dab::CaptureStatus::Running)
        msg = "Capturing: " + proc.getDeviceName();
    else
    {
        msg = "WARNING: device unavailable - retrying...";
        col = juce::Colours::orange;
    }
    statusLabel.setText(msg, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, col);
    repaint(meterArea);
}

void BridgeEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff202428));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("DesktopAudioBridge", 12, 6, getWidth() - 24, 22, juce::Justification::centredLeft);

    g.setColour(juce::Colours::black);
    g.fillRect(meterArea);
    const float db = juce::Decibels::gainToDecibels(meterLevel, -60.0f);
    const float frac = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour(db > -1.0f ? juce::Colours::red : db > -12.0f ? juce::Colours::yellow : juce::Colours::limegreen);
    g.fillRect(meterArea.withWidth((int) ((float) meterArea.getWidth() * frac)));
    g.setColour(juce::Colours::grey);
    g.drawRect(meterArea);
}

void BridgeEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(26);
    deviceLabel.setBounds(r.removeFromTop(20));
    auto row = r.removeFromTop(26);
    refreshButton.setBounds(row.removeFromRight(70));
    row.removeFromRight(6);
    deviceBox.setBounds(row);
    statusLabel.setBounds(r.removeFromTop(26));
    r.removeFromTop(4);

    auto g = r.removeFromTop(26);
    gainLabel.setBounds(g.removeFromLeft(90));
    gainSlider.setBounds(g);
    r.removeFromTop(4);
    auto b = r.removeFromTop(26);
    bufferLabel.setBounds(b.removeFromLeft(90));
    bufferSlider.setBounds(b);
    r.removeFromTop(4);
    muteButton.setBounds(r.removeFromTop(26));
    r.removeFromTop(6);
    meterArea = r.removeFromTop(16);
}
