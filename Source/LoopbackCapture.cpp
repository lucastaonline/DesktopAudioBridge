#include "LoopbackCapture.h"
#include "Log.h"

#include <juce_core/juce_core.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cmath>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

namespace dab
{
namespace
{
struct ComScope
{
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    HRESULT hr;
};

// PKEY_Device_FriendlyName, defined locally to avoid needing INITGUID/propsys.
const PROPERTYKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

template <typename T>
void safeRelease(T*& p)
{
    if (p != nullptr) { p->Release(); p = nullptr; }
}

juce::String getFriendlyName(IMMDevice* dev)
{
    juce::String result;
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal != nullptr)
            result = juce::String(v.pwszVal);
        PropVariantClear(&v);
        props->Release();
    }
    return result;
}

float readSample(const uint8_t* p, bool isFloat, int bytes)
{
    if (isFloat)
    {
        float f;
        std::memcpy(&f, p, sizeof(float));
        return f;
    }
    switch (bytes)
    {
        case 2: { int16_t s; std::memcpy(&s, p, 2); return (float) s / 32768.0f; }
        case 3: { int32_t s = (int32_t) ((p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24)); return (float) (s >> 8) / 8388608.0f; }
        case 4: { int32_t s; std::memcpy(&s, p, 4); return (float) ((double) s / 2147483648.0); }
        default: return 0.0f;
    }
}
} // namespace

std::vector<DeviceInfo> listRenderDevices()
{
    std::vector<DeviceInfo> out;
    ComScope com;

    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**) &en)))
        return out;

    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
    {
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; ++i)
        {
            IMMDevice* dev = nullptr;
            if (FAILED(col->Item(i, &dev))) continue;
            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id)) && id != nullptr)
            {
                out.push_back({ juce::String(id), getFriendlyName(dev) });
                CoTaskMemFree(id);
            }
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    return out;
}

LoopbackCapture::LoopbackCapture(StereoRing& r) : juce::Thread("DAB loopback"), ring(r) {}

LoopbackCapture::~LoopbackCapture() { stop(); }

void LoopbackCapture::start(const juce::String& endpointId)
{
    stop();
    deviceId = endpointId;
    if (deviceId.isEmpty()) return;

    ring.startCount.store(ring.writeCount.load());
    ring.generation.fetch_add(1);
    status.store((int) CaptureStatus::Unavailable);
    startThread(juce::Thread::Priority::high);
}

void LoopbackCapture::stop()
{
    signalThreadShouldExit();
    stopThread(3000);
    ring.deviceRate.store(0.0);
    status.store((int) CaptureStatus::Stopped);
}

void LoopbackCapture::run()
{
    ComScope com;
    timeBeginPeriod(1); // default 15.6 ms timer granularity would make polling bursty
    // Keep retrying on its own (device removed, format/default change, sleep/wake)
    // so recovery never depends on the editor being open.
    while (! threadShouldExit())
    {
        runCapture();
        if (threadShouldExit()) break;
        status.store((int) CaptureStatus::Unavailable);
        ring.deviceRate.store(0.0);
        for (int i = 0; i < 20 && ! threadShouldExit(); ++i)
            juce::Thread::sleep(100);
        ring.startCount.store(ring.writeCount.load());
        ring.generation.fetch_add(1);
    }
    timeEndPeriod(1);
}

