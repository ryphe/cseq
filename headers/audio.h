#pragma once
#include "globals.h"
#include "dsp.h"
#include "granular.h"
#include "synth.h"
#include "state.h"
#include "fx.h"
#include <stdio.h>
#include <float.h>    
#include <malloc.h>   
#include <intrin.h>   

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
#include <immintrin.h>
#endif

 
#if defined(_MSC_VER)
#define CSEQ_TLS __declspec(thread)
#else
#define CSEQ_TLS __thread
#endif

static CSEQ_TLS int s_denormThreadReady = 0;

// Smallest power of two >= v. Used to size ring buffers so the hot-path
// wrap-around can use `& (size - 1)` instead of a runtime integer modulo.
static inline int next_pow2(int v) {
    if (v <= 1) return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

 
static inline void audio_enable_denormal_flush(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
     
    unsigned prevState = 0, unused = 0;
    if (_controlfp_s(&prevState, 0, 0) == 0)
        _controlfp_s(&unused, prevState | _DN_FLUSH, _MCW_DN);
#endif
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);    
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#else
     
#endif
}

 
static inline void audio_thread_init_denormals(void) {
    if (s_denormThreadReady) return;
    s_denormThreadReady = 1;
    audio_enable_denormal_flush();
}

 
static bool g_cpuHasAvx2 = false;

static inline void audio_detect_cpu_features(void) {
    int regs[4] = {0};
    __cpuid(regs, 0);
    if (regs[0] < 7) return;
    __cpuidex(regs, 7, 0);
    g_cpuHasAvx2 = (regs[1] & (1 << 5)) != 0;    
}

 
static inline void apply_lofi_sample(float* L, float* R,
                                     float* phase, float* holdL, float* holdR,
                                     float* lpL, float* lpR,
                                     bool enabled, int bitDepth, float sampleRate)
{
    if (!enabled) return;

    float step = sampleRate / (float)SAMPLE_RATE;
    if (step < 0.05f) step = 0.05f;
    if (step > 1.0f)  step = 1.0f;

    *phase += step;
    if (*phase >= 1.0f) {
        *phase -= floorf(*phase);

        int bits = bitDepth;
        if (bits < 8)  bits = 8;
        if (bits > 12) bits = 12;
        float levels = (float)(1 << (bits - 1));
        *holdL = floorf(*L * levels + 0.5f) / levels;
        *holdR = floorf(*R * levels + 0.5f) / levels;
    }
    *L = *holdL;
    *R = *holdR;

    *lpL += 0.45f * (*L - *lpL);
    *lpR += 0.45f * (*R - *lpR);
    *L = *lpL;
    *R = *lpR;
}

 
static inline float soft_clip_sample(float x) {
     
    return tanhf(x);
}

 
typedef struct {
    float *delayL, *delayR;
    int    delay_len;
    int    write_pos;
    float  threshold;
    float  attack_coef;
    float  release_coef;
    float  gain;
    float  env;
} StereoLimiter;

static inline void stereo_limiter_init(StereoLimiter *L, float sample_rate,
                                       float lookahead_ms, float release_ms,
                                       float threshold_linear) {
    L->threshold = threshold_linear;
    L->gain      = 1.0f;
    L->env       = 0.0f;
    L->write_pos = 0;

    L->delay_len = (int)(lookahead_ms * sample_rate * 0.001f);
    if (L->delay_len < 4) L->delay_len = 4;
    // Round up to a power of two so the per-sample ring wrap uses `& (len-1)`.
    L->delay_len = next_pow2(L->delay_len);
    // Reuse buffers if a pre-allocation pass already sized them (see
    // stereo_limiter_reserve); keeps malloc off the audio thread.
    if (!L->delayL || !L->delayR) {
        free(L->delayL); free(L->delayR);
        L->delayL = (float*)malloc(L->delay_len * sizeof(float));
        L->delayR = (float*)malloc(L->delay_len * sizeof(float));
    }
    if (L->delayL) memset(L->delayL, 0, L->delay_len * sizeof(float));
    if (L->delayR) memset(L->delayR, 0, L->delay_len * sizeof(float));

    float attack_time = lookahead_ms * 0.001f;
    L->attack_coef  = expf(-1.0f / (attack_time * sample_rate));
    L->release_coef = expf(-1.0f / (release_ms * 0.001f * sample_rate));
}

// Pre-allocate the lookahead delay lines outside the realtime path. Call this
// from the UI thread before the audio device starts; the first audio callback
// then only resets coefficients, never allocates.
static inline void stereo_limiter_reserve(StereoLimiter *L, float sample_rate,
                                          float lookahead_ms) {
    if (L->delayL && L->delayR) return;
    int len = (int)(lookahead_ms * sample_rate * 0.001f);
    if (len < 4) len = 4;
    // Round up so the per-sample ring wrap can mask; the buffer must match
    // delay_len (the process loop indexes up to delay_len-1).
    L->delay_len = next_pow2(len);
    L->delayL = (float*)malloc(L->delay_len * sizeof(float));
    L->delayR = (float*)malloc(L->delay_len * sizeof(float));
    if (L->delayL) memset(L->delayL, 0, L->delay_len * sizeof(float));
    if (L->delayR) memset(L->delayR, 0, L->delay_len * sizeof(float));
}

static inline void stereo_limiter_free(StereoLimiter *L) {
    if (L->delayL) { free(L->delayL); L->delayL = NULL; }
    if (L->delayR) { free(L->delayR); L->delayR = NULL; }
}

static inline void stereo_limiter_process(StereoLimiter *L, float *inL, float *inR) {
    float peakL = fabsf(*inL);
    float peakR = fabsf(*inR);
    float peak = (peakL > peakR) ? peakL : peakR;

    if (peak > L->env)
        L->env = L->attack_coef * L->env + (1.0f - L->attack_coef) * peak;
    else
        L->env = L->release_coef * L->env + (1.0f - L->release_coef) * peak;

    float target_gain = 1.0f;
    if (L->env > L->threshold && L->env > 1e-9f)
        target_gain = L->threshold / L->env;

    if (target_gain < L->gain)
        L->gain = L->attack_coef * L->gain + (1.0f - L->attack_coef) * target_gain;
    else
        L->gain = L->release_coef * L->gain + (1.0f - L->release_coef) * target_gain;

    L->delayL[L->write_pos] = *inL;
    L->delayR[L->write_pos] = *inR;
    // delay_len is a power of two (see next_pow2 in init/reserve), so wrap
    // with a mask instead of a runtime integer modulo.
    int read_pos = (L->write_pos + 1) & (L->delay_len - 1);
    L->write_pos = (L->write_pos + 1) & (L->delay_len - 1);

    *inL = L->delayL[read_pos] * L->gain;
    *inR = L->delayR[read_pos] * L->gain;

    if (*inL >  1.0f) *inL =  1.0f;
    if (*inL < -1.0f) *inL = -1.0f;
    if (*inR >  1.0f) *inR =  1.0f;
    if (*inR < -1.0f) *inR = -1.0f;
}

 
static inline float midi_adsr_level_at(double posInNote, double noteFrames,
                                       float atkFrames, float decFrames, float sustain) {
    (void)noteFrames;
    // Note-on envelope level (A -> D -> S) at a given position inside the note.
    if (atkFrames > 0.0f && posInNote < (double)atkFrames) {
        float x = (float)(posInNote / (double)atkFrames);
        if (x > 1.0f) x = 1.0f;
        return x;
    }
    double decStart = (double)atkFrames;
    double decEnd = decStart + (double)decFrames;
    if (decFrames > 0.0f && posInNote < decEnd) {
        float x = (float)((posInNote - decStart) / (double)decFrames);
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return 1.0f + (sustain - 1.0f) * x;
    }
    return sustain;
}

