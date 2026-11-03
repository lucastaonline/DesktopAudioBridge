#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace dab
{
struct DeviceInfo
{
    juce::String id;
    juce::String name;
};

// Single-producer (capture thread) / single-consumer (audio thread) ring of
// interleaved stereo float frames. Counters are absolute and never reset.
struct StereoRing
{
    static constexpr int64_t capacity = 1 << 17; // frames (~2.7 s at 48 kHz)
    static constexpr int64_t mask = capacity - 1;

    std::vector<float> data = std::vector<float>((size_t) capacity * 2, 0.0f);
    std::atomic<int64_t> writeCount { 0 };
    std::atomic<int64_t> startCount { 0 }; // writeCount when the current capture session began
    std::atomic<uint32_t> generation { 0 };
    std::atomic<double> deviceRate { 0.0 };

    void push(float l, float r) noexcept
    {
        const auto w = writeCount.load(std::memory_order_relaxed);
        const auto i = (size_t) (w & mask) * 2;
        data[i] = l;
        data[i + 1] = r;
        writeCount.store(w + 1, std::memory_order_release);
    }
};

enum class CaptureStatus : int
{
    Stopped = 0,
    Running = 1,
    Unavailable = 2
};

// Lists active Windows render endpoints.
std::vector<DeviceInfo> listRenderDevices();

// WASAPI loopback capture of one render endpoint, on its own thread.
class LoopbackCapture : private juce::Thread
{
public:
    explicit LoopbackCapture(StereoRing& ringToFill);
    ~LoopbackCapture() override;

    void start(const juce::String& endpointId);
    void stop();
    CaptureStatus getStatus() const noexcept { return (CaptureStatus) status.load(); }

    std::atomic<int64_t> packets { 0 }, heartbeat { 0 }, idleEvents { 0 }, discontinuities { 0 };

private:
    void run() override;
    void runCapture();

    StereoRing& ring;
    juce::String deviceId;
    std::atomic<int> status { (int) CaptureStatus::Stopped };
};
} // namespace dab
