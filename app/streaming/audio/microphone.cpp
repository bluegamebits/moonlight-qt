#include "microphone.h"

#include <Limelight.h>
#include <opus.h>

#include <cstring>

MicrophoneStreamer::MicrophoneStreamer()
    : m_Device(0),
      m_Encoder(nullptr),
      m_AudioSubsystemInitialized(false),
      m_Muted(false),
      m_WasMuted(false),
      m_FrameFill(0),
      m_SendErrors(0)
{
}

MicrophoneStreamer::~MicrophoneStreamer()
{
    stop();
}

bool MicrophoneStreamer::start(const QString& deviceName, bool startMuted)
{
    int err;

    SDL_assert(m_Device == 0);

    if (!LiIsMicrophoneStreamActive()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone passthrough was not negotiated with the host");
        return false;
    }

    m_Encoder = opus_encoder_create(k_SampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (m_Encoder == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "opus_encoder_create() failed: %d",
                     err);
        return false;
    }

    // Voice-tuned settings with in-band FEC so the host can recover from single packet losses.
    // DTX lets us skip sending packets entirely during silence.
    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(48000));
    opus_encoder_ctl(m_Encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(5));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        stop();
        return false;
    }
    m_AudioSubsystemInitialized = true;

    SDL_AudioSpec want = {}, have;
    want.freq = k_SampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 480; // 10 ms callbacks
    want.callback = captureCallback;
    want.userdata = this;

    QByteArray deviceNameUtf8 = deviceName.toUtf8();
    const char* requestedDevice = deviceName.isEmpty() ? nullptr : deviceNameUtf8.constData();

    // SDL converts whatever the device provides into the format we asked for
    m_Device = SDL_OpenAudioDevice(requestedDevice, 1, &want, &have, 0);
    if (m_Device == 0 && requestedDevice != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to open microphone '%s' (%s). Falling back to the default microphone.",
                    requestedDevice,
                    SDL_GetError());
        m_Device = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
    }
    if (m_Device == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open microphone: %s",
                     SDL_GetError());
        stop();
        return false;
    }

    m_FrameFill = 0;
    m_SendErrors = 0;
    m_Muted = startMuted;
    m_WasMuted = startMuted;

    SDL_PauseAudioDevice(m_Device, 0);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone streaming started (%s)%s",
                requestedDevice ? requestedDevice : "default device",
                startMuted ? " - muted" : "");
    return true;
}

void MicrophoneStreamer::stop()
{
    if (m_Device != 0) {
        // Closing the device waits for any in-progress callback to finish
        SDL_CloseAudioDevice(m_Device);
        m_Device = 0;

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone streaming stopped");
    }

    if (m_AudioSubsystemInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_AudioSubsystemInitialized = false;
    }

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }
}

void MicrophoneStreamer::setMuted(bool muted)
{
    m_Muted = muted;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone %s",
                muted ? "muted" : "unmuted");
}

bool MicrophoneStreamer::isMuted() const
{
    return m_Muted;
}

bool MicrophoneStreamer::isActive() const
{
    return m_Device != 0;
}

void SDLCALL MicrophoneStreamer::captureCallback(void* userdata, Uint8* stream, int len)
{
    auto me = static_cast<MicrophoneStreamer*>(userdata);
    me->processSamples(reinterpret_cast<const int16_t*>(stream), len / (int)sizeof(int16_t));
}

void MicrophoneStreamer::processSamples(const int16_t* samples, int count)
{
    bool muted = m_Muted;

    if (muted) {
        // Drop any partially filled frame so stale audio isn't sent on unmute
        m_FrameFill = 0;
        m_WasMuted = true;
        return;
    }
    else if (m_WasMuted) {
        // Start from a clean encoder state after a mute so we don't
        // resume with a burst of predicted audio from before the mute.
        opus_encoder_ctl(m_Encoder, OPUS_RESET_STATE);
        m_WasMuted = false;
    }

    while (count > 0) {
        int toCopy = SDL_min(count, k_FrameSamples - m_FrameFill);
        memcpy(&m_Frame[m_FrameFill], samples, toCopy * sizeof(int16_t));
        m_FrameFill += toCopy;
        samples += toCopy;
        count -= toCopy;

        if (m_FrameFill < k_FrameSamples) {
            break;
        }
        m_FrameFill = 0;

        int len = opus_encode(m_Encoder, m_Frame, k_FrameSamples, m_Packet, sizeof(m_Packet));
        if (len < 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "opus_encode() failed: %d",
                         len);
            continue;
        }
        else if (len <= 2) {
            // DTX frame (silence). There's nothing worth sending.
            continue;
        }

        int err = LiSendMicrophoneOpusData(m_Packet, len);
        if (err != 0 && (m_SendErrors++ % 250) == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LiSendMicrophoneOpusData() failed: %d",
                        err);
        }
    }
}