// Sampler-style envelope: A/D/S run inside the written note length, then the
// release ramps from the level at note-off to zero OVER relFrames past the
// note end (positions beyond noteFrames are the release tail).
static inline float midi_adsr_gain(double posInNote, double noteFrames,
                                   float atkFrames, float decFrames, float sustain,
                                   float relFrames) {
    if (noteFrames <= 0.0) return 0.0f;
    if (posInNote < 0.0) posInNote = 0.0;

    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;

    if (posInNote < noteFrames)
        return midi_adsr_level_at(posInNote, noteFrames, atkFrames, decFrames, sustain);

    // Release tail: starts from whatever level the envelope reached at note-off.
    if (relFrames <= 0.0f) return 0.0f;
    double relPos = posInNote - noteFrames;
    if (relPos >= (double)relFrames) return 0.0f;
    float levelAtOff = midi_adsr_level_at(noteFrames, noteFrames, atkFrames, decFrames, sustain);
    float x = (float)(relPos / (double)relFrames);
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return levelAtOff * (1.0f - x);
}

 
static inline void midi_process_clip_frames(const Clip* c, float* L, float* R, ma_uint32 n,
                                            ma_uint64 startGlobalFrame, float bpm, float swing, float fpb,
                                            const AudioSample* samples, int sampleCount,
                                            int clipIdx,
                                            const float* trackTriggerProb, uint32_t* trackRngState,
                                            uint8_t* midiNoteArmed, uint8_t* midiNoteSkipped) {
    (void)bpm; (void)swing;
    if (!c || !c->isMidi || c->midiNoteCount <= 0 || n == 0) return;

    // Feature 1: per-track trigger probability + RNG. prob is read once per
    // clip; the per-note armed/skipped edge state is indexed by clip and note.
    float prob = 1.0f;
    uint32_t* rng = NULL;
    if (c->track >= 0 && c->track < MAX_TRACKS) {
        if (trackTriggerProb) prob = trackTriggerProb[c->track];
        if (trackRngState)    rng  = &trackRngState[c->track];
    }
    const bool hasProbState = (midiNoteArmed && midiNoteSkipped && rng);

    // Resolve the clip's sample source:
    //  - A regular sample file attached to the clip, or
    //  - The pre-rendered SoundFont note bank (if no sample is attached).
    // With SoundFont, each active note may resolve to a different pre-rendered
    // note; the existing per-voice ADSR and the track mixer apply uniformly,
    // so SoundFont notes inherit track volume/pan/EQ/FX exactly like samples.
    const bool useSfont = sfont_is_ready();
    const AudioSample* s = NULL;
    if (c->sampleIndex >= 0 && c->sampleIndex < sampleCount && samples) {
        const AudioSample* ps = &samples[c->sampleIndex];
        if (ps->loaded && ps->pFrames && ps->frameCount >= 2) s = ps;
    }
    if (!s && !useSfont) return;

    const float swungStart = apply_clip_swing(c->startBeat, swing);
    const float swungEnd   = swungStart + c->lengthBeats;
    // Release tails are rendered past the note end (and past the clip end),
    // so widen the processing window by the release length up front.
    const float relFramesPre = (c->adsrRelease > 0.0f) ? c->adsrRelease * 0.001f * (float)SAMPLE_RATE : 0.0f;
    const float relBeats = (fpb > 0.0f) ? relFramesPre / fpb : 0.0f;
    const float tailEnd = swungEnd + relBeats;

    float startLinearBeat = (float)((double)startGlobalFrame / (double)fpb);
    float endLinearBeat   = (float)((double)(startGlobalFrame + n) / (double)fpb);
    if (endLinearBeat <= swungStart || startLinearBeat >= tailEnd) return;

    float chunkStartLocal = startLinearBeat - swungStart;
    float chunkEndLocal   = endLinearBeat - swungStart;

    typedef struct {
        float startBeat;
        float lengthBeats;
        float rate;
        float gain;
        const AudioSample* sample;
        int   noteIndex;         // index into c->midiNotes (for prob edge state)
    } ActiveMidiVoice;

    ActiveMidiVoice voices[MIDI_MAX_VOICES];
    int voiceCount = 0;

    for (int i = 0; i < c->midiNoteCount && voiceCount < MIDI_MAX_VOICES; ++i) {
        const MidiNote* nt = &c->midiNotes[i];
        if (!nt->active) continue;

        // Swing the note's own position so off-beat eighths shift like the
        // clip grid. The swung start is used for the note's window and its
        // playback position; length stays raw so the duration is unchanged.
        float swungNoteStart = apply_note_clip_swing(nt->startBeat, swing);
        float ntEnd = swungNoteStart + nt->lengthBeats;
        // Include notes whose release tail is still ringing in this chunk.
        if (ntEnd + relBeats <= chunkStartLocal || swungNoteStart >= chunkEndLocal) continue;

        // Resolve the per-note sample and pitch rate. A regular sample file is
        // pitched relative to MIDI 60; a pre-rendered SoundFont note is already
        // at its exact pitch (rate 1.0) unless we fell back to a neighboring
        // key to fill a gap in the SF2 key map, in which case pitch-shift it.
        const AudioSample* vs = s;
        float rate;
        if (s) {
            rate = powf(2.0f, (float)(nt->pitch - 60) / 12.0f);
        } else {
            int foundPitch;
            vs = sfont_get_sample_nearest(nt->pitch, &foundPitch);
            if (!vs) continue;
            rate = (foundPitch == nt->pitch)
                 ? 1.0f : powf(2.0f, (float)(nt->pitch - foundPitch) / 12.0f);
        }
        if (rate < 0.01f) rate = 0.01f;
        if (rate > 16.0f) rate = 16.0f;

         
        float vel = (nt->velocity > 1.0f) ? (nt->velocity / 100.0f) : nt->velocity;
        if (vel < 0.0f) vel = 0.0f;
        if (vel > 1.0f) vel = 1.0f;

        voices[voiceCount].startBeat   = swungNoteStart;
        voices[voiceCount].lengthBeats = nt->lengthBeats;
        voices[voiceCount].rate        = rate;
        voices[voiceCount].gain        = midi_velocity_gain(vel) * c->volume;
        voices[voiceCount].sample      = vs;
        voices[voiceCount].noteIndex   = i;
        voiceCount++;
    }

    if (voiceCount == 0) return;

    const float atkFrames = (c->adsrAttack > 0.0f) ? c->adsrAttack * 0.001f * (float)SAMPLE_RATE : 0.0f;
    const float decFrames = (c->adsrDecay  > 0.0f) ? c->adsrDecay  * 0.001f * (float)SAMPLE_RATE : 0.0f;
    const float relFrames = (c->adsrRelease > 0.0f) ? c->adsrRelease * 0.001f * (float)SAMPLE_RATE : 0.0f;
    float sustain = c->adsrSustain;
    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;

    for (ma_uint32 f = 0; f < n; ++f) {
        float linearBeat = (float)((double)(startGlobalFrame + f) / (double)fpb);
        float localBeat = linearBeat - swungStart;
        if (localBeat < 0.0f || localBeat >= c->lengthBeats + relBeats) continue;

        double clipSamplePos = (double)localBeat * (double)fpb;
        double clipTotalSamples = (double)c->lengthBeats * (double)fpb;
        float clipMicroFade = 1.0f;
        // Micro-fades only apply inside the clip proper; a release tail that
        // rings past the clip end must not be faded back in at the boundary.
        if (localBeat < c->lengthBeats) {
            if (clipSamplePos < (double)FADE_SAMPLES) {
                clipMicroFade = (float)(clipSamplePos / (double)FADE_SAMPLES);
            } else if ((clipTotalSamples - clipSamplePos) < (double)FADE_SAMPLES) {
                clipMicroFade = (float)((clipTotalSamples - clipSamplePos) / (double)FADE_SAMPLES);
            }
        }
        if (clipMicroFade < 0.0f) clipMicroFade = 0.0f;
        if (clipMicroFade > 1.0f) clipMicroFade = 1.0f;

        for (int v = 0; v < voiceCount; ++v) {
            float vStart = voices[v].startBeat;
            float vLen   = voices[v].lengthBeats;
            // Note body plus its release tail.
            if (localBeat < vStart || localBeat >= vStart + vLen + relBeats) continue;

            // Feature 1: roll the per-track probability once at the note-on
            // edge (body entered) and hold it through body+tail. A skipped
            // note stays silent for its whole window, then re-arms. The edge
            // state is per-clip/per-note in the RenderContext (0 = armed).
            if (hasProbState && clipIdx >= 0 && clipIdx < MAX_CLIPS) {
                uint8_t* armed   = &midiNoteArmed[clipIdx * MIDI_MAX_NOTES + voices[v].noteIndex];
                uint8_t* skipped = &midiNoteSkipped[clipIdx * MIDI_MAX_NOTES + voices[v].noteIndex];
                bool inBody = (localBeat >= vStart && localBeat < vStart + vLen);
                if (inBody) {
                    if (*armed == 0) {
                        *armed   = 1;
                        *skipped = track_roll_probability(rng, prob) ? 0 : 1;
                    }
                    if (*skipped) continue;
                } else {
                    *armed = 0;   // release tail: re-arm once the body has ended
                }
            }

            float noteLocal = localBeat - vStart;
            double srcPos = (double)noteLocal * (double)fpb * (double)voices[v].rate;
            ma_uint64 i0 = (ma_uint64)srcPos;
            const AudioSample* vs = voices[v].sample;
            if (i0 + 1 >= vs->frameCount) continue;
            ma_uint64 i1 = i0 + 1;

            float frac = (float)(srcPos - (double)i0);
            float sl = vs->pFrames[i0 * 2 + 0] + frac * (vs->pFrames[i1 * 2 + 0] - vs->pFrames[i0 * 2 + 0]);
            float sr = vs->pFrames[i0 * 2 + 1] + frac * (vs->pFrames[i1 * 2 + 1] - vs->pFrames[i0 * 2 + 1]);

            double noteFrames = (double)vLen * (double)fpb;
            double posInNote  = (double)noteLocal * (double)fpb;


            float env = midi_adsr_gain(posInNote, noteFrames, atkFrames, decFrames, sustain, relFrames);

             
            float edgeFade = 1.0f;
            double framesLeft = (double)vs->frameCount - srcPos;
            if (framesLeft < 64.0) edgeFade = (float)(framesLeft / 64.0);
            if (edgeFade < 0.0f) edgeFade = 0.0f;

            float finalGain = env * voices[v].gain * edgeFade * clipMicroFade;
            L[f] += sl * finalGain;
            R[f] += sr * finalGain;
        }
    }
}

// Polyphonic sample / SoundFont audition slots (audio-thread local)
typedef struct {
    int    key;
    double pos;       // sample read position
    double envFrame;  // frames since note-on (advances through the tail)
    double envLen;    // envelope length: kHeldLen while held, frozen at release
    const AudioSample* sample;
    bool   sfont;     // pre-rendered SoundFont note (rate 1.0) vs clip sample
} AudSlot;

static AudSlot s_aud[8] = { {0} };
static const double kHeldLen = 1.0e12;

static inline bool midi_audition_has_ringing(void) {
    for (int sl = 0; sl < 8; ++sl) {
        if (s_aud[sl].key > 0) return true;
    }
    return false;
}

