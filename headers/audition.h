#pragma once
#include <string.h>

// ---------------------------------------------------------------------------
// Audition Voice — real-time preview playback mixed into the master bus.
//
// This is the "Media Explorer" preview engine. It renders inside the engine's
// master audio callback (via audition_process_voice, called from
// audio_callback) rather than opening a second output device, so it cannot
// conflict with WASAPI Exclusive / ASIO and needs no separate device.
//
// Real-time safety (see arch.md Hard Rules):
//   * The audio callback path never calls malloc/free/fopen/fread or any GUI.
//   * PCM is fully decoded into pre-allocated ping-pong buffers before play.
//   * The worker only ever writes the buffer that is NOT currently published
//     (activeIdx), and the audio thread only reads the published one, so there
//     is never a concurrent read/write of the same buffer and nothing is ever
//     freed while the audio thread may be reading it.
//   * All cross-thread control state uses volatile LONG + Interlocked* (never
//     bare volatile for synchronization — arch.md Hard Rule 5).
//
// The two pre-allocated buffers hold up to AUDITION_MAX_FRAMES frames of
// interleaved-stereo float PCM each. A worker decodes into the inactive buffer
// and atomically publishes it via audition_play(); the audio thread re-reads
// activeIdx once per <=1024-frame chunk, so it sees a consistent swap.
// ---------------------------------------------------------------------------

#define AUDITION_MAX_FRAMES (30 * 44100)   // 30 s of stereo float at 44.1 kHz
#define AUDITION_FADE_FRAMES 66            // ~1.5 ms declick ramp at 44.1 kHz
                                           // (fast so pausing leaves no audible
                                           //  trail)

// Two pre-allocated ping-pong buffers. Static arrays (no malloc), zero-filled.
static float g_audBuf[2][AUDITION_MAX_FRAMES * 2];
static int   g_audPreallocated = 0;

// Cross-thread control state. Every field is written/read with Interlocked*
// from the UI/worker side and read with InterlockedCompareExchange on the
// audio thread. read_frame_idx is advanced only by the audio thread (single
// writer) and read by the UI for the scrub head.
typedef struct {
    volatile LONG activeIdx;     // which ping-pong buffer is published (0/1)
    volatile LONG totalFrames;   // frames in the published buffer (<= MAX)
    volatile LONG playing;       // 1 = audition requested
    volatile LONG trigger_stop;  // 1 = request a fade-out before stop
    volatile LONG speedBits;     // bit-cast float speed (0.5..2.0)
    volatile LONG generation;    // bumped by UI on each new request; lets the
                                 // worker discard stale decode results
    volatile LONG read_frame_idx;// audio-thread-owned playhead (for UI scrub)
    volatile LONG repeat;        // 1 = loop the preview; 0 = play once and stop
    volatile LONG volBits;       // bit-cast float output level (0..1)
    volatile LONG seek_frame;    // -1 = no seek pending; else target frame
} AuditionVoiceState;
static AuditionVoiceState g_audState;

// ---------------------------------------------------------------------------
// Publish / control helpers (called from the UI / worker threads, NOT the
// audio thread).
// ---------------------------------------------------------------------------

// Point the audition voice at a freshly decoded buffer and start playing it.
static inline void audition_play(int bufIdx, LONG totalFrames, LONG generation) {
    if (bufIdx < 0 || bufIdx > 1) return;
    if (totalFrames < 0) totalFrames = 0;
    if (totalFrames > (LONG)AUDITION_MAX_FRAMES) totalFrames = (LONG)AUDITION_MAX_FRAMES;
    InterlockedExchange(&g_audState.totalFrames, totalFrames);
    InterlockedExchange(&g_audState.activeIdx, bufIdx);
    InterlockedExchange(&g_audState.generation, generation);
    InterlockedExchange(&g_audState.playing, 1);
    InterlockedExchange(&g_audState.trigger_stop, 0);
}

// Request a fade-out and stop. The audio thread ramps the gain to zero before
// going silent, so there is no pop.
static inline void audition_stop(void) {
    InterlockedExchange(&g_audState.trigger_stop, 1);
    InterlockedExchange(&g_audState.playing, 0);
}

