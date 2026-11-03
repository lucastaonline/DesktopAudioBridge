#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
inline float hermite(float y0, float y1, float y2, float y3, float x) noexcept
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + y1;
}
} // namespace

BridgeProcessor::BridgeProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createLayout())
{
    gainDb = apvts.getRawParameterValue("gain");
    muteParam = apvts.getRawParameterValue("mute");
    bufferMs = apvts.getRawParameterValue("bufferMs");
    apvts.state.setProperty("deviceId", "", nullptr);
    apvts.state.setProperty("deviceName", "", nullptr);
}

BridgeProcessor::~BridgeProcessor() { capture.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout BridgeProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "gain", 1 }, "Gain",
                                                     NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f,
                                                     AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { "mute", 1 }, "Mute", false));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { "bufferMs", 1 }, "Buffer",
                                                     NormalisableRange<float>(10.0f, 250.0f, 1.0f), 50.0f,
                                                     AudioParameterFloatAttributes().withLabel("ms")));
    return layout;
}

void BridgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    tmpL.assign((size_t) juce::jmax(1, samplesPerBlock), 0.0f);
    tmpR.assign((size_t) juce::jmax(1, samplesPerBlock), 0.0f);
    gainSmoothed.reset(sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue(0.0f);
    playing = false;
    primeBase = -1;
    ratioCorrection = 0.0;
}

bool BridgeProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

// Fills l/r with captured audio resampled to the host rate (adaptive ratio keeps
// the ring fill level near the configured target, absorbing clock drift).
void BridgeProcessor::renderBridge(int n, float* l, float* r)
{
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);

    const double devRate = ring.deviceRate.load();
    if (devRate <= 0.0 || capture.getStatus() != dab::CaptureStatus::Running)
    {
        playing = false;
        primeBase = -1;
        return;
    }

    const auto gen = ring.generation.load();
    if (gen != seenGeneration)
    {
        seenGeneration = gen;
        playing = false;
        primeBase = -1;
        ratioCorrection = 0.0;
    }

    const int64_t w = ring.writeCount.load(std::memory_order_acquire);
    const double target = juce::jmax(64.0, (double) bufferMs->load() * 0.001 * devRate);

    if (! playing)
    {
        if (primeBase < 0) primeBase = w;
        const auto base = juce::jmax(primeBase, ring.startCount.load());
        if ((double) (w - base) < target) return;
        readPos = (double) w - target;
        smoothFill = target;
        playing = true;
        primeBase = -1;
    }

    double fill = (double) w - readPos;
    if (fill > target * 3.0 + devRate * 0.02 || fill > (double) dab::StereoRing::capacity * 0.5)
    {
        readPos = (double) w - target; // far off: hard resync
        fill = target;
        smoothFill = target;
    }

    smoothFill += 0.1 * (fill - smoothFill);
    const double err = (smoothFill - target) / target;
    const double desired = juce::jlimit(-0.02, 0.02, err * 0.02);
    ratioCorrection += 0.05 * (desired - ratioCorrection);
    const double step = devRate / hostRate * (1.0 + ratioCorrection);

    const auto& d = ring.data;
    for (int i = 0; i < n; ++i)
    {
        const auto ip = (int64_t) std::floor(readPos);
        if (ip + 2 >= w || ip < 1)
        {
            playing = false; // underrun: re-prime
            primeBase = -1;
            return;
        }
        const auto x = (float) (readPos - (double) ip);
        const size_t i0 = (size_t) ((ip - 1) & dab::StereoRing::mask) * 2;
        const size_t i1 = (size_t) (ip & dab::StereoRing::mask) * 2;
        const size_t i2 = (size_t) ((ip + 1) & dab::StereoRing::mask) * 2;
        const size_t i3 = (size_t) ((ip + 2) & dab::StereoRing::mask) * 2;
        l[i] = hermite(d[i0], d[i1], d[i2], d[i3], x);
        r[i] = hermite(d[i0 + 1], d[i1 + 1], d[i2 + 1], d[i3 + 1], x);
        readPos += step;
    }
}

void BridgeProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();

    const float targetGain = muteParam->load() > 0.5f ? 0.0f : juce::Decibels::decibelsToGain(gainDb->load());
    gainSmoothed.setTargetValue(targetGain);

    float peak = 0.0f;
    int done = 0;
    while (done < total)
    {
        const int n = juce::jmin(total - done, (int) tmpL.size());
        renderBridge(n, tmpL.data(), tmpR.data());

        for (int i = 0; i < n; ++i)
        {
            const float g = gainSmoothed.getNextValue();
            const float l = tmpL[(size_t) i] * g;
            const float r = tmpR[(size_t) i] * g;
            peak = juce::jmax(peak, std::abs(l), std::abs(r));
            if (numCh == 1)
                buffer.getWritePointer(0)[done + i] += 0.5f * (l + r);
            else if (numCh >= 2)
            {
                buffer.getWritePointer(0)[done + i] += l;
                buffer.getWritePointer(1)[done + i] += r;
            }
        }
        done += n;
    }
    meterPeak.store(peak);
}

juce::AudioProcessorEditor* BridgeProcessor::createEditor() { return new BridgeEditor(*this); }

void BridgeProcessor::setDevice(const juce::String& id, const juce::String& name)
{
    apvts.state.setProperty("deviceId", id, nullptr);
    apvts.state.setProperty("deviceName", name, nullptr);
    restartCapture();
}

juce::String BridgeProcessor::getDeviceId() const { return apvts.state.getProperty("deviceId").toString(); }
juce::String BridgeProcessor::getDeviceName() const { return apvts.state.getProperty("deviceName").toString(); }

void BridgeProcessor::restartCapture()
{
    capture.stop();
    capture.start(getDeviceId());
}

void BridgeProcessor::retryIfUnavailable()
{
    if (getDeviceId().isNotEmpty() && capture.getStatus() != dab::CaptureStatus::Running)
        restartCapture();
}

void BridgeProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void BridgeProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            restartCapture();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BridgeProcessor(); }