static inline void midi_editor_process_preview(float* L, float* R, ma_uint32 frameCount, float bpm, float fpb) {
    (void)bpm;
    midi_lock();
    if (g_midiEdit.clipIdx < 0 || g_midiEdit.clipIdx >= g_Seq.clipCount) { midi_unlock(); return; }
    const Clip* c = &g_Seq.clips[g_midiEdit.clipIdx];
    if (!c->isMidi) { midi_unlock(); return; }

    const bool isSynthKind = (c->clipKind == CLIP_KIND_QUADRUM || c->clipKind == CLIP_KIND_HALO);
    if (isSynthKind) {
        const bool timelineRendering = seq_is_playing() || granular_is_active();
        if (timelineRendering) {
            midi_unlock();
            return;
        }
        int heldKeys[8];
        int heldKeyCount = 0;
        for (int i = 0; i < 8 && i < g_midiEdit.auditionNoteCount; ++i) {
            heldKeys[i] = g_midiEdit.auditionNotes[i];
            heldKeyCount = i + 1;
        }
        const bool   playLoop  = g_midiEdit.isAuditionPlaying && c->lengthBeats > 0.01f;
        const int    clipIdx   = g_midiEdit.clipIdx;
        const double invFpbP   = 1.0 / (double)(fpb > 0.0 ? fpb : 1.0);
        float localBeat = (float)g_midiEdit.auditionPlayheadBeat;
        midi_unlock();

        int prevTrack = c->track;
        float prevPan = (prevTrack >= 0 && prevTrack < MAX_TRACKS) ? g_Seq.trackPan[prevTrack] : 0.0f;
        if (prevPan < -1.0f) prevPan = -1.0f;
        if (prevPan >  1.0f) prevPan =  1.0f;
        float panL = (prevPan <= 0.0f) ? 1.0f : (1.0f - prevPan);
        float panR = (prevPan >= 0.0f) ? 1.0f : (1.0f + prevPan);

        for (ma_uint32 f = 0; f < frameCount; ++f) {
            float sumL = 0.0f, sumR = 0.0f;
            synth_editor_process_preview(c, clipIdx, &sumL, &sumR,
                                         heldKeys, heldKeyCount, localBeat, playLoop, fpb);
            L[f] += sumL * panL;
            R[f] += sumR * panR;
            if (playLoop) {
                localBeat += (float)invFpbP;
                if (localBeat >= c->lengthBeats) localBeat -= c->lengthBeats;
            }
        }

        if (playLoop) {
            midi_lock();
            g_midiEdit.auditionPlayheadBeat = (double)localBeat;
            midi_unlock();
        }
        return;
    }

    const bool playLoop = g_midiEdit.isAuditionPlaying && c->lengthBeats > 0.01f;
    int heldKeys[8];
    int heldKeyCount = 0;
    for (int i = 0; i < 8 && i < g_midiEdit.auditionNoteCount; ++i) {
        heldKeys[i] = g_midiEdit.auditionNotes[i];
        heldKeyCount = i + 1;
    }
    const bool audHeld = (heldKeyCount > 0);

    // Keep processing chunk if any audition slot is still in its release tail
    if (!playLoop && !audHeld && !midi_audition_has_ringing()) { midi_unlock(); return; }

    const double invFpb = 1.0 / (double)fpb;
    const float  swing  = g_Seq.swing;

    const AudioSample* s = NULL;
    if (c->sampleIndex >= 0 && c->sampleIndex < g_Seq.sampleCount) {
        const AudioSample* ps = &g_Seq.samples[c->sampleIndex];
        if (ps->loaded && ps->pFrames && ps->frameCount >= 2) s = ps;
    }
    const bool useSfont = sfont_is_ready();

    float atkMs = (c->adsrAttack > 0.0f) ? c->adsrAttack : 2.0f;
    float relMs = (c->adsrRelease > 0.0f) ? c->adsrRelease : 2.0f;
    const float atkFrames = atkMs * 0.001f * (float)SAMPLE_RATE;
    const float decFrames = (c->adsrDecay > 0.0f) ? c->adsrDecay * 0.001f * (float)SAMPLE_RATE : 0.0f;
    const float relFrames = relMs * 0.001f * (float)SAMPLE_RATE;
    float sustain = c->adsrSustain;
    if (sustain < 0.0f) sustain = 0.0f;
    if (sustain > 1.0f) sustain = 1.0f;

    int prevTrack = c->track;
    float prevPan = (prevTrack >= 0 && prevTrack < MAX_TRACKS) ? g_Seq.trackPan[prevTrack] : 0.0f;
    if (prevPan < -1.0f) prevPan = -1.0f;
    if (prevPan >  1.0f) prevPan =  1.0f;
    float prevPanL = (prevPan <= 0.0f) ? 1.0f : (1.0f - prevPan);
    float prevPanR = (prevPan >= 0.0f) ? 1.0f : (1.0f + prevPan);

    for (ma_uint32 f = 0; f < frameCount; ++f) {
        float sumL = 0.0f, sumR = 0.0f;

        // Freeze envLen ONLY once when key transition from held -> released
        for (int sl = 0; sl < 8; ++sl) {
            if (s_aud[sl].key <= 0) continue;
            bool still = false;
            for (int h = 0; h < heldKeyCount; ++h) {
                if (heldKeys[h] == s_aud[sl].key) { still = true; break; }
            }
            if (!still && s_aud[sl].envLen >= kHeldLen * 0.5) {
                s_aud[sl].envLen = s_aud[sl].envFrame;
            }
        }

        // Allocate slots for freshly pressed keys; re-arm any decaying tails on the same note
        for (int h = 0; h < heldKeyCount; ++h) {
            int key = heldKeys[h];
            if (key <= 0) continue;
            bool has = false;
            for (int sl = 0; sl < 8; ++sl) {
                if (s_aud[sl].key == key) {
                    if (s_aud[sl].envLen >= kHeldLen * 0.5) { has = true; break; }
                    s_aud[sl].key = 0; // Release tail in progress: re-arm slot for clean retrigger
                }
            }
            if (has) continue;
            for (int sl = 0; sl < 8; ++sl) {
                if (s_aud[sl].key > 0) continue;
                const AudioSample* ks = s;
                bool ksFont = false;
                if (!ks && useSfont) {
                    ks = sfont_get_sample(key);
                    ksFont = (ks != NULL);
                }
                if (ks) {
                    s_aud[sl].key      = key;
                    s_aud[sl].pos      = 0.0;
                    s_aud[sl].envFrame = 0.0;
                    s_aud[sl].envLen   = kHeldLen;
                    s_aud[sl].sample   = ks;
                    s_aud[sl].sfont    = ksFont;
                }
                break;
            }
        }

        // Render active slots
        for (int sl = 0; sl < 8; ++sl) {
            if (s_aud[sl].key <= 0) continue;
            AudSlot* au = &s_aud[sl];

            float env = midi_adsr_gain(au->envFrame, au->envLen,
                                       atkFrames, decFrames, sustain, relFrames);
            au->envFrame += 1.0;
            if (env <= 0.0f) {
                bool still = false;
                for (int h = 0; h < heldKeyCount; ++h) {
                    if (heldKeys[h] == au->key) { still = true; break; }
                }
                if (!still) { au->key = 0; au->pos = 0.0; }
                continue;
            }

            double rate = au->sfont ? 1.0 : powf(2.0f, ((float)au->key - 60.0f) / 12.0f);
            if (rate < 0.01) rate = 0.01;
            if (rate > 16.0) rate = 16.0;

            const AudioSample* as = au->sample;
            if (!as || !as->pFrames) continue;
            au->pos += rate;

            // One-shot playback: silence when sample finishes; do NOT loop
            if (au->pos >= (double)as->frameCount) {
                bool still = false;
                for (int h = 0; h < heldKeyCount; ++h) {
                    if (heldKeys[h] == au->key) { still = true; break; }
                }
                if (!still) { au->key = 0; au->pos = 0.0; }
                continue;
            }

            ma_uint64 i0 = (ma_uint64)au->pos;
            ma_uint64 i1 = (i0 + 1 < as->frameCount) ? (i0 + 1) : i0;
            float frac = (float)(au->pos - (double)i0);
            float slL = as->pFrames[i0 * 2 + 0] + frac * (as->pFrames[i1 * 2 + 0] - as->pFrames[i0 * 2 + 0]);
            float slR = as->pFrames[i1 * 2 + 1] + frac * (as->pFrames[i1 * 2 + 1] - as->pFrames[i0 * 2 + 1]);

            float edgeFade = 1.0f;
            double framesLeft = (double)as->frameCount - au->pos;
            if (framesLeft < 64.0) edgeFade = (float)(framesLeft / 64.0);
            if (edgeFade < 0.0f) edgeFade = 0.0f;
            float gain = 0.55f * env * edgeFade;
            sumL += slL * gain * prevPanL;
            sumR += slR * gain * prevPanR;
        }

        if (playLoop) {
            float beat = (float)g_midiEdit.auditionPlayheadBeat;

            typedef struct {
                double startBeat, lengthBeats;
                double relUntil;
                float  levelAtOff;
                float  rate;
                double srcPos;
                int    pitch;
                float  vel;
                const AudioSample* sample;
                double synthPhase;
                float  synthLp;
                bool   envDead;
            } PrevVoice;
            static PrevVoice s_ring[64];
            static int       s_ringCount = 0;
            static double    s_lastPlayhead = -1.0;

            if (s_lastPlayhead >= 0.0 && beat < s_lastPlayhead) s_ringCount = 0;
            s_lastPlayhead = beat;

            for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
                const MidiNote* nt = &c->midiNotes[i];
                if (!nt->active) continue;
                float sNote = apply_note_clip_swing(nt->startBeat, swing);
                if (beat < sNote || beat >= sNote + nt->lengthBeats) continue;

                bool already = false;
                for (int v = 0; v < s_ringCount; ++v) {
                    if (s_ring[v].pitch == nt->pitch &&
                        fabs(s_ring[v].startBeat - (double)sNote) < 1e-4) { already = true; break; }
                }
                if (already) continue;
                if (s_ringCount < 64) {
                    PrevVoice* v = &s_ring[s_ringCount++];
                    v->startBeat   = (double)sNote;
                    v->lengthBeats = nt->lengthBeats;
                    double relBeats = (fpb > 0.0) ? (double)relFrames / (double)fpb : 0.0;
                    v->relUntil    = (double)sNote + (double)nt->lengthBeats + relBeats;
                    v->levelAtOff  = midi_adsr_level_at((double)nt->lengthBeats * (double)fpb,
                                                       (double)nt->lengthBeats * (double)fpb,
                                                       atkFrames, decFrames, sustain);
                    const AudioSample* vs = s;
                    float noteRate;
                    if (s) {
                        noteRate = (float)pow(2.0, (nt->pitch - 60.0) / 12.0);
                    } else if (useSfont) {
                        int foundPitch;
                        vs = sfont_get_sample_nearest(nt->pitch, &foundPitch);
                        if (!vs) continue;
                        noteRate = (foundPitch == nt->pitch)
                                 ? 1.0f : (float)pow(2.0, (nt->pitch - foundPitch) / 12.0);
                    } else {
                        continue;
                    }
                    if (noteRate < 0.01f) noteRate = 0.01f;
                    if (noteRate > 16.0f) noteRate = 16.0f;
                    v->rate       = noteRate;
                    v->sample     = vs;
                    v->srcPos     = 0.0;
                    v->pitch      = nt->pitch;
                    v->synthPhase = 0.0;
                    v->synthLp    = 0.0f;
                    v->envDead    = false;
                    float vel     = (nt->velocity > 1.0f) ? (nt->velocity / 100.0f) : nt->velocity;
                    if (vel < 0.0f) vel = 0.0f;
                    if (vel > 1.0f) vel = 1.0f;
                    v->vel        = vel;
                }
            }

            for (int v = 0; v < s_ringCount; ++v) {
                PrevVoice* pv = &s_ring[v];
                bool inBody = (beat >= pv->startBeat && beat < pv->startBeat + pv->lengthBeats);
                bool inTail = (beat >= pv->startBeat + pv->lengthBeats && beat < pv->relUntil);
                if (!inBody && !inTail) continue;

                double noteFrames = pv->lengthBeats * (double)fpb;
                double posInNote  = (beat - pv->startBeat) * (double)fpb;
                float env;
                if (inBody) {
                    env = midi_adsr_level_at(posInNote, noteFrames, atkFrames, decFrames, sustain);
                } else if (relFrames > 0.0f) {
                    env = pv->levelAtOff * (float)(1.0 - (posInNote - noteFrames) / (double)relFrames);
                } else {
                    env = 0.0f;
                }

                if (env < 0.0f) env = 0.0f;
                if (env > 1.0f) env = 1.0f;

                float sl, sr;
                const AudioSample* vs = pv->sample;
                if (vs) {
                    if (!pv->envDead) pv->srcPos += (double)pv->rate;
                    if (pv->srcPos >= (double)vs->frameCount) { pv->envDead = true; continue; }
                    ma_uint64 i0 = (ma_uint64)pv->srcPos;
                    ma_uint64 i1 = (i0 + 1 < vs->frameCount) ? (i0 + 1) : i0;
                    float frac = (float)(pv->srcPos - (double)i0);
                    sl = vs->pFrames[i0 * 2 + 0] + frac * (vs->pFrames[i1 * 2 + 0] - vs->pFrames[i0 * 2 + 0]);
                    sr = vs->pFrames[i1 * 2 + 1] + frac * (vs->pFrames[i1 * 2 + 1] - vs->pFrames[i0 * 2 + 1]);

                    float edgeFade = 1.0f;
                    double framesLeft = (double)vs->frameCount - pv->srcPos;
                    if (framesLeft < 64.0) edgeFade = (float)(framesLeft / 64.0);
                    if (edgeFade < 0.0f) edgeFade = 0.0f;

                    float gain = env * midi_velocity_gain(pv->vel) * 0.55f * edgeFade;
                    sumL += sl * gain * prevPanL;
                    sumR += sr * gain * prevPanR;
                } else {
                    double freq = 440.0 * (float)pow(2.0, (pv->pitch - 60.0) / 12.0);
                    pv->synthPhase += freq / (double)SAMPLE_RATE;
                    if (pv->synthPhase >= 1.0) pv->synthPhase -= floor(pv->synthPhase);
                    float tone = 0.30f * (float)(2.0 * pv->synthPhase - 1.0);
                    pv->synthLp += 0.35f * (tone - pv->synthLp);
                    float gain = env * midi_velocity_gain(pv->vel) * 0.55f;
                    sumL += pv->synthLp * gain * prevPanL;
                    sumR += pv->synthLp * gain * prevPanR;
                }
            }

            int w2 = 0;
            for (int v = 0; v < s_ringCount; ++v) {
                bool done = beat >= s_ring[v].relUntil || (s_ring[v].envDead && beat >= s_ring[v].startBeat + s_ring[v].lengthBeats);
                if (!done) s_ring[w2++] = s_ring[v];
            }
            s_ringCount = w2;

            g_midiEdit.auditionPlayheadBeat += invFpb;
            if (g_midiEdit.auditionPlayheadBeat >= (double)c->lengthBeats)
                g_midiEdit.auditionPlayheadBeat -= floor(g_midiEdit.auditionPlayheadBeat / (double)c->lengthBeats) * (double)c->lengthBeats;
        }

        L[f] += sumL;
        R[f] += sumR;
    }

    midi_unlock();
}
 