void LoopbackCapture::runCapture()
{
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    WAVEFORMATEX* fmt = nullptr;

    auto cleanup = [&]
    {
        if (client != nullptr) client->Stop();
        if (fmt != nullptr) CoTaskMemFree(fmt);
        safeRelease(cap);
        safeRelease(client);
        safeRelease(dev);
        safeRelease(en);
    };

    auto step = [&](HRESULT hr, const char* what)
    {
        if (SUCCEEDED(hr)) return true;
        log(juce::String("capture: ") + what + " failed, hr=0x" + juce::String::toHexString((int) hr));
        return false;
    };

    log("capture: opening " + deviceId);
    if (! step(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**) &en), "CoCreateInstance")
        || ! step(en->GetDevice(deviceId.toWideCharPointer(), &dev), "GetDevice")
        || ! step(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) &client), "Activate")
        || ! step(client->GetMixFormat(&fmt), "GetMixFormat")
        || ! step(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr), "Initialize")
        || ! step(client->GetService(__uuidof(IAudioCaptureClient), (void**) &cap), "GetService")
        || ! step(client->Start(), "Start"))
    {
        cleanup();
        return;
    }

    const int channels = juce::jmax(1, (int) fmt->nChannels);
    const int bytes = fmt->wBitsPerSample / 8;
    const int frameBytes = fmt->nBlockAlign;
    bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        isFloat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
    const double rate = (double) fmt->nSamplesPerSec;
    const auto openedRate = fmt->nSamplesPerSec;
    const auto openedChannels = fmt->nChannels;
    const auto openedBits = fmt->wBitsPerSample;

    log(juce::String::formatted("capture: running, %u Hz, %d ch, %d bit, float=%d, blockAlign=%d",
                                (unsigned) openedRate, (int) openedChannels, (int) openedBits, (int) isFloat, frameBytes));

    ring.deviceRate.store(rate);
    status.store((int) CaptureStatus::Running);

    auto lastTick = juce::Time::getHighResolutionTicks();
    const double tickHz = (double) juce::Time::getHighResolutionTicksPerSecond();
    double silenceDebt = 0.0;
    double idleSeconds = 0.0;
    double sinceCheck = 0.0;
    bool inIdle = false;

    while (! threadShouldExit())
    {
        juce::Thread::sleep(4);
        heartbeat.fetch_add(1);

        UINT32 packet = 0;
        HRESULT hr = cap->GetNextPacketSize(&packet);
        if (FAILED(hr))
        {
            log("capture: GetNextPacketSize failed, hr=0x" + juce::String::toHexString((int) hr));
            break;
        }

        const auto now = juce::Time::getHighResolutionTicks();
        const double elapsed = (double) (now - lastTick) / tickHz;
        lastTick = now;
        if (elapsed > 0.1)
            log(juce::String::formatted("capture: loop stalled %.0f ms", elapsed * 1000.0));

        // Once a second: the endpoint must still be active and keep the same mix
        // format; otherwise the loopback stream can go dead silently.
        sinceCheck += elapsed;
        if (sinceCheck >= 1.0)
        {
            sinceCheck = 0.0;
            DWORD state = 0;
            if (SUCCEEDED(dev->GetState(&state)) && state != DEVICE_STATE_ACTIVE)
            {
                log("capture: endpoint no longer active, state=" + juce::String((int) state));
                break;
            }
            IAudioClient* probe = nullptr;
            WAVEFORMATEX* pf = nullptr;
            bool changed = false;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**) &probe))
                && SUCCEEDED(probe->GetMixFormat(&pf)))
                changed = pf->nSamplesPerSec != openedRate || pf->nChannels != openedChannels || pf->wBitsPerSample != openedBits;
            if (pf != nullptr) CoTaskMemFree(pf);
            safeRelease(probe);
            if (changed)
            {
                log("capture: mix format changed, reopening");
                break;
            }
        }

        if (packet == 0)
        {
            // Loopback delivers nothing while the endpoint is silent. Packets also
            // arrive in ~10 ms bursts, so only treat a long gap as real silence;
            // otherwise silence would be interleaved with real audio.
            idleSeconds += elapsed;
            if (idleSeconds < 0.03) continue;
            if (! inIdle) { inIdle = true; idleEvents.fetch_add(1); }
            silenceDebt += elapsed * rate;
            const int n = (int) silenceDebt;
            silenceDebt -= n;
            for (int i = 0; i < juce::jmin(n, 48000); ++i) ring.push(0.0f, 0.0f);
            continue;
        }
        if (inIdle)
        {
            inIdle = false;
            log(juce::String::formatted("capture: audio resumed after %.2f s idle", idleSeconds));
        }
        idleSeconds = 0.0;
        silenceDebt = 0.0;

        while (packet != 0 && ! threadShouldExit())
        {
            BYTE* p = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = cap->GetBuffer(&p, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr))
            {
                log("capture: GetBuffer failed, hr=0x" + juce::String::toHexString((int) hr));
                cleanup();
                return;
            }
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                discontinuities.fetch_add(1);

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for (UINT32 f = 0; f < frames; ++f)
            {
                if (silent) { ring.push(0.0f, 0.0f); continue; }
                const uint8_t* fp = p + (size_t) f * frameBytes;
                const float l = readSample(fp, isFloat, bytes);
                const float r = channels > 1 ? readSample(fp + bytes, isFloat, bytes) : l;
                ring.push(l, r);
            }
            packets.fetch_add(1);

            cap->ReleaseBuffer(frames);
            if (FAILED(cap->GetNextPacketSize(&packet)))
            {
                log("capture: GetNextPacketSize (inner) failed");
                cleanup();
                return;
            }
        }
    }
    cleanup();
    log("capture: stopped");
}
} // namespace dab