// Clamp and publish the preview speed (0.5x .. 2.0x). Applied in real time by
// the audio thread via linear resampling.
static inline void audition_set_speed(float speed) {
    if (speed < 0.5f) speed = 0.5f;
    if (speed > 2.0f) speed = 2.0f;
    LONG bits;
    memcpy(&bits, &speed, sizeof(bits));
    InterlockedExchange(&g_audState.speedBits, bits);
}

static inline bool audition_is_playing(void) {
    return InterlockedCompareExchange(&g_audState.playing, 0, 0) != 0;
}

// Enable/disable looped preview playback (Repeat toggle, default off).
static inline void audition_set_repeat(bool repeat) {
    InterlockedExchange(&g_audState.repeat, repeat ? 1 : 0);
}

// Set the audition output level (0..1). Applied in real time by the audio
// thread as a scalar gain on the mixed signal.
static inline void audition_set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    LONG bits;
    memcpy(&bits, &vol, sizeof(bits));
    InterlockedExchange(&g_audState.volBits, bits);
}

// Request a seek to a frame (clamped to the published buffer). The audio
// thread applies it at the next chunk boundary.
static inline void audition_seek(LONG frame) {
    InterlockedExchange(&g_audState.seek_frame, frame);
}

// Audio-thread-local flag: true while the voice is producing (or fading out)
// audio. The audio callback uses this to keep rendering through the ~3 ms
// de-click fade after a stop request, so the fade isn't cut off by an early
// return that would otherwise leave a pop.
static bool s_auditionRendering = false;
static inline bool audition_voice_rendering(void) {
    return s_auditionRendering;
}

// Current playhead in the published buffer, for the UI waveform scrub head.
static inline LONG audition_get_read_frame(void) {
    LONG f = InterlockedCompareExchange(&g_audState.read_frame_idx, 0, 0);
    return f < 0 ? 0 : f;
}

// Write access for the worker thread: returns the pre-allocated buffer and its
// capacity so the worker can decode into the inactive slot. Only call from the
// worker; never from the audio thread.
static inline float* audition_get_write_buffer(int bufIdx) {
    return g_audBuf[bufIdx & 1];
}

// Called once at startup (main.c, next to audio_limiter_preinit) so the first
// audio callback never allocates. Static arrays need no malloc; this just
// guarantees they are zeroed.
static inline void audition_preinit(void) {
    if (g_audPreallocated) return;
    memset(g_audBuf, 0, sizeof(g_audBuf));
    memset(&g_audState, 0, sizeof(g_audState));
    g_audPreallocated = 1;
}

// Called at shutdown.
static inline void audition_shutdown(void) {
    memset(&g_audState, 0, sizeof(g_audState));
    g_audPreallocated = 0;
}