typedef struct {
    
    const Clip*         clips;
    int                 clipCount;
    int                 trackCount;
     
    const AudioSample*  samples;
    int                 sampleCount;
    const bool*         trackMuted;
    const bool*         trackSolo;
    const float*        trackVolume;
    const float*        trackPan;
    const float*        trackWidth;
    const bool*         trackEqActive;

    // Per-track trigger probability + its xorshift32 RNG state (Feature 1).
    // Transient per-clip/per-note edge state lives in the caller-provided
    // arrays below (zeroed = "armed"); see render_frames / midi path.
    const float*  trackTriggerProb;
    uint32_t*     trackRngState;
    uint8_t*      clipTrigState;     // MAX_CLIPS: 0=armed,1=play,2=skip
    uint8_t*      midiNoteArmed;     // MAX_CLIPS*MIDI_MAX_NOTES: 0=armed,1=rolled
    uint8_t*      midiNoteSkipped;   // MAX_CLIPS*MIDI_MAX_NOTES: 0=ok,1=skip

    
    SmoothEQ3*          trackEQ;
    PeakBiquad          (*trackPeak)[3];
    TrackFilter*        trackFilter;
    GranularEngine*     clipGran;
    GranularEngine*     trackGran;
    SynthHaloState*     clipHalo;
    SynthQuadrumState*  clipQuadrum;
    FxChain*            trackFx;           
    StereoLimiter*      limiter;

    
    bool                isLofi;
    int                 lofiBitDepth;
    float               lofiSampleRate;
    float               masterVolume;
    int                 masterMode;

    
    float*              lofiPhase;
    float*              lofiHoldL;
    float*              lofiHoldR;
    float*              lofiLpL;
    float*              lofiLpR;

    
    float               bpm;
    float               swing;
    float               fpb;
    float               totalBeats;
    bool                applyMaster;
    bool                soloActive;

    
    TrackMask128        trackActiveMask;     
    TrackMask128        trackPlayableMask;   
} RenderContext;

typedef struct {
    int                 clipCount;
    Clip                clips[MAX_CLIPS];
    GranClipSnapshot    clipGran[MAX_CLIPS];
    SynthHaloState      clipHalo[MAX_CLIPS];
    SynthQuadrumState   clipQuadrum[MAX_CLIPS];

     
    AudioSample         samples[MAX_SAMPLES];
    int                 sampleCount;

    int                 trackCount;
    bool                trackMuted[MAX_TRACKS];
    bool                trackSolo[MAX_TRACKS];
    float               trackVolume[MAX_TRACKS];
    float               trackPan[MAX_TRACKS];
    float               trackWidth[MAX_TRACKS];
    bool                trackEqActive[MAX_TRACKS];
    float               trackTriggerProb[MAX_TRACKS];
    uint32_t            trackRngState[MAX_TRACKS];
    SmoothEQ3           trackEQ[MAX_TRACKS];
    PeakBiquad          trackPeak[MAX_TRACKS][3];
    TrackFilter         trackFilter[MAX_TRACKS];
    GranClipSnapshot    trackGran[MAX_TRACKS];
    FxChain             trackFx[MAX_TRACKS];    

    float               bpm;
    float               swing;
    int                 visibleBarCount;    
    int                 timeSigNum;
    int                 timeSigDen;

    bool                isLofi;
    int                 lofiBitDepth;
    float               lofiSampleRate;

    int                 masterVolume;
    int                 masterMode;
    int                 exportBitDepth;

     
    TrackMask128        soloMask;
    TrackMask128        muteMask;
    TrackMask128        hasAudioMask;
    TrackMask128        activeMask;
    TrackMask128        playableMask;
} ExportSnapshot;

 

