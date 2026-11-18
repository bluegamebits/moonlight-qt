#include "echocanceller.h"

#include "SDL_compat.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cstring>

std::mutex EchoCanceller::s_ActiveLock;
EchoCanceller* EchoCanceller::s_Active = nullptr;

EchoCanceller::EchoCanceller()
    : m_EchoState(nullptr),
      m_PreprocessState(nullptr),
      m_ReferenceStart(0),
      m_ReferenceCount(0),
      m_Resyncs(0)
{
}

EchoCanceller::~EchoCanceller()
{
    {
        std::lock_guard<std::mutex> lock(s_ActiveLock);
        if (s_Active == this) {
            s_Active = nullptr;
        }
    }

    if (m_PreprocessState != nullptr) {
        speex_preprocess_state_destroy(m_PreprocessState);
    }
    if (m_EchoState != nullptr) {
        speex_echo_state_destroy(m_EchoState);
    }
}

bool EchoCanceller::init()
{
    SDL_assert(m_EchoState == nullptr);

    m_EchoState = speex_echo_state_init(k_FrameSamples, k_TailSamples);
    m_PreprocessState = speex_preprocess_state_init(k_FrameSamples, k_SampleRate);
    if (m_EchoState == nullptr || m_PreprocessState == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create the echo canceller");
        return false;
    }

    int sampleRate = k_SampleRate;
    speex_echo_ctl(m_EchoState, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);

    // The preprocessor removes the echo the adaptive filter leaves behind. It only
    // applies its suppression gain with noise suppression on, so keep that enabled.
    int enabled = 1, disabled = 0;
    speex_preprocess_ctl(m_PreprocessState, SPEEX_PREPROCESS_SET_ECHO_STATE, m_EchoState);
    speex_preprocess_ctl(m_PreprocessState, SPEEX_PREPROCESS_SET_DENOISE, &enabled);
    speex_preprocess_ctl(m_PreprocessState, SPEEX_PREPROCESS_SET_AGC, &disabled);
    speex_preprocess_ctl(m_PreprocessState, SPEEX_PREPROCESS_SET_DEREVERB, &disabled);

    {
        std::lock_guard<std::mutex> lock(s_ActiveLock);
        m_ReferenceStart = 0;
        m_ReferenceCount = 0;
        s_Active = this;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone echo cancellation enabled (%d ms tail)",
                k_TailSamples * 1000 / k_SampleRate);
    return true;
}

void EchoCanceller::submitPlayback(const float* samples, int frames, int channels)
{
    std::lock_guard<std::mutex> lock(s_ActiveLock);
    if (s_Active != nullptr && frames > 0 && channels > 0) {
        s_Active->addReference(samples, frames, channels);
    }
}

void EchoCanceller::addReference(const float* samples, int frames, int channels)
{
    for (int i = 0; i < frames; i++) {
        // The microphone hears all speakers at once, so mix down to mono
        float sum = 0;
        for (int ch = 0; ch < channels; ch++) {
            sum += samples[i * channels + ch];
        }
        float mono = std::min(std::max(sum / channels, -1.0f), 1.0f);

        if (m_ReferenceCount == k_ReferenceCapacity) {
            // Full, so drop the oldest sample
            m_ReferenceStart = (m_ReferenceStart + 1) % k_ReferenceCapacity;
            m_ReferenceCount--;
        }

        m_Reference[(m_ReferenceStart + m_ReferenceCount) % k_ReferenceCapacity] = (int16_t)(mono * 32767.0f);
        m_ReferenceCount++;
    }
}

void EchoCanceller::takeReference(int16_t* frame)
{
    std::lock_guard<std::mutex> lock(s_ActiveLock);

    if (m_ReferenceCount > k_MaxReferenceBacklog) {
        // Keep only the newest frame so the reference stays ahead of its echo
        int dropped = m_ReferenceCount - k_FrameSamples;
        m_ReferenceStart = (m_ReferenceStart + dropped) % k_ReferenceCapacity;
        m_ReferenceCount -= dropped;

        if ((m_Resyncs++ % 60) == 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Echo canceller dropped %d ms of playback reference to stay in sync",
                        dropped * 1000 / k_SampleRate);
        }
    }

    // Use silence for whatever playback hasn't provided (nothing playing, or a gap)
    int available = SDL_min(m_ReferenceCount, k_FrameSamples);
    for (int i = 0; i < available; i++) {
        frame[i] = m_Reference[(m_ReferenceStart + i) % k_ReferenceCapacity];
    }
    memset(&frame[available], 0, (k_FrameSamples - available) * sizeof(int16_t));

    m_ReferenceStart = (m_ReferenceStart + available) % k_ReferenceCapacity;
    m_ReferenceCount -= available;
}

void EchoCanceller::process(int16_t* frame)
{
    int16_t reference[k_FrameSamples];
    takeReference(reference);

    speex_echo_cancellation(m_EchoState, frame, reference, m_Output);
    speex_preprocess_run(m_PreprocessState, m_Output);
    memcpy(frame, m_Output, sizeof(m_Output));
}