// ---------------------------------------------------------------------------
// Real-time mixer. Called from audio_callback once per <=1024-frame chunk,
// BEFORE the master limiter, so the audition passes through the same master
// treatment as everything else. Adds the audition signal into outL/outR.
//
// Linear interpolation at the configured speed (the same resampling the
// timeline uses for clip playback rate). A gain envelope provides a ~3 ms
// micro-fade on start, stop, and file switch to eliminate pops.
// ---------------------------------------------------------------------------
static void audition_process_voice(float* outL, float* outR, ma_uint32 frames) {
    // Audio-thread-local state (persists across calls).
    static LONG   s_active = -1;      // buffer currently being rendered
    static double s_srcPos = 0.0;     // source read position
    static float  s_gain   = 0.0f;    // gain envelope [0,1]
    static bool   s_first  = true;

    if (s_first) {
        s_active = -1;
        s_srcPos = 0.0;
        s_gain   = 0.0f;
        s_first  = false;
    }

    LONG playing = InterlockedCompareExchange(&g_audState.playing, 0, 0);
    LONG stop    = InterlockedCompareExchange(&g_audState.trigger_stop, 0, 0);
    LONG repeat  = InterlockedCompareExchange(&g_audState.repeat, 0, 0);
    LONG vbits   = InterlockedCompareExchange(&g_audState.volBits, 0, 0);
    float vol;
    memcpy(&vol, &vbits, sizeof(vol));
    if (!(vol > 0.0f)) vol = 1.0f;
    if (vol > 1.0f) vol = 1.0f;
    LONG active  = InterlockedCompareExchange(&g_audState.activeIdx, 0, 0);
    LONG total   = InterlockedCompareExchange(&g_audState.totalFrames, 0, 0);
    LONG sb      = InterlockedCompareExchange(&g_audState.speedBits, 0, 0);
    float speed;
    memcpy(&speed, &sb, sizeof(speed));
    if (!(speed > 0.0f) || speed < 0.25f || speed > 4.0f) speed = 1.0f;

    if (total < 0) total = 0;
    if (total > (LONG)AUDITION_MAX_FRAMES) total = (LONG)AUDITION_MAX_FRAMES;
    if (active < 0 || active > 1) active = 0;

    bool wantPlay = (playing != 0) && (stop == 0);
    const float fadeStep = 1.0f / (float)AUDITION_FADE_FRAMES;

    // Buffer switch (new file published): retrigger from the start with a
    // fade-in so the previous buffer's tail can't pop.
    if (active != s_active) {
        s_active = active;
        s_srcPos = 0.0;
        s_gain   = 0.0f;
    }

    // Consume a pending seek at the chunk boundary (clamped to the buffer).
    LONG seek = InterlockedCompareExchange(&g_audState.seek_frame, -1, -1);
    if (seek >= 0) {
        if (seek >= total) seek = total > 0 ? total - 1 : 0;
        s_srcPos = (double)seek;
        InterlockedExchange(&g_audState.seek_frame, -1);
    }

    // Ramp the envelope toward the target (1.0 playing, 0.0 stopped).
    float target = wantPlay ? 1.0f : 0.0f;
    if (s_gain < target) { s_gain += fadeStep; if (s_gain > target) s_gain = target; }
    else if (s_gain > target) { s_gain -= fadeStep; if (s_gain < target) s_gain = target; }

    // If there is nothing to render (stopped and already silent), output zero.
    if (s_gain <= 0.0005f && !wantPlay) {
        s_auditionRendering = false;
        return;
    }
    s_auditionRendering = true;

    const float* buf = g_audBuf[s_active];
    double srcPos = s_srcPos;
    for (ma_uint32 i = 0; i < frames; ++i) {
        // Advance the envelope (covers the fade-in after a retrigger).
        if (s_gain < target) { s_gain += fadeStep; if (s_gain > target) s_gain = target; }

        double sp = srcPos;
        ma_uint64 i0 = (ma_uint64)sp;
        if ((ma_uint64)total == 0) { outL[i] += 0.0f; outR[i] += 0.0f; continue; }
        if (i0 >= (ma_uint64)total) i0 = (ma_uint64)total - 1;
        ma_uint64 i1 = i0 + 1;
        if (i1 >= (ma_uint64)total) i1 = 0;
        float frac = (float)(sp - (double)i0);
        float l = buf[i0 * 2 + 0] + frac * (buf[i1 * 2 + 0] - buf[i0 * 2 + 0]);
        float r = buf[i0 * 2 + 1] + frac * (buf[i1 * 2 + 1] - buf[i0 * 2 + 1]);
        outL[i] += l * s_gain * vol;
        outR[i] += r * s_gain * vol;

        srcPos += (double)speed;
        if (srcPos >= (double)total) {
            if (repeat != 0) {
                srcPos -= (double)total;   // loop back to the start
            } else {
                // Play-once: reached the end. Request a fade-out (the gain
                // envelope ramps to 0 over ~3 ms across the remaining samples
                // of this chunk) and hold the position at the last frame so
                // the fade reads a stable value — no click.
                srcPos = (double)total - 1.0;
                InterlockedExchange(&g_audState.trigger_stop, 1);
                InterlockedExchange(&g_audState.playing, 0);
            }
        }
    }
    s_srcPos = srcPos;

    // Publish the playhead for the UI scrub head (audio thread is the only
    // writer of read_frame_idx).
    if ((ma_uint64)total > 0) {
        ma_uint64 rf = (ma_uint64)srcPos % (ma_uint64)total;
        InterlockedExchange(&g_audState.read_frame_idx, (LONG)rf);
    }
}