__declspec(noinline) static ma_uint32 render_frames(
    float* outL, float* outR, ma_uint32 frames,
    ma_uint64 startGlobalFrame,
    const RenderContext* ctx)
{
    if (frames == 0 || !ctx) return 0;

    float trackL[MAX_TRACKS], trackR[MAX_TRACKS];
    const ma_uint64 loopTotalFrames = (ma_uint64)((double)ctx->totalBeats * (double)ctx->fpb);
    const int nTracks = (ctx->trackCount < MAX_TRACKS) ? ctx->trackCount : MAX_TRACKS;

     
    float gLl[MAX_TRACKS], gLr[MAX_TRACKS], gRl[MAX_TRACKS], gRr[MAX_TRACKS];
    for (int t = 0; t < nTracks; ++t) {
        float w = (ctx->trackWidth[t] >= 0.0f) ? ctx->trackWidth[t] : 1.0f;
        float pan = ctx->trackPan[t];
        if (pan < -1.0f) pan = -1.0f;
        if (pan >  1.0f) pan =  1.0f;
        float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
        float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);
        float vol = ctx->trackVolume[t];
         
        if (fabsf(vol) < 1e-30f) vol = 0.0f;
        float v;
        v = vol * (0.5f + 0.5f * w) * panL; gLl[t] = (v > 0.0f) ? v : 0.0f;
        v = vol * (0.5f - 0.5f * w) * panL; gLr[t] = (v > 0.0f) ? v : 0.0f;
        v = vol * (0.5f - 0.5f * w) * panR; gRl[t] = (v > 0.0f) ? v : 0.0f;
        v = vol * (0.5f + 0.5f * w) * panR; gRr[t] = (v > 0.0f) ? v : 0.0f;
    }

    memset(outL, 0, frames * sizeof(float));
    memset(outR, 0, frames * sizeof(float));

     
    ma_uint32 denormTick = 0;
    for (ma_uint32 f = 0; f < frames; ++f) {
        ma_uint64 currentFrame = (loopTotalFrames > 0)
            ? ((startGlobalFrame + f) % loopTotalFrames)
            : 0;
        float linearBeat = frame_to_beat(currentFrame, ctx->bpm, 0.0f);

        memset(trackL, 0, sizeof(trackL));
        memset(trackR, 0, sizeof(trackR));

         
        for (int i = 0; i < ctx->clipCount; ++i) {
            const Clip* c = &ctx->clips[i];
            if (c->track < 0 || c->track >= ctx->trackCount) continue;
            if (!track_mask_test(&ctx->trackActiveMask, c->track)) continue;
            if (!ctx->soloActive && c->isMuted) continue;
            if (c->startBeat >= ctx->totalBeats) continue;

            if (c->clipKind == CLIP_KIND_QUADRUM || c->clipKind == CLIP_KIND_HALO) {
                float prob = 1.0f;
                uint32_t* rng = NULL;
                if (c->track >= 0 && c->track < ctx->trackCount) {
                    if (ctx->trackTriggerProb) prob = ctx->trackTriggerProb[c->track];
                    if (ctx->trackRngState)    rng  = &ctx->trackRngState[c->track];
                }
                synth_clip_process_frames(c, ctx->clipHalo ? &ctx->clipHalo[i] : NULL,
                                          ctx->clipQuadrum ? &ctx->clipQuadrum[i] : NULL,
                                          &trackL[c->track], &trackR[c->track],
                                          1, currentFrame, ctx->bpm, ctx->swing, ctx->fpb,
                                          prob, rng);
                continue;
            }
            if (c->isMidi) {
                midi_process_clip_frames(c, &trackL[c->track], &trackR[c->track],
                                         1, currentFrame, ctx->bpm, ctx->swing, ctx->fpb,
                                         ctx->samples, ctx->sampleCount, i,
                                         ctx->trackTriggerProb, ctx->trackRngState,
                                         ctx->midiNoteArmed, ctx->midiNoteSkipped);
                continue;
            }
            if (c->isGranular && ctx->clipGran) {
                granular_process_clip_ptr(ctx->clipGran, ctx->clips, i,
                                          &trackL[c->track], &trackR[c->track],
                                          1, ctx->bpm, ctx->swing, currentFrame);
                continue;
            }

            
            if (!ctx->samples || c->sampleIndex < 0 || c->sampleIndex >= ctx->sampleCount) continue;
            const AudioSample* s = &ctx->samples[c->sampleIndex];
            if (!s->loaded || !s->pFrames || s->frameCount == 0) continue;

            float pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;

            // Loop only when the clip length is at least twice the FULL sample
            // length. The decision is independent of the alt-slip offset so
            // slipping never flips a clip between looping and not, and a
            // looping clip always repeats the whole sample from its true start.
            const double sampleLen = (double)s->frameCount;
            const bool loop = (sampleLen > 0.0) &&
                ((double)c->lengthBeats * (double)ctx->fpb * (double)pRate >= sampleLen * 2.0);

            float swungStart = apply_clip_swing(c->startBeat, ctx->swing);
            float swungEnd = swungStart + c->lengthBeats;

            // Feature 1: per-track trigger probability. A sample clip streams
            // continuously, so the roll happens ONCE at window entry (armed ->
            // decided) and is held for the whole window; otherwise a skipped
            // clip would stutter on/off every frame. The per-clip edge state
            // lives in the RenderContext (0 = armed), zeroed on a fresh play.
            uint8_t* tstate = (ctx->clipTrigState) ? &ctx->clipTrigState[i] : NULL;
            if (tstate) {
                bool inWindow = (linearBeat >= swungStart && linearBeat < swungEnd);
                if (inWindow) {
                    if (*tstate == 0) {
                        float prob = 1.0f;
                        uint32_t* rng = NULL;
                        if (c->track >= 0 && c->track < ctx->trackCount) {
                            if (ctx->trackTriggerProb) prob = ctx->trackTriggerProb[c->track];
                            if (ctx->trackRngState)    rng  = &ctx->trackRngState[c->track];
                        }
                        *tstate = (rng && track_roll_probability(rng, prob)) ? 1 : 2;
                    }
                    if (*tstate == 2) continue;   // skipped: silence this window
                } else {
                    *tstate = 0;                   // re-arm once the window ends
                }
            }

            if (linearBeat < swungStart || linearBeat >= swungEnd) continue;

            float localBeat = linearBeat - swungStart;

            // Compute the absolute read position (in sample frames) from the
            // start of the sample. This is the total elapsed frames for the
            // clip, offset by the sample's playback start.
            double posTotal = (double)localBeat * (double)ctx->fpb * (double)pRate
                            + (double)c->sampleOffsetFrames;

            // Past the end of the sample: either loop or fall silent for the
            // remainder of the clip. When looping, wrap back to the sample's
            // actual start (frame 0) — the alt-slip offset only sets the entry
            // point, repeats restart at the very beginning of the sample.
            if (posTotal >= (double)s->frameCount) {
                if (loop) {
                    posTotal = fmod(posTotal, (double)s->frameCount);
                } else {
                    // No loop: output silence for this frame and skip.
                    trackL[c->track] += 0.0f;
                    trackR[c->track] += 0.0f;
                    continue;
                }
            }
            if (posTotal < 0.0) posTotal = 0.0;

            ma_uint64 idx = (ma_uint64)posTotal;
            if (idx >= s->frameCount) idx = s->frameCount - 1;
            ma_uint64 nextIdx = (idx + 1 < s->frameCount) ? idx + 1 : idx;
            float frac = (float)(posTotal - (double)idx);

            float l = s->pFrames[idx * 2 + 0]
                + frac * (s->pFrames[nextIdx * 2 + 0] - s->pFrames[idx * 2 + 0]);
            float r = s->pFrames[idx * 2 + 1]
                + frac * (s->pFrames[nextIdx * 2 + 1] - s->pFrames[idx * 2 + 1]);

            // Edge-fade only when NOT looping and near the sample's end, to
            // avoid clicks. When looping, the wrap itself is the seam.
            float edgeFade = 1.0f;
            if (!loop) {
                double framesLeft = (double)s->frameCount - posTotal;
                if (framesLeft < 64.0) {
                    edgeFade = (float)(framesLeft / 64.0);
                    if (edgeFade < 0.0f) edgeFade = 0.0f;
                }
            }
            l *= edgeFade;
            r *= edgeFade;

             
            // samplePos is the monotonic elapsed position within the clip
            // (0..totalSamples), independent of the alt-slip offset and the
            // loop wrap. It must NOT be derived from the wrapped read position,
            // which can fall below the offset and turn the micro-fade into
            // silence on every repeat pass of a looping alt-slipped clip.
            double samplePos = (double)localBeat * (double)ctx->fpb * (double)pRate;
            double totalSamples = (double)c->lengthBeats * ctx->fpb * pRate;
            float microFade = 1.0f;
            if (samplePos < (double)FADE_SAMPLES) {
                microFade = (float)(samplePos / (double)FADE_SAMPLES);
            } else if ((totalSamples - samplePos) < (double)FADE_SAMPLES) {
                microFade = (float)((totalSamples - samplePos) / (double)FADE_SAMPLES);
            }
            if (microFade < 0.0f) microFade = 0.0f;
            if (microFade > 1.0f) microFade = 1.0f;
            l *= microFade;
            r *= microFade;

             
            float fadeInGain = 1.0f;
            if (c->fadeInBeats > 0.0001f && localBeat < c->fadeInBeats) {
                fadeInGain = compute_fade_gain(localBeat / c->fadeInBeats, c->fadeInType, true);
            }
            float fadeOutGain = 1.0f;
            if (c->fadeOutBeats > 0.0001f && (c->lengthBeats - localBeat) < c->fadeOutBeats) {
                 
                float fadeProg = 1.0f - ((c->lengthBeats - localBeat) / c->fadeOutBeats);
                fadeOutGain = compute_fade_gain(fadeProg, c->fadeOutType, false);
            }
            float fade = (fadeInGain < fadeOutGain) ? fadeInGain : fadeOutGain;
            if (fade < 0.0f) fade = 0.0f;
            if (fade > 1.0f) fade = 1.0f;

            const float vol = c->volume;
            trackL[c->track] += l * vol * fade;
            trackR[c->track] += r * vol * fade;
        }

         
        if (ctx->trackGran) {
            uint64_t lo = ctx->trackPlayableMask.lo, hi = ctx->trackPlayableMask.hi;
            while (lo | hi) {
                int t;
                if (lo) { t = ctz_u64(lo); lo &= lo - 1; }
                else    { t = 64 + ctz_u64(hi); hi &= hi - 1; }
                if (t >= nTracks) break;
                granular_process_track_ptr(ctx->trackGran, t, &trackL[t], &trackR[t],
                                           1, ctx->bpm, ctx->swing, currentFrame);
            }
        }

         
        float finalL = 0.0f, finalR = 0.0f;

         
        {
            uint64_t lo = ctx->trackPlayableMask.lo, hi = ctx->trackPlayableMask.hi;
            while (lo | hi) {
                int t;
                if (lo) { t = ctz_u64(lo); lo &= lo - 1; }
                else    { t = 64 + ctz_u64(hi); hi &= hi - 1; }
                if (t >= nTracks) break;

                bool needsScalar = ctx->trackEqActive[t] && ctx->trackEQ;
                if (!needsScalar && ctx->trackPeak) {
                    for (int b = 0; b < 3; ++b) {
                         
                        if (fabsf(ctx->trackPeak[t][b].gainDb) > 0.0001f) { needsScalar = true; break; }
                    }
                }
                 
                if (!needsScalar && ctx->trackFx && ctx->trackFx[t].count > 0) needsScalar = true;
                if (!needsScalar && ctx->trackFilter && track_filter_any_active(&ctx->trackFilter[t])) needsScalar = true;
                if (!needsScalar) continue;

                float L = trackL[t], R = trackR[t];
                if (ctx->trackFilter && track_filter_any_active(&ctx->trackFilter[t]))
                    track_filter_process_block(&ctx->trackFilter[t], (float)SAMPLE_RATE, &L, &R, 1);
                if (ctx->trackFx && ctx->trackFx[t].count > 0)
                    fx_chain_process(&ctx->trackFx[t], &L, &R);
                if (ctx->trackEqActive[t] && ctx->trackEQ)
                    smooth_eq3_process_float(&ctx->trackEQ[t], &L, &R, &L, &R, 1);
                if (ctx->trackPeak) {
                    for (int b = 0; b < 3; ++b)
                        peak_biquad_process(&ctx->trackPeak[t][b], &L, &R);
                }
                finalL += L * gLl[t] + R * gLr[t];
                finalR += L * gRl[t] + R * gRr[t];
                trackL[t] = 0.0f;
                trackR[t] = 0.0f;
            }
        }

         
#if defined(__AVX2__) && (defined(_M_X64) || defined(__x86_64__))
        if (g_cpuHasAvx2) {
            __m256 accL8 = _mm256_setzero_ps(), accR8 = _mm256_setzero_ps();
            int t = 0;
            const int n8 = nTracks & ~7;
            for (; t < n8; t += 8) {
                if (!track_mask_any_range(&ctx->trackPlayableMask, t, 8)) continue;
                __m256 L = _mm256_loadu_ps(&trackL[t]);
                __m256 R = _mm256_loadu_ps(&trackR[t]);
                accL8 = _mm256_add_ps(accL8, _mm256_add_ps(
                    _mm256_mul_ps(L, _mm256_loadu_ps(&gLl[t])),
                    _mm256_mul_ps(R, _mm256_loadu_ps(&gLr[t]))));
                accR8 = _mm256_add_ps(accR8, _mm256_add_ps(
                    _mm256_mul_ps(L, _mm256_loadu_ps(&gRl[t])),
                    _mm256_mul_ps(R, _mm256_loadu_ps(&gRr[t]))));
            }
                __declspec(align(32)) float red8[8];
                 
                accL8 = denormal_flush_ps256(accL8);
                accR8 = denormal_flush_ps256(accR8);
                _mm256_store_ps(red8, accL8);
                finalL += (red8[0] + red8[1]) + (red8[2] + red8[3]) + (red8[4] + red8[5]) + (red8[6] + red8[7]);
                _mm256_store_ps(red8, accR8);
                finalR += (red8[0] + red8[1]) + (red8[2] + red8[3]) + (red8[4] + red8[5]) + (red8[6] + red8[7]);
            for (; t < nTracks; ++t) {
                if (!track_mask_test(&ctx->trackPlayableMask, t)) continue;
                finalL += trackL[t] * gLl[t] + trackR[t] * gLr[t];
                finalR += trackL[t] * gRl[t] + trackR[t] * gRr[t];
            }
        } else
#endif
        {
#if defined(_M_X64) || defined(__x86_64__)
            __m128 accL4 = _mm_setzero_ps(), accR4 = _mm_setzero_ps();
            int t = 0;
            const int n4 = nTracks & ~3;
            for (; t < n4; t += 4) {
                if (!track_mask_any_range(&ctx->trackPlayableMask, t, 4)) continue;
                __m128 L = _mm_loadu_ps(&trackL[t]);
                __m128 R = _mm_loadu_ps(&trackR[t]);
                accL4 = _mm_add_ps(accL4, _mm_add_ps(
                    _mm_mul_ps(L, _mm_loadu_ps(&gLl[t])),
                    _mm_mul_ps(R, _mm_loadu_ps(&gLr[t]))));
                accR4 = _mm_add_ps(accR4, _mm_add_ps(
                    _mm_mul_ps(L, _mm_loadu_ps(&gRl[t])),
                    _mm_mul_ps(R, _mm_loadu_ps(&gRr[t]))));
            }
            __declspec(align(16)) float red4[4];
             
            accL4 = denormal_flush_ps128(accL4);
            accR4 = denormal_flush_ps128(accR4);
            _mm_store_ps(red4, accL4);
            finalL += (red4[0] + red4[1]) + (red4[2] + red4[3]);
            _mm_store_ps(red4, accR4);
            finalR += (red4[0] + red4[1]) + (red4[2] + red4[3]);
            for (; t < nTracks; ++t) {
                if (!track_mask_test(&ctx->trackPlayableMask, t)) continue;
                finalL += trackL[t] * gLl[t] + trackR[t] * gLr[t];
                finalR += trackL[t] * gRl[t] + trackR[t] * gRr[t];
            }
#else
            for (int t = 0; t < nTracks; ++t) {
                if (!track_mask_test(&ctx->trackPlayableMask, t)) continue;
                finalL += trackL[t] * gLl[t] + trackR[t] * gLr[t];
                finalR += trackL[t] * gRl[t] + trackR[t] * gRr[t];
            }
#endif
        }

         
        finalL = denormal_flush_f(finalL);
        finalR = denormal_flush_f(finalR);

         
        if ((++denormTick & 63) == 0) {
            if (ctx->limiter) {
                ctx->limiter->env  = denormal_flush_f(ctx->limiter->env);
                ctx->limiter->gain = denormal_flush_f(ctx->limiter->gain);
            }
            if (ctx->lofiLpL) {
                *ctx->lofiLpL = denormal_flush_f(*ctx->lofiLpL);
                *ctx->lofiLpR = denormal_flush_f(*ctx->lofiLpR);
                *ctx->lofiHoldL = denormal_flush_f(*ctx->lofiHoldL);
                *ctx->lofiHoldR = denormal_flush_f(*ctx->lofiHoldR);
                *ctx->lofiPhase = denormal_flush_f(*ctx->lofiPhase);
            }
#ifdef CSEQ_AUDIO_DEBUG
            denormal_debug_dump();
#endif
        }

         
        if (ctx->isLofi && ctx->lofiPhase) {
            apply_lofi_sample(&finalL, &finalR,
                              ctx->lofiPhase, ctx->lofiHoldL, ctx->lofiHoldR,
                              ctx->lofiLpL, ctx->lofiLpR,
                              ctx->isLofi, ctx->lofiBitDepth, ctx->lofiSampleRate);
        }

         
        if (ctx->applyMaster) {
            float mv = ctx->masterVolume;
            if (mv < 0.0f) mv = 0.0f;
            if (mv > 1.5f) mv = 1.5f;
            finalL *= mv;
            finalR *= mv;

            if (ctx->masterMode == 0) {
                finalL = soft_clip_sample(finalL);
                finalR = soft_clip_sample(finalR);
            } else if (ctx->limiter) {
                stereo_limiter_process(ctx->limiter, &finalL, &finalR);
            }

            if (finalL >  1.0f) finalL =  1.0f;
            if (finalL < -1.0f) finalL = -1.0f;
            if (finalR >  1.0f) finalR =  1.0f;
            if (finalR < -1.0f) finalR = -1.0f;
        }

        outL[f] = finalL;
        outR[f] = finalR;
    }

    return frames;
}

 
// Shared master limiter. Buffers are pre-allocated by audio_limiter_preinit()
// (called from main.c before the device starts) so the audio callback never
// allocates; the callback only runs stereo_limiter_init to set coefficients.
static StereoLimiter g_masterLimiter = {0};

