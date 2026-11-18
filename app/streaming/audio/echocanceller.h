#pragma once

#include <cstdint>
#include <mutex>

struct SpeexEchoState_;
struct SpeexPreprocessState_;

// Removes the stream's own audio from the microphone so people on the host
// don't hear themselves or the game when the client uses speakers.
//
// The audio renderer hands every block it queues for playback to
// submitPlayback(), which keeps a short mono copy as the echo reference.
// The microphone then calls process() on each captured frame, which runs
// SpeexDSP's adaptive echo canceller followed by its residual echo and noise
// suppressor. Only one canceller is active at a time.
class EchoCanceller
{
public:
    static constexpr int k_SampleRate = 48000;
    static constexpr int k_FrameSamples = 480; // 10 ms

    EchoCanceller();
    ~EchoCanceller();

    // Creates the SpeexDSP state and starts collecting playback audio
    bool init();

    // Removes echo from one k_FrameSamples mono frame in place (capture thread)
    void process(int16_t* frame);

    // Records interleaved float samples that are about to be played (audio playback thread)
    static void submitPlayback(const float* samples, int frames, int channels);

private:
    void addReference(const float* samples, int frames, int channels);
    void takeReference(int16_t* frame);

    // How long the canceller listens for echo after the sound is played. This covers
    // the playback queue, the audio devices and the room.
    static constexpr int k_TailSamples = k_SampleRate * 300 / 1000;

    // The reference has to reach the canceller before its echo shows up in the microphone.
    // Playback and capture run on separate clocks, so trim the queued reference back to
    // one frame whenever it grows past this.
    static constexpr int k_MaxReferenceBacklog = k_FrameSamples * 4;

    // One second of reference audio
    static constexpr int k_ReferenceCapacity = k_SampleRate;

    static std::mutex s_ActiveLock;
    static EchoCanceller* s_Active;

    SpeexEchoState_* m_EchoState;
    SpeexPreprocessState_* m_PreprocessState;

    // Guarded by s_ActiveLock
    int16_t m_Reference[k_ReferenceCapacity];
    int m_ReferenceStart;
    int m_ReferenceCount;
    uint32_t m_Resyncs;

    int16_t m_Output[k_FrameSamples];
};
