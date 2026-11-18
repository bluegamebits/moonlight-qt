#pragma once

#include "SDL_compat.h"

#include <QString>

#include <atomic>
#include <cstdint>

struct OpusEncoder;
class EchoCanceller;

// Captures the local microphone, encodes it with Opus and sends it to the
// host using LiSendMicrophoneOpusData(). Requires a host that negotiated
// LI_FF_MICROPHONE_V1 (see LiIsMicrophoneStreamActive()).
class MicrophoneStreamer
{
public:
    MicrophoneStreamer();
    ~MicrophoneStreamer();

    // Opens the named capture device (or the system default if empty) and starts streaming.
    // With echo cancellation, the stream's own audio is removed from the microphone.
    bool start(const QString& deviceName, bool startMuted, bool echoCancellation);
    void stop();

    void setMuted(bool muted);
    bool isMuted() const;
    bool isActive() const;

private:
    static void SDLCALL captureCallback(void* userdata, Uint8* stream, int len);
    void processSamples(const int16_t* samples, int count);

    static constexpr int k_SampleRate = 48000;
    static constexpr int k_FrameSamples = 960; // 20 ms
    static constexpr int k_MaxPacketSize = 1000;

    SDL_AudioDeviceID m_Device;
    OpusEncoder* m_Encoder;
    EchoCanceller* m_EchoCanceller;
    bool m_AudioSubsystemInitialized;
    std::atomic<bool> m_Muted;
    std::atomic<bool> m_WasMuted;

    int16_t m_Frame[k_FrameSamples];
    int m_FrameFill;
    unsigned char m_Packet[k_MaxPacketSize];
    uint32_t m_SendErrors;
};