static inline void audio_limiter_preinit(void) {
    stereo_limiter_reserve(&g_masterLimiter, (float)SAMPLE_RATE, 2.0f);
}

static inline void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pDevice; (void)pInput;
    float* out = (float*)pOutput;
    memset(out, 0, frameCount * NUM_CHANNELS * sizeof(float));

     
    audio_thread_init_denormals();

    static float s_masterFade = 0.0f;
    const float kRampStep = 1.0f / 256.0f;

    // Scrub/seek de-click: a playhead jump makes the rendered waveform step
    // from one sample position to another between callbacks, which is a
    // click. A ~3 ms linear ramp over the output across the jump turns the
    // step into an inaudible glide.
    static ma_uint64 s_prevEndFrame = 0;
    static bool     s_prevEndValid = false;
    static ma_uint32 s_scrubFade = 0;
    enum { SCRUB_FADE_LEN = 128 };   // ~2.9 ms at 44.1 kHz

    static float s_lofiPhase = 0.0f;
    static float s_lofiHoldL = 0.0f, s_lofiHoldR = 0.0f;
    static float s_lofiLpL = 0.0f, s_lofiLpR = 0.0f;

    static StereoLimiter* s_limiter = &g_masterLimiter;
    static bool s_limiter_inited = false;
    if (!s_limiter_inited) {
        stereo_limiter_init(s_limiter, (float)SAMPLE_RATE, 2.0f, 100.0f, 0.98f);
        s_limiter_inited = true;
    }

     
    bool previewActive = false;
    if (midi_editor_is_open()) {
        midi_lock();
        // An audition is active if a key is held (auditionHeld) or the PLAY
        // loop is running. Checking auditionHeld (not auditionNote > 0) ensures
        // Quadrum's Kick (voice 0, pitch 0) is treated as an active audition.
        previewActive = (g_midiEdit.auditionHeld || g_midiEdit.isAuditionPlaying);
        int editClip = g_midiEdit.clipIdx;
        midi_unlock();
        // Keep the callback alive after a key release so the synth's release
        // tails / one-shot decays finish ringing instead of freezing mid-note
        // (which is what made Halo "hold in place" and resume on the next
        // click). Only relevant when the timeline isn't already driving the
        // engine (it handles its own tails). synth_editor_has_ringing reads
        // g_Seq.clips, so guard it with seq_lock to avoid racing clip
        // compaction/deletion.

    // Keep the audio callback alive during release tails
    if (!previewActive && !seq_is_playing() && !granular_is_active()) {
            seq_lock();
            bool ringing = synth_editor_has_ringing(editClip);
            seq_unlock();
            if (ringing || midi_audition_has_ringing()) previewActive = true;
        }
    }

    bool shouldPlay = seq_is_playing() || granular_is_active() || previewActive
                    || audition_is_playing()   // Media Explorer audition voice
                    || audition_voice_rendering(); // ...and its fade-out tail
                                                    // renders even when the
                                                    // timeline, granular engine,
                                                    // and MIDI preview are all
                                                    // quiet.

    
    if (!shouldPlay && s_masterFade <= 0.0f) {
        s_masterFade = 0.0f;
        s_limiter->env  = 0.0f;
        s_limiter->gain = 1.0f;
        if (s_limiter->delayL && s_limiter->delayR) {
            memset(s_limiter->delayL, 0, s_limiter->delay_len * sizeof(float));
            memset(s_limiter->delayR, 0, s_limiter->delay_len * sizeof(float));
        }
        return;
    }

    // Feature 1 transient edge state for the live render path. Zeroed =
    // "armed". These are static so the audio callback never allocates; they
    // are re-armed to 0 whenever playback transitions from stopped to playing.
    static uint8_t s_clipTrigState[MAX_CLIPS];
    static uint8_t s_midiNoteArmed[MAX_CLIPS * MIDI_MAX_NOTES];
    static uint8_t s_midiNoteSkipped[MAX_CLIPS * MIDI_MAX_NOTES];
    static bool    s_prevPlaying = false;
    bool nowPlaying = seq_is_playing();
    if (nowPlaying && !s_prevPlaying) {
        memset(s_clipTrigState, 0, sizeof(s_clipTrigState));
        memset(s_midiNoteArmed, 0, sizeof(s_midiNoteArmed));
        memset(s_midiNoteSkipped, 0, sizeof(s_midiNoteSkipped));
    }
    s_prevPlaying = nowPlaying;

    bool soloActive = false;
    TrackMask128 activeMask = { 0, 0 };
    TrackMask128 playableMask = { 0, 0 };

    const float bpm = g_Seq.bpm, swing = g_Seq.swing, fpb = frames_per_beat(bpm);
    ma_uint64 startFrame = (ma_uint64)InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);

    // Detect a playhead jump: anything that isn't a seamless continuation of
    // the previous callback. The timeline's loop wrap is the one expected
    // discontinuity and is excluded so looping stays click-free by itself.
    // This only applies during active timeline playback: while the transport
    // is stopped (e.g. offline note audition), the playhead is static, so the
    // scrub de-click ramp would re-fire on every callback and amplitude-
    // modulate the preview output (a 172 Hz buzz). During audition we instead
    // reset the scrub state so it stays silent.
    if (seq_is_playing()) {
        if (shouldPlay && s_prevEndValid) {
            ma_uint64 expected = s_prevEndFrame;
            ma_uint64 loopTotal = (ma_uint64)((double)total_beats() * (double)fpb);
            bool isWrap = (loopTotal > 0 && startFrame == (s_prevEndFrame % loopTotal));
            if (!isWrap && startFrame != expected) {
                s_scrubFade = SCRUB_FADE_LEN;
            }
        }
        s_prevEndFrame = startFrame + frameCount;
        s_prevEndValid = true;
    } else {
        s_prevEndValid = false;
        s_scrubFade = 0;
    }

     
    enum { MAX_CB_CHUNK = 1024 };
    static float tempL[MAX_CB_CHUNK];
    static float tempR[MAX_CB_CHUNK];

    ma_uint32 framesDone = 0;
    while (framesDone < frameCount) {
        ma_uint32 curChunk = frameCount - framesDone;
        if (curChunk > MAX_CB_CHUNK) curChunk = MAX_CB_CHUNK;
        seq_lock();

         
        if (InterlockedCompareExchange(&g_clipMapStale, 0, 0)) {
            cseq_rebuild_clip_maps_only();
        }

         
        soloActive    = !track_mask_is_empty(&g_Seq.soloMask);
        activeMask    = compute_active_mask(g_Seq.soloMask, g_Seq.muteMask, g_Seq.hasAudioMask);
        playableMask  = compute_playable_mask(g_Seq.soloMask, g_Seq.muteMask);
        g_Seq.activeMask = activeMask;

        RenderContext liveCtx = {
            .clips         = g_Seq.clips,
            .clipCount     = g_Seq.clipCount,
            .trackCount    = g_Seq.trackCount,
            .samples       = g_Seq.samples,
            .sampleCount   = g_Seq.sampleCount,
            .trackMuted    = g_Seq.trackMuted,
            .trackSolo     = g_Seq.trackSolo,
            .trackVolume   = g_Seq.trackVolume,
            .trackPan      = g_Seq.trackPan,
            .trackWidth    = g_Seq.trackWidth,
            .trackEqActive = g_Seq.trackEqActive,
            .trackTriggerProb = g_Seq.trackTriggerProb,
            .trackRngState    = g_Seq.trackRngState,
            .clipTrigState    = s_clipTrigState,
            .midiNoteArmed    = s_midiNoteArmed,
            .midiNoteSkipped  = s_midiNoteSkipped,
            .trackEQ       = g_Seq.trackEQ,
            .trackPeak     = g_Seq.trackPeak,
            .trackFilter   = g_Seq.trackFilter,
            .clipGran      = g_ClipGran,
            .trackGran     = g_TrackGran,
            .clipHalo      = g_ClipHalo,
            .clipQuadrum   = g_ClipQuadrum,
            .trackFx       = g_TrackFx,
            .limiter       = s_limiter,
            .isLofi        = g_Seq.isLofi,
            .lofiBitDepth  = g_Seq.lofiBitDepth,
            .lofiSampleRate= g_Seq.lofiSampleRate,
            .masterVolume  = g_Seq.masterVolume,
            .masterMode    = g_Seq.masterMode,
            .lofiPhase     = &s_lofiPhase,
            .lofiHoldL     = &s_lofiHoldL,
            .lofiHoldR     = &s_lofiHoldR,
            .lofiLpL       = &s_lofiLpL,
            .lofiLpR       = &s_lofiLpR,
            .bpm           = bpm,
            .swing         = swing,
            .fpb           = fpb,
            .totalBeats    = total_beats(),
            .applyMaster   = false,
            .soloActive    = soloActive,
            .trackActiveMask   = activeMask,
            .trackPlayableMask = playableMask
        };

         
        bool runTimeline = seq_is_playing() || granular_is_active();
        if (runTimeline) {
            render_frames(tempL, tempR, curChunk, startFrame + framesDone, &liveCtx);
        } else {
            memset(tempL, 0, curChunk * sizeof(float));
            memset(tempR, 0, curChunk * sizeof(float));
        }

        if (previewActive) {
            midi_editor_process_preview(tempL, tempR, curChunk, bpm, fpb);
        }

        // Media Explorer audition voice: mixed into the master bus before the
        // limiter so it gets the same master treatment. Zero allocation / no
        // I/O on the realtime path; runs whether or not the timeline plays.
        audition_process_voice(tempL, tempR, curChunk);

         
        float mv = g_Seq.masterVolume;
        if (mv < 0.0f) mv = 0.0f;
        if (mv > 1.5f) mv = 1.5f;
        for (ma_uint32 i = 0; i < curChunk; ++i) {
            tempL[i] *= mv;
            tempR[i] *= mv;
            if (g_Seq.masterMode == 0) {
                tempL[i] = soft_clip_sample(tempL[i]);
                tempR[i] = soft_clip_sample(tempR[i]);
            } else {
                stereo_limiter_process(s_limiter, &tempL[i], &tempR[i]);
            }
            if (tempL[i] > 1.0f) tempL[i] = 1.0f;
            if (tempL[i] < -1.0f) tempL[i] = -1.0f;
            if (tempR[i] > 1.0f) tempR[i] = 1.0f;
            if (tempR[i] < -1.0f) tempR[i] = -1.0f;
        }
        seq_unlock();

        for (ma_uint32 f = 0; f < curChunk; ++f) {
            if (shouldPlay) {
                if (s_masterFade < 1.0f) {
                    s_masterFade += kRampStep;
                    if (s_masterFade > 1.0f) s_masterFade = 1.0f;
                }
            } else {
                if (s_masterFade > 0.0f) {
                    s_masterFade -= kRampStep;
                    if (s_masterFade < 0.0f) s_masterFade = 0.0f;
                }
            }

            // De-click ramp across a playhead jump: 0 at the jump sample,
            // back to unity over SCRUB_FADE_LEN frames.
            float scrubGain = 1.0f;
            if (s_scrubFade > 0) {
                scrubGain = 1.0f - (float)s_scrubFade / (float)SCRUB_FADE_LEN;
                s_scrubFade--;
            }

            ma_uint32 outIdx = framesDone + f;
            out[outIdx * 2 + 0] = tempL[f] * s_masterFade * scrubGain;
            out[outIdx * 2 + 1] = tempR[f] * s_masterFade * scrubGain;
        }

        framesDone += curChunk;
    }

    if (seq_is_playing() && shouldPlay) {
        ma_uint64 loopTotalFrames = (ma_uint64)((double)total_beats() * (double)fpb);
        ma_uint64 nextFrame = (loopTotalFrames > 0) ? ((startFrame + frameCount) % loopTotalFrames) : 0;
        set_playback_frame((LONG)nextFrame);
    }
    vis_push_audio_samples(out, (int)frameCount);
}

 

 
static void export_snapshot_free(ExportSnapshot* snap) {
    if (!snap) return;
    for (int i = 0; i < MAX_SAMPLES; ++i) {
        if (snap->samples[i].pFrames) {
            free(snap->samples[i].pFrames);
            snap->samples[i].pFrames = NULL;
        }
    }
    for (int i = 0; i < MAX_CLIPS; ++i) {
        if (snap->clipGran[i].ownFrames) {
            free(snap->clipGran[i].ownFrames);
            snap->clipGran[i].ownFrames = NULL;
        }
        snap->clipGran[i].ownLoaded = false;
        synth_snapshot_free(&snap->clipHalo[i], &snap->clipQuadrum[i]);
    }
    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (snap->trackGran[t].ownFrames) {
            free(snap->trackGran[t].ownFrames);
            snap->trackGran[t].ownFrames = NULL;
        }
        snap->trackGran[t].ownLoaded = false;
    }
    for (int t = 0; t < MAX_TRACKS; ++t) {
        fx_chain_clear(&snap->trackFx[t]);
    }
    free(snap);
}

 
static void export_free_gran_engines(GranularEngine* engs, int count) {
    if (!engs) return;
    for (int i = 0; i < count; ++i) {
        if (engs[i].ownFrames) {
            free(engs[i].ownFrames);
            engs[i].ownFrames = NULL;
        }
        engs[i].ownLoaded = false;
    }
    free(engs);
}

static DWORD WINAPI ExportTimelineThreadProc(LPVOID lpParam) {
    (void)lpParam;
    const char* outputPath = g_Seq.jobPath;
    ULONGLONG startTick = GetTickCount64();

     
    audio_thread_init_denormals();

     
    ExportSnapshot* snap = (ExportSnapshot*)calloc(1, sizeof(ExportSnapshot));
    if (!snap) {
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "Out of memory creating export snapshot.");
        return 1;
    }

    seq_lock();
    snap->clipCount = (g_Seq.clipCount > MAX_CLIPS) ? MAX_CLIPS : g_Seq.clipCount;
    if (snap->clipCount > 0) {
        memcpy(snap->clips, g_Seq.clips, sizeof(Clip) * snap->clipCount);
        for (int i = 0; i < snap->clipCount; ++i) {
            gran_engine_to_snapshot(&snap->clipGran[i], &g_ClipGran[i]);
            synth_snapshot_take(&snap->clipHalo[i], &snap->clipQuadrum[i], i);
        }
    }

    snap->trackCount = (g_Seq.trackCount > MAX_TRACKS) ? MAX_TRACKS : g_Seq.trackCount;
    for (int t = 0; t < snap->trackCount; ++t) {
        snap->trackMuted[t]    = g_Seq.trackMuted[t];
        snap->trackSolo[t]     = g_Seq.trackSolo[t];
        snap->trackVolume[t]   = g_Seq.trackVolume[t];
        snap->trackPan[t]      = g_Seq.trackPan[t];
        snap->trackWidth[t]    = g_Seq.trackWidth[t];
        snap->trackEqActive[t] = g_Seq.trackEqActive[t];
        snap->trackTriggerProb[t] = g_Seq.trackTriggerProb[t];
        snap->trackRngState[t]    = g_Seq.trackRngState[t];
        snap->trackEQ[t]       = g_Seq.trackEQ[t];
        for (int b = 0; b < 3; ++b) {
            snap->trackPeak[t][b] = g_Seq.trackPeak[t][b];
        }
        snap->trackFilter[t]   = g_Seq.trackFilter[t];
        gran_engine_to_snapshot(&snap->trackGran[t], &g_TrackGran[t]);
        fx_chain_to_snapshot(&snap->trackFx[t], &g_TrackFx[t]);
    }

     
    snap->soloMask  = g_Seq.soloMask;
    snap->muteMask  = g_Seq.muteMask;
    snap->hasAudioMask.lo = 0ULL;
    snap->hasAudioMask.hi = 0ULL;
    for (int i = 0; i < snap->clipCount; ++i) {
        int tr = snap->clips[i].track;
        if (tr >= 0 && tr < MAX_TRACKS) track_mask_set(&snap->hasAudioMask, tr);
    }
    snap->activeMask   = compute_active_mask(snap->soloMask, snap->muteMask, snap->hasAudioMask);
    snap->playableMask = compute_playable_mask(snap->soloMask, snap->muteMask);

     
    snap->sampleCount = (g_Seq.sampleCount > MAX_SAMPLES) ? MAX_SAMPLES : g_Seq.sampleCount;
    for (int i = 0; i < snap->sampleCount; ++i) {
        snap->samples[i] = g_Seq.samples[i];
        snap->samples[i].pFrames = NULL;
        // Peaks are a render-only cache and are never freed by the export
        // path; detach so the snapshot can't alias the live pyramid.
        snap->samples[i].peaks = NULL;
        snap->samples[i].peakTotal = 0;
        snap->samples[i].lodCount = 0;
        snap->samples[i].peaksReady = 0;
        if (g_Seq.samples[i].loaded && g_Seq.samples[i].pFrames && g_Seq.samples[i].frameCount > 0) {
            size_t bytes = sizeof(float) * NUM_CHANNELS * (size_t)g_Seq.samples[i].frameCount;
            float* pcm = (float*)malloc(bytes);
            if (pcm) {
                memcpy(pcm, g_Seq.samples[i].pFrames, bytes);
                snap->samples[i].pFrames = pcm;
            } else {
                snap->samples[i].loaded = false;  
            }
        } else {
            snap->samples[i].loaded = false;
        }
    }

    snap->bpm            = g_Seq.bpm;
    snap->swing          = g_Seq.swing;
    snap->visibleBarCount = g_Seq.visibleBarCount;
    snap->timeSigNum     = g_Seq.timeSigNum;
    snap->timeSigDen     = g_Seq.timeSigDen;
    snap->isLofi         = g_Seq.isLofi;
    snap->lofiBitDepth   = g_Seq.lofiBitDepth;
    snap->lofiSampleRate = g_Seq.lofiSampleRate;
    snap->masterVolume   = (int)g_Seq.masterVolume;
    snap->masterMode     = g_Seq.masterMode;
    snap->exportBitDepth = g_Seq.exportBitDepth;
    seq_unlock();
     

    const float bpm = snap->bpm, swing = snap->swing, fpb = frames_per_beat(bpm);
    int safeBars = (snap->visibleBarCount < MIN_BARS) ? MIN_BARS : ((snap->visibleBarCount > MAX_BARS) ? MAX_BARS : snap->visibleBarCount);
    int tsNum = (snap->timeSigNum > 0) ? snap->timeSigNum : 4;
    int tsDen = (snap->timeSigDen > 0) ? snap->timeSigDen : 4;
    const float bpb = (float)tsNum * 4.0f / (float)tsDen;
    const float totalBeats = (float)safeBars * bpb;
    const ma_uint64 totalExportFrames = (ma_uint64)((double)totalBeats * (double)fpb);

    if (totalExportFrames == 0) {
        export_snapshot_free(snap);
        job_end(NULL);
        return 1;
    }

     
    GranularEngine* exportClipGran  = (GranularEngine*)calloc(MAX_CLIPS, sizeof(GranularEngine));
    GranularEngine* exportTrackGran = (GranularEngine*)calloc(MAX_TRACKS, sizeof(GranularEngine));
    // Feature 1 transient edge state for the export render. calloc => zeroed
    // = "armed" for a fresh export, so it never aliases the live arrays.
    uint8_t* exportClipTrig   = (uint8_t*)calloc(MAX_CLIPS, sizeof(uint8_t));
    uint8_t* exportMidiArmed  = (uint8_t*)calloc((size_t)MAX_CLIPS * MIDI_MAX_NOTES, sizeof(uint8_t));
    uint8_t* exportMidiSkipped= (uint8_t*)calloc((size_t)MAX_CLIPS * MIDI_MAX_NOTES, sizeof(uint8_t));
    if (!exportClipGran || !exportTrackGran || !exportClipTrig || !exportMidiArmed || !exportMidiSkipped) {
        export_free_gran_engines(exportClipGran, MAX_CLIPS);
        export_free_gran_engines(exportTrackGran, MAX_TRACKS);
        if (exportClipTrig)   free(exportClipTrig);
        if (exportMidiArmed)  free(exportMidiArmed);
        if (exportMidiSkipped) free(exportMidiSkipped);
        export_snapshot_free(snap);
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "Out of memory initializing export DSP.");
        return 1;
    }

    for (int i = 0; i < snap->clipCount; ++i) {
        gran_snapshot_to_engine(&exportClipGran[i], &snap->clipGran[i], i, snap->clips[i].track);
         
        exportClipGran[i].sampleTable = snap->samples;
        exportClipGran[i].sampleTableCount = snap->sampleCount;
    }
    for (int t = 0; t < snap->trackCount; ++t) {
        gran_snapshot_to_engine(&exportTrackGran[t], &snap->trackGran[t], -1, t);
        exportTrackGran[t].sampleTable = snap->samples;
        exportTrackGran[t].sampleTableCount = snap->sampleCount;
    }

     
    ma_format encFmt = ma_format_f32;
    if (snap->exportBitDepth == 16) encFmt = ma_format_s16;
    else if (snap->exportBitDepth == 24) encFmt = ma_format_s24;
    else encFmt = ma_format_f32;

    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, encFmt, NUM_CHANNELS, SAMPLE_RATE);
    ma_encoder enc;
     
    wchar_t outPathW[MAX_PATH * 2];
    bool haveOutPath = (utf8_to_wide_buf(outputPath, outPathW, (int)(sizeof(outPathW) / sizeof(outPathW[0]))) > 0);
    if (!haveOutPath || ma_encoder_init_file_w(outPathW, &cfg, &enc) != MA_SUCCESS) {
        export_free_gran_engines(exportClipGran, MAX_CLIPS);
        export_free_gran_engines(exportTrackGran, MAX_TRACKS);
        free(exportClipTrig); free(exportMidiArmed); free(exportMidiSkipped);
        export_snapshot_free(snap);
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "Could not create WAV file.");
        return 1;
    }

     
    enum { CHUNK = 4096 };
    float* chunkL = (float*)malloc(CHUNK * sizeof(float));
    float* chunkR = (float*)malloc(CHUNK * sizeof(float));
    float* inter  = (float*)malloc(CHUNK * 2 * sizeof(float));

    StereoLimiter e_limiter;
    stereo_limiter_init(&e_limiter, (float)SAMPLE_RATE, 2.0f, 100.0f, 0.98f);
    float exportLofiPhase = 0.0f, exportHoldL = 0.0f, exportHoldR = 0.0f;
    float exportLpL = 0.0f, exportLpR = 0.0f;

    bool soloActive = !track_mask_is_empty(&snap->soloMask);
    for (int t = 0; t < snap->trackCount && t < MAX_TRACKS; ++t) {
        if (snap->trackSolo[t]) { soloActive = true; break; }
    }

    RenderContext exportCtx = {
        .clips         = snap->clips,
        .clipCount     = snap->clipCount,
        .trackCount    = snap->trackCount,
        .samples       = snap->samples,
        .sampleCount   = snap->sampleCount,
        .trackMuted    = snap->trackMuted,
        .trackSolo     = snap->trackSolo,
        .trackVolume   = snap->trackVolume,
        .trackPan      = snap->trackPan,
        .trackWidth    = snap->trackWidth,
        .trackEqActive = snap->trackEqActive,
        .trackTriggerProb = snap->trackTriggerProb,
        .trackRngState    = snap->trackRngState,
        .clipTrigState    = exportClipTrig,
        .midiNoteArmed    = exportMidiArmed,
        .midiNoteSkipped  = exportMidiSkipped,
        .trackEQ       = snap->trackEQ,
        .trackPeak     = snap->trackPeak,
        .trackFilter   = snap->trackFilter,
        .clipGran      = exportClipGran,
        .trackGran     = exportTrackGran,
        .clipHalo      = snap->clipHalo,
        .clipQuadrum   = snap->clipQuadrum,
        .trackFx       = snap->trackFx,
        .limiter       = &e_limiter,
        .isLofi        = snap->isLofi,
        .lofiBitDepth  = snap->lofiBitDepth,
        .lofiSampleRate= snap->lofiSampleRate,
        .masterVolume  = (float)snap->masterVolume,
        .masterMode    = snap->masterMode,
        .lofiPhase     = &exportLofiPhase,
        .lofiHoldL     = &exportHoldL,
        .lofiHoldR     = &exportHoldR,
        .lofiLpL       = &exportLpL,
        .lofiLpR       = &exportLpR,
        .bpm           = bpm,
        .swing         = swing,
        .fpb           = fpb,
        .totalBeats    = totalBeats,
        .applyMaster   = true,
        .soloActive    = soloActive,
        .trackActiveMask   = snap->activeMask,
        .trackPlayableMask = snap->playableMask
    };

     
    wchar_t tmpPathW[MAX_PATH * 2];
    {
        wchar_t tempDirW[MAX_PATH];
        UINT got = GetTempPathW(MAX_PATH, tempDirW);
        if (got > 0 && got < MAX_PATH) {
            size_t len = wcslen(tempDirW);
            const wchar_t* sep = (len > 0 && tempDirW[len - 1] != L'\\' && tempDirW[len - 1] != L'/') ? L"\\" : L"";
            _snwprintf(tmpPathW, sizeof(tmpPathW) / sizeof(tmpPathW[0]), L"%s%scseq_exp_%llu.raw",
                       tempDirW, sep, (unsigned long long)GetTickCount64());
        } else if (haveOutPath) {
            _snwprintf(tmpPathW, sizeof(tmpPathW) / sizeof(tmpPathW[0]), L"%s.tmp.raw", outPathW);
        } else {
            _snwprintf(tmpPathW, sizeof(tmpPathW) / sizeof(tmpPathW[0]), L"cseq_exp_%llu.tmp.raw",
                       (unsigned long long)GetTickCount64());
        }
        tmpPathW[(sizeof(tmpPathW) / sizeof(tmpPathW[0])) - 1] = L'\0';
    }

    FILE* tmp = _wfopen(tmpPathW, L"wb+");
    if (!tmp || !chunkL || !chunkR || !inter) {
        if (tmp) { fclose(tmp); DeleteFileW(tmpPathW); }
        if (chunkL) free(chunkL);
        if (chunkR) free(chunkR);
        if (inter)  free(inter);
        stereo_limiter_free(&e_limiter);
        export_free_gran_engines(exportClipGran, MAX_CLIPS);
        export_free_gran_engines(exportTrackGran, MAX_TRACKS);
        free(exportClipTrig); free(exportMidiArmed); free(exportMidiSkipped);
        export_snapshot_free(snap);
        ma_encoder_uninit(&enc);
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "Out of memory / file creation error.");
        return 1;
    }

    bool exportSuccess = true;

     
    for (ma_uint64 base = 0; base < totalExportFrames; base += CHUNK) {
        ma_uint32 n = (ma_uint32)((base + CHUNK > totalExportFrames)
            ? (totalExportFrames - base)
            : CHUNK);

        
        render_frames(chunkL, chunkR, n, base, &exportCtx);

        for (ma_uint32 i = 0; i < n; ++i) {
            inter[i * 2 + 0] = chunkL[i];
            inter[i * 2 + 1] = chunkR[i];
        }

        if (fwrite(inter, sizeof(float) * 2, n, tmp) != (size_t)n) {
            exportSuccess = false;
            break;
        }

        job_set_progress((int)((base * 90ull) / (totalExportFrames > 0 ? totalExportFrames : 1)));
    }

     
    ma_uint64 totalWrittenFrames = totalExportFrames;
    const ma_uint32 kMaxTail = SAMPLE_RATE / 2;
    ma_uint32 tailLeft = kMaxTail;
    ma_uint64 tailBase = totalExportFrames;

    while (exportSuccess && tailLeft > 0) {
        bool anyActive = false;
        for (int t = 0; t < snap->trackCount && t < MAX_TRACKS; ++t) {
            for (int i = 0; i < GRAN_MAX_GRAINS; ++i)
                if (exportTrackGran[t].grains[i].active) { anyActive = true; break; }
            if (anyActive) break;
        }
        if (!anyActive) {
            for (int c = 0; c < snap->clipCount && c < MAX_CLIPS; ++c) {
                if (!snap->clips[c].isGranular) continue;
                for (int i = 0; i < GRAN_MAX_GRAINS; ++i)
                    if (exportClipGran[c].grains[i].active) { anyActive = true; break; }
                if (anyActive) break;
            }
        }
        if (!anyActive) break;

        ma_uint32 n = (tailLeft > CHUNK) ? CHUNK : tailLeft;

        
        for (int c = 0; c < snap->clipCount && c < MAX_CLIPS; ++c) exportClipGran[c].density = 0.0f;
        for (int t = 0; t < snap->trackCount && t < MAX_TRACKS; ++t) exportTrackGran[t].density = 0.0f;

        render_frames(chunkL, chunkR, n, tailBase, &exportCtx);

        for (ma_uint32 i = 0; i < n; ++i) {
            inter[i * 2 + 0] = chunkL[i];
            inter[i * 2 + 1] = chunkR[i];
        }

        if (fwrite(inter, sizeof(float) * 2, n, tmp) != (size_t)n) {
            exportSuccess = false;
            break;
        }

        totalWrittenFrames += n;
        tailBase += n;
        tailLeft -= n;
    }

     
    if (exportSuccess) {
        fflush(tmp);
        rewind(tmp);
        ma_uint64 remaining = totalWrittenFrames;
        uint32_t dither_seed = 123456789;

        while (remaining > 0) {
            ma_uint32 n = (ma_uint32)((remaining > CHUNK) ? CHUNK : remaining);
            if (fread(inter, sizeof(float), n * 2, tmp) != n * 2) {
                exportSuccess = false;
                break;
            }

            if (encFmt == ma_format_s16) {
                int16_t* out16 = (int16_t*)inter;
                const float step = 1.0f / 32767.0f;
                for (ma_uint32 i = 0; i < n * 2; ++i) {
                    float v = inter[i];
                    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
                    dither_seed = dither_seed * 1664525u + 1013904223u;
                    float u1 = (float)(dither_seed & 0xFFFF) / 65536.0f - 0.5f;
                    dither_seed = dither_seed * 1664525u + 1013904223u;
                    float u2 = (float)(dither_seed & 0xFFFF) / 65536.0f - 0.5f;
                    float dither = (u1 + u2) * 0.5f;
                    float dithered = v + dither * step;
                    if (dithered > 1.0f) dithered = 1.0f;
                    if (dithered < -1.0f) dithered = -1.0f;
                    out16[i] = (int16_t)(dithered * 32767.0f);
                }
                ma_encoder_write_pcm_frames(&enc, out16, n, NULL);
            } else if (encFmt == ma_format_s24) {
                uint8_t* out24 = (uint8_t*)inter;
                const float step = 1.0f / 8388607.0f;
                for (ma_uint32 i = 0; i < n * 2; ++i) {
                    float v = inter[i];
                    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
                    dither_seed = dither_seed * 1664525u + 1013904223u;
                    float u1 = (float)(dither_seed & 0xFFFF) / 65536.0f - 0.5f;
                    dither_seed = dither_seed * 1664525u + 1013904223u;
                    float u2 = (float)(dither_seed & 0xFFFF) / 65536.0f - 0.5f;
                    float dither = (u1 + u2) * 0.5f;
                    float dithered = v + dither * step;
                    if (dithered > 1.0f) dithered = 1.0f;
                    if (dithered < -1.0f) dithered = -1.0f;
                    int32_t s24 = (int32_t)(dithered * 8388607.0f);
                    out24[i * 3 + 0] = (uint8_t)(s24 & 0xFF);
                    out24[i * 3 + 1] = (uint8_t)((s24 >> 8) & 0xFF);
                    out24[i * 3 + 2] = (uint8_t)((s24 >> 16) & 0xFF);
                }
                ma_encoder_write_pcm_frames(&enc, out24, n, NULL);
            } else {
                for (ma_uint32 i = 0; i < n * 2; ++i) {
                    float v = inter[i];
                    if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
                    inter[i] = v;
                }
                ma_encoder_write_pcm_frames(&enc, inter, n, NULL);
            }
            remaining -= n;
        }
    }

     
    fclose(tmp);
    DeleteFileW(tmpPathW);
    ma_encoder_uninit(&enc);

    stereo_limiter_free(&e_limiter);
    free(chunkL); free(chunkR); free(inter);
    export_free_gran_engines(exportClipGran, MAX_CLIPS);
    export_free_gran_engines(exportTrackGran, MAX_TRACKS);
    free(exportClipTrig); free(exportMidiArmed); free(exportMidiSkipped);
    export_snapshot_free(snap);

    if (exportSuccess) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Exported in %.1fs.", (float)((GetTickCount64() - startTick) / 1000.0));
        job_end(msg);
        return 0;
    } else {
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "An error occurred while exporting audio.");
        return 1;
    }
}

static inline void export_timeline_to_wav(const char* outputPath) {
    if (!job_begin(3, outputPath)) {
        cseq_report_error(g_hWnd, "Export Error", "Another file operation is already in progress.");
        return;
    }

    // Timeline isolation: export renders from a snapshot taken under seq_lock,
    // and the SoundFont audio comes from the immutable pre-rendered sample bank
    // (g_SFontCache), so export is fully thread-safe and playback can keep
    // running while it happens. The export thread renders its own copies of the
    // clips/samples/granular engines, so it never disturbs the live render.
    // Only clear the MIDI audition so the preview path isn't double-rendering
    // into the live callback during export.
    midi_lock();
    midi_audition_clear_poly();
    g_midiEdit.isAuditionPlaying = false;
    g_midiEdit.auditionPlayheadBeat = 0.0;
    midi_unlock();

    
    HANDLE hThread = CreateThread(NULL, 0, ExportTimelineThreadProc, NULL, CREATE_SUSPENDED, NULL);
    if (hThread) {
        SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
        ResumeThread(hThread);
        CloseHandle(hThread);
    } else {
        job_end(NULL);
        cseq_report_error(g_hWnd, "Export Error", "Failed to launch export worker thread.");
    }
}
