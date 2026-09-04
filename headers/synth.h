// --- Synth module engines: Quadrum (drum) + Halo (poly) -----------------------
// Integration layer between the vendored DSP engines (synth_halo.h /
// synth_quadrum.h) and cseq's realtime render path.
//
// Per-clip runtime state mirrors the granular pattern (g_ClipGran[MAX_CLIPS]):
// a static array indexed by clip index, read/written only under the seq lock.
// The user-facing *parameters* live inline on the Clip struct (quadrumParams[8]
// / haloPatch) so undo (whole-Clip memcpy) and the CSQY codec carry them; the
// runtime state here is a derived rendering cache that is rebuilt from those
// parameters, so it never needs to be serialized.
//
// Halo: a realtime per-block synth. Its HaloVoiceManager holds all voice
// runtime state (phases, filters, envelopes) and is advanced on the audio
// thread; note-on/off just call the cheap voice functions.
//
// Quadrum: renders each drum voice as a whole transient into a cached buffer
// (quadrum_render). Triggers during playback just play back the cached buffer;
// the buffer is re-rendered only when that voice's patch changes. Buffers are
// malloc'd lazily and freed on clip teardown / project reset, so idle clips
// cost nothing.

#ifndef CSEQ_SYNTH_H
#define CSEQ_SYNTH_H

#include "globals.h"
#include "dsp.h"

// --- Halo per-clip runtime state -------------------------------------------
typedef struct {
    HaloVoiceManager vm;            // polyphonic voice state (8 voices)
    // Per-note-index edge tracking for timeline playback: noteActive[i] is
    // true when clip note i is currently sounding. Indexed by note index (not
    // pitch) so multiple notes on the same pitch can't overwrite each other.
    bool   noteActive[MIDI_MAX_NOTES];
    // The exact HaloVoice instance allocated for clip note i. Cached so that
    // note-off releases that voice directly WITHOUT re-entering the allocator
    // (which can reset phase/filter state and cause a pop).
    HaloVoice* noteVoice[MIDI_MAX_NOTES];
    // Dedicated audition state: the pitch currently held by a piano-key click
    // (-1 if none), the exact voice for that key, and per-note tracking for the
    // preview PLAY loop (separate from timeline noteActive so key audition and
    // the loop never collide).
    int    auditionActivePitch;
    HaloVoice* auditionVoice;
    bool   auditionNoteActive[MIDI_MAX_NOTES];
    HaloVoice* auditionNoteVoice[MIDI_MAX_NOTES];
    // Previous clip-local beat, used to detect loop-wrap/scrub discontinuities
    // so held voices are released instead of getting stuck sustaining.
    float  lastLocalBeat;
    bool   initialized;
} SynthHaloState;

// --- Quadrum per-clip runtime state ----------------------------------------
typedef struct {
    // Per-voice cached rendered transient (mono). The audio thread reads
    // buf[v] / len[v] / pos[v] / playing[v] only under the seq lock. A second
    // staging buffer (renderBuf[v]) is used for off-thread re-rendering so a
    // knob edit never writes the buffer the audio thread is reading; the two
    // are swapped under a brief seq lock.
    float* buf[8];
    float* renderBuf[8];   // staging double-buffer for re-render
    int    len[8];         // rendered sample count
    int    pos[8];         // playback read position
    bool   playing[8];     // currently sounding
    bool   cached[8];      // buf[v] holds a render matching the current patch
    // Per-note-index edge tracking for timeline playback (see SynthHaloState).
    bool   noteActive[MIDI_MAX_NOTES];
    // Dedicated audition state (see SynthHaloState).
    int    auditionActiveVoice;
    bool   auditionNoteActive[MIDI_MAX_NOTES];
    // A cheap signature of the current patch so we can detect edits and
    // invalidate the cache without comparing every field.
    double patchSig[8];
} SynthQuadrumState;

// Per-clip runtime state arrays (defined in main.c). Mirrors g_ClipGran.
extern SynthHaloState    g_ClipHalo[MAX_CLIPS];
extern SynthQuadrumState g_ClipQuadrum[MAX_CLIPS];

// A cheap, stable hash of a QuadrumParams bank so the audio thread can tell
// whether a voice's cached transient is stale after an edit.
static inline double synth_quadrum_patch_sig(const QuadrumParams* p) {
    if (!p) return 0.0;
    double s = 0.0;
    s = s * 31.0 + p->pitch;
    s = s * 31.0 + p->pitch_env;
    s = s * 31.0 + p->pitch_decay;
    s = s * 31.0 + p->fm_ratio;
    s = s * 31.0 + p->fm_depth;
    s = s * 31.0 + p->noise_mix;
    s = s * 31.0 + p->noise_decay;
    s = s * 31.0 + p->noise_cutoff;
    s = s * 31.0 + p->click;
    s = s * 31.0 + p->filter_cutoff;
    s = s * 31.0 + p->filter_q;
    s = s * 31.0 + p->filter_type;
    s = s * 31.0 + p->drive;
    s = s * 31.0 + p->decay;
    s = s * 31.0 + p->clap_taps;
    s = s * 31.0 + p->clap_spread;
    return s;
}

// Initialize one clip's synth runtime state. Called under the seq lock when a
// clip is created or the project is (re)loaded. Must run OFF the audio thread:
// for Quadrum clips it pre-allocates AND pre-renders all 8 voice transients
// here, so the realtime path never allocates and never runs the expensive
// quadrum_render (a full 8-voice render is ~60ms — far too slow for the audio
// thread).
static inline void synth_state_init_clip(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    SynthHaloState* h = &g_ClipHalo[clipIdx];
    if (!h->initialized) {
        voice_manager_init(&h->vm, (double)SAMPLE_RATE);
        memset(h->noteActive, 0, sizeof(h->noteActive));
        memset(h->noteVoice, 0, sizeof(h->noteVoice));
        h->auditionActivePitch = -1;
        h->auditionVoice = NULL;
        memset(h->auditionNoteActive, 0, sizeof(h->auditionNoteActive));
        memset(h->auditionNoteVoice, 0, sizeof(h->auditionNoteVoice));
        h->lastLocalBeat = 0.0f;
        h->initialized = true;
    }
    SynthQuadrumState* q = &g_ClipQuadrum[clipIdx];
    memset(q->playing, 0, sizeof(q->playing));
    memset(q->noteActive, 0, sizeof(q->noteActive));
    q->auditionActiveVoice = -1;
    memset(q->auditionNoteActive, 0, sizeof(q->auditionNoteActive));
    memset(q->cached, 0, sizeof(q->cached));
    if (clipIdx < g_Seq.clipCount && g_Seq.clips[clipIdx].clipKind == CLIP_KIND_QUADRUM) {
        Clip* c = &g_Seq.clips[clipIdx];
        for (int v = 0; v < 8; ++v) {
            if (!q->buf[v]) {
                q->buf[v] = (float*)malloc((size_t)QUADRUM_MAX_SAMPLES * sizeof(float));
                if (!q->buf[v]) { q->len[v] = 0; continue; }
            }
            if (!q->renderBuf[v]) {
                q->renderBuf[v] = (float*)malloc((size_t)QUADRUM_MAX_SAMPLES * sizeof(float));
                if (!q->renderBuf[v]) { q->len[v] = 0; continue; }
            }
            // Pre-render this voice off the audio thread.
            q->len[v] = quadrum_render(&c->quadrumParams[v], q->buf[v], QUADRUM_MAX_SAMPLES);
            q->cached[v] = (q->len[v] > 0);
            q->patchSig[v] = synth_quadrum_patch_sig(&c->quadrumParams[v]);
        }
    }
}

// Re-render a clip's Quadrum voice transients after a parameter edit. Must run
// OFF the audio thread (called from the synth UI when a knob changes). The
// audio thread only ever plays back the cached buffers, never re-renders.
// Self-locking: renders each voice with NO lock held, then swaps the buffer
// pointer under a brief seq lock. Callers must NOT hold seq_lock around this.
static inline void synth_quadrum_rerender_clip(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    if (clipIdx >= g_Seq.clipCount) return;
    Clip* c = &g_Seq.clips[clipIdx];
    if (c->clipKind != CLIP_KIND_QUADRUM) return;
    SynthQuadrumState* q = &g_ClipQuadrum[clipIdx];
    for (int v = 0; v < 8; ++v) {
        if (!q->buf[v] || !q->renderBuf[v]) continue;
        // Render into the staging buffer with no lock held.
        int n = quadrum_render(&c->quadrumParams[v], q->renderBuf[v], QUADRUM_MAX_SAMPLES);
        // Swap pointers under a brief lock so the audio thread never sees a
        // half-written buffer.
        seq_lock();
        float* tmp = q->buf[v];
        q->buf[v] = q->renderBuf[v];
        q->renderBuf[v] = tmp;
        q->len[v] = n;
        q->cached[v] = (n > 0);
        q->patchSig[v] = synth_quadrum_patch_sig(&c->quadrumParams[v]);
        seq_unlock();
    }
}

// Re-render a single Quadrum voice after a parameter edit (cheaper than the
// full clip when dragging one knob). Must run OFF the audio thread. This is
// self-locking: it copies the params under a brief seq lock, renders into the
// staging buffer with NO lock held (quadrum_render is ~8ms and must never run
// while the audio thread is waiting on the seq lock), then swaps the buffer
// pointer under a brief seq lock. Callers must NOT hold seq_lock around this.
static inline void synth_quadrum_rerender_voice(int clipIdx, int voice) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    if (clipIdx >= g_Seq.clipCount) return;
    if (voice < 0 || voice >= 8) return;
    Clip* c = &g_Seq.clips[clipIdx];
    if (c->clipKind != CLIP_KIND_QUADRUM) return;
    SynthQuadrumState* q = &g_ClipQuadrum[clipIdx];
    if (!q->buf[voice] || !q->renderBuf[voice]) return;

    // Snapshot the params under the lock so the render reads a consistent patch.
    QuadrumParams params;
    seq_lock();
    params = c->quadrumParams[voice];
    seq_unlock();

    // Render with no lock held.
    int n = quadrum_render(&params, q->renderBuf[voice], QUADRUM_MAX_SAMPLES);

    // Swap the freshly-rendered buffer in under a brief lock.
    seq_lock();
    float* tmp = q->buf[voice];
    q->buf[voice] = q->renderBuf[voice];
    q->renderBuf[voice] = tmp;
    q->len[voice] = n;
    q->cached[voice] = (n > 0);
    q->patchSig[voice] = synth_quadrum_patch_sig(&params);
    seq_unlock();
}

// Free any heap-allocated quadrum transient buffers for a clip. Called on clip
// delete / project clear / reset. Must not run on the audio thread.
static inline void synth_state_free_clip(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return;
    SynthQuadrumState* q = &g_ClipQuadrum[clipIdx];
    for (int v = 0; v < 8; ++v) {
        if (q->buf[v]) { free(q->buf[v]); q->buf[v] = NULL; }
        if (q->renderBuf[v]) { free(q->renderBuf[v]); q->renderBuf[v] = NULL; }
        q->len[v] = 0; q->pos[v] = 0; q->playing[v] = false; q->cached[v] = false;
    }
}

// Silence all synth voices/transients on transport stop (mirrors
// granular_stop_all). Called off the audio thread when playback halts so
// ringing Halo voices and playing Quadrum transients don't keep sounding and
// click at the stop/loop boundary. Caller should hold the seq lock.
static inline void synth_stop_all(void) {
    for (int i = 0; i < MAX_CLIPS; ++i) {
        SynthHaloState* h = &g_ClipHalo[i];
        if (h->initialized) {
            for (int v = 0; v < HALO_MAX_VOICES; ++v) {
                voice_force_idle(&h->vm.voices[v]);
            }
            memset(h->noteActive, 0, sizeof(h->noteActive));
            memset(h->noteVoice, 0, sizeof(h->noteVoice));
            h->auditionActivePitch = -1;
            h->auditionVoice = NULL;
            memset(h->auditionNoteActive, 0, sizeof(h->auditionNoteActive));
            memset(h->auditionNoteVoice, 0, sizeof(h->auditionNoteVoice));
            h->lastLocalBeat = 0.0f;
        }
        SynthQuadrumState* q = &g_ClipQuadrum[i];
        memset(q->playing, 0, sizeof(q->playing));
        memset(q->noteActive, 0, sizeof(q->noteActive));
        q->auditionActiveVoice = -1;
        memset(q->auditionNoteActive, 0, sizeof(q->auditionNoteActive));
    }
}

// Free a snapshot's heap-allocated quadrum buffers.
static inline void synth_snapshot_free(SynthHaloState* halo, SynthQuadrumState* quad);

// Deep-copy live synth state into an export snapshot (off the audio thread).
// Quadrum transient buffers are heap-copied so export renders from its own
// copy and never races the live engine. Returns 0 on success, -1 if a buffer
// allocation fails (caller should free via synth_snapshot_free).
static inline int synth_snapshot_take(SynthHaloState* dstHalo, SynthQuadrumState* dstQuad,
                                      int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return 0;
    *dstHalo = g_ClipHalo[clipIdx];
    // The copied struct's HaloVoice* pointers point into the LIVE engine's
    // vm.voices[]; remap them to the snapshot's own vm.voices[] so export
    // releases the correct voice instances. Recompute from the source's
    // pointers by offset within the live voice array.
    SynthHaloState* srcHalo = &g_ClipHalo[clipIdx];
    for (int i = 0; i < MIDI_MAX_NOTES; ++i) {
        if (srcHalo->noteVoice[i]) {
            ptrdiff_t off = srcHalo->noteVoice[i] - srcHalo->vm.voices;
            if (off >= 0 && off < HALO_MAX_VOICES)
                dstHalo->noteVoice[i] = &dstHalo->vm.voices[off];
            else
                dstHalo->noteVoice[i] = NULL;
        }
        if (srcHalo->auditionNoteVoice[i]) {
            ptrdiff_t off = srcHalo->auditionNoteVoice[i] - srcHalo->vm.voices;
            if (off >= 0 && off < HALO_MAX_VOICES)
                dstHalo->auditionNoteVoice[i] = &dstHalo->vm.voices[off];
            else
                dstHalo->auditionNoteVoice[i] = NULL;
        }
    }
    if (srcHalo->auditionVoice) {
        ptrdiff_t off = srcHalo->auditionVoice - srcHalo->vm.voices;
        if (off >= 0 && off < HALO_MAX_VOICES)
            dstHalo->auditionVoice = &dstHalo->vm.voices[off];
        else
            dstHalo->auditionVoice = NULL;
    }
    SynthQuadrumState* src = &g_ClipQuadrum[clipIdx];
    SynthQuadrumState* dst = dstQuad;
    memset(dst, 0, sizeof(*dst));
    // Copy per-clip edge/audition state (per-voice buffers handled below).
    memcpy(dst->noteActive, src->noteActive, sizeof(dst->noteActive));
    dst->auditionActiveVoice = src->auditionActiveVoice;
    memcpy(dst->auditionNoteActive, src->auditionNoteActive, sizeof(dst->auditionNoteActive));
    for (int v = 0; v < 8; ++v) {
        dst->buf[v] = NULL;
        dst->len[v] = src->len[v];
        dst->pos[v] = src->pos[v];
        dst->playing[v] = src->playing[v];
        dst->cached[v] = src->cached[v];
        dst->patchSig[v] = src->patchSig[v];
        if (src->buf[v] && src->len[v] > 0) {
            dst->buf[v] = (float*)malloc((size_t)QUADRUM_MAX_SAMPLES * sizeof(float));
            if (!dst->buf[v]) { synth_snapshot_free(dstHalo, dstQuad); return -1; }
            memcpy(dst->buf[v], src->buf[v], (size_t)QUADRUM_MAX_SAMPLES * sizeof(float));
        }
    }
    return 0;
}

// Free a snapshot's heap-allocated quadrum buffers.
static inline void synth_snapshot_free(SynthHaloState* halo, SynthQuadrumState* quad) {
    (void)halo;
    if (!quad) return;
    for (int v = 0; v < 8; ++v) {
        if (quad->buf[v]) { free(quad->buf[v]); quad->buf[v] = NULL; }
    }
}

// Reset all per-clip synth state (project new/clear). Caller holds no locks.
static inline void synth_state_reset_all(void) {
    for (int i = 0; i < MAX_CLIPS; ++i) {
        synth_state_free_clip(i);
        memset(&g_ClipHalo[i], 0, sizeof(SynthHaloState));
        memset(&g_ClipQuadrum[i], 0, sizeof(SynthQuadrumState));
    }
}

// Shift synth state left by one slot at `del` when a clip is removed and the
// clip array is compacted (keeps g_ClipHalo/g_ClipQuadrum in lockstep with
// g_Seq.clips, exactly like g_ClipGran). Caller holds the seq lock.
static inline void synth_state_shift_left(int del, int clipCount) {
    if (del < 0 || clipCount <= 0) return;
    for (int j = del; j < clipCount - 1; ++j) {
        synth_state_free_clip(j);                      // free the slot we overwrite
        g_ClipHalo[j]    = g_ClipHalo[j + 1];
        g_ClipQuadrum[j] = g_ClipQuadrum[j + 1];
    }
    // Clear the vacated tail slot.
    synth_state_free_clip(clipCount - 1);
    memset(&g_ClipHalo[clipCount - 1], 0, sizeof(SynthHaloState));
    memset(&g_ClipQuadrum[clipCount - 1], 0, sizeof(SynthQuadrumState));
}

// Forward declarations (definitions below the dispatcher).
static inline void synth_clip_process_frames_halo(const Clip* c, SynthHaloState* st,
                                                  float* L, float* R, ma_uint32 n,
                                                  ma_uint64 startGlobalFrame, float swing, float fpb,
                                                  float triggerProb, uint32_t* rngState);
static inline void synth_clip_process_frames_quadrum(const Clip* c, SynthQuadrumState* st,
                                                     float* L, float* R, ma_uint32 n,
                                                     ma_uint64 startGlobalFrame, float swing, float fpb,
                                                     float triggerProb, uint32_t* rngState);
static inline void mix_quadrum_active(SynthQuadrumState* st, float* L, float* R, int frames);

// Render one frame of a Quadrum or Halo clip into the track accumulators.
// st points into the per-clip runtime state arrays (live: g_ClipHalo /
// g_ClipQuadrum; export: the snapshot's own copies). Writes into the track
// accumulators so the per-track DSP chain (filter -> FX -> EQ -> peak) and the
// mixdown apply equally to synth clips.
static inline void synth_clip_process_frames(const Clip* c, SynthHaloState* haloSt,
                                             SynthQuadrumState* quadSt, float* L, float* R,
                                             ma_uint32 n, ma_uint64 startGlobalFrame,
                                             float bpm, float swing, float fpb,
                                             float triggerProb, uint32_t* rngState) {
    (void)bpm;
    if (!c || !c->isMidi || c->midiNoteCount <= 0 || n == 0) return;
    if (c->clipKind == CLIP_KIND_HALO) {
        if (!haloSt) return;
        synth_clip_process_frames_halo(c, haloSt, L, R, n, startGlobalFrame, swing, fpb,
                                       triggerProb, rngState);
    } else if (c->clipKind == CLIP_KIND_QUADRUM) {
        if (!quadSt) return;
        synth_clip_process_frames_quadrum(c, quadSt, L, R, n, startGlobalFrame, swing, fpb,
                                          triggerProb, rngState);
    }
}

// --- Halo per-frame processing ---------------------------------------------
static inline void synth_clip_process_frames_halo(const Clip* c, SynthHaloState* st,
                                                  float* L, float* R, ma_uint32 n,
                                                  ma_uint64 startGlobalFrame, float swing, float fpb,
                                                  float triggerProb, uint32_t* rngState) {
    (void)n;
    if (!st->initialized) {
        voice_manager_init(&st->vm, (double)SAMPLE_RATE);
        st->auditionActivePitch = -1;
        st->auditionVoice = NULL;
        st->initialized = true;
    }

    // The Halo patch's own amp envelope (amp_attack/decay/sustain/release) is
    // authoritative — edited via the [SYNTH] panel knobs. De-click floors: a
    // minimum attack prevents note-on clicks; a minimum release prevents
    // truncating a waveform mid-cycle at note-off (a pop).
    HaloPatch patch = c->haloPatch;
    if (patch.amp_attack  < 0.002) patch.amp_attack  = 0.002;
    if (patch.amp_release < 0.020) patch.amp_release = 0.020; // 20 ms de-click floor

    // Time window for this frame in clip-local beats.
    float startLinearBeat = (float)((double)startGlobalFrame / (double)fpb);
    float swungStart = apply_clip_swing(c->startBeat, swing);
    float localBeat = startLinearBeat - swungStart;

    // Detect a playhead discontinuity (loop wrap / scrub): if localBeat jumped
    // backward or skipped far ahead, force-idle every voice and clear all note
    // state so a sustaining voice can't get stuck across the boundary.
    if (st->initialized && localBeat >= 0.0f &&
        st->lastLocalBeat > 0.0f && localBeat < st->lastLocalBeat - 0.001f) {
        for (int v = 0; v < HALO_MAX_VOICES; ++v) {
            voice_force_idle(&st->vm.voices[v]);
        }
        memset(st->noteActive, 0, sizeof(st->noteActive));
        memset(st->noteVoice, 0, sizeof(st->noteVoice));
    }
    st->lastLocalBeat = localBeat;

    // Detect note-on/off edges against the previous frame and drive voices.
    // Indexed by note index i (not pitch) so multiple notes on the same pitch
    // can't overwrite each other's edge state (which caused 44.1 kHz
    // retriggering). Note-off releases the exact cached voice directly without
    // re-entering the allocator (which can reset phase/filter state → pop).
    if (localBeat >= 0.0f) {
        for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
            const MidiNote* nt = &c->midiNotes[i];
            if (!nt->active) continue;
            int pitch = nt->pitch;
            if (pitch < 0 || pitch >= MIDI_MAX_NOTES) continue;
            float ntStart = apply_note_clip_swing(nt->startBeat, swing);
            float ntEnd = ntStart + nt->lengthBeats;
            bool nowOn = (localBeat >= ntStart && localBeat < ntEnd);
            bool wasOn = st->noteActive[i];
            if (nowOn && !wasOn) {
                // Feature 1: per-track trigger probability gates the note-on.
                // A failed roll still marks the note active (so it won't
                // re-roll) but allocates no voice; the note-off path below is
                // a safe no-op on a NULL voice.
                if (rngState && !track_roll_probability(rngState, triggerProb)) {
                    st->noteVoice[i] = NULL;
                } else {
                    float vel = (nt->velocity > 1.0f) ? (nt->velocity / 100.0f) : nt->velocity;
                    if (vel < 0.0f) vel = 0.0f; if (vel > 1.0f) vel = 1.0f;
                    HaloVoice* v = voice_manager_alloc(&st->vm, pitch);
                    if (v) {
                        voice_note_on(v, pitch, vel, (double)SAMPLE_RATE, &patch);
                        st->noteVoice[i] = v;
                    } else {
                        st->noteVoice[i] = NULL;
                    }
                }
            } else if (!nowOn && wasOn) {
                if (st->noteVoice[i]) {
                    voice_note_off(st->noteVoice[i]);
                    st->noteVoice[i] = NULL;
                }
            }
            st->noteActive[i] = nowOn;
        }
    }

    // Render this frame. Keep rendering past the clip end as long as any voice
    // is still ringing into its release tail (like Quadrum's one-shot decay),
    // so a note ending at the clip boundary isn't chopped to 0 in one sample.
    bool anyVoiceActive = false;
    for (int v = 0; v < HALO_MAX_VOICES; ++v) {
        if (st->vm.voices[v].active) { anyVoiceActive = true; break; }
    }
    if (localBeat >= 0.0f && (localBeat < c->lengthBeats || anyVoiceActive)) {
        float tmp[2] = { 0.0f, 0.0f };
        halo_process_audio(&st->vm, &patch, tmp, 1);
        L[0] += tmp[0]; R[0] += tmp[1];
    }
}

// --- Quadrum per-frame processing ------------------------------------------
static inline void synth_clip_process_frames_quadrum(const Clip* c, SynthQuadrumState* st,
                                                     float* L, float* R, ma_uint32 n,
                                                     ma_uint64 startGlobalFrame, float swing, float fpb,
                                                     float triggerProb, uint32_t* rngState) {
    (void)n;

    float startLinearBeat = (float)((double)startGlobalFrame / (double)fpb);
    float swungStart = apply_clip_swing(c->startBeat, swing);
    float localBeat = startLinearBeat - swungStart;
    if (localBeat < 0.0f || localBeat >= c->lengthBeats) {
        // Outside the clip: no new triggers, but keep mixing any ringing voices.
        mix_quadrum_active(st, L, R, 1);
        return;
    }

    // Detect note-on edges (pitch 0-7 = voice index) and trigger playback of
    // the pre-rendered cached transient. The audio thread NEVER calls
    // quadrum_render (it is ~60ms for 8 voices); rendering happens off-thread
    // at clip init/load and on knob edits. Indexed by note index i (not voice)
    // so multiple notes on the same drum voice can't overwrite each other's
    // edge state (which caused 44.1 kHz retriggering of the transient).
    for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
        const MidiNote* nt = &c->midiNotes[i];
        if (!nt->active) continue;
        int voice = nt->pitch;
        if (voice < 0 || voice >= 8) continue;
        float ntStart = apply_note_clip_swing(nt->startBeat, swing);
        float ntEnd = ntStart + nt->lengthBeats;
        bool nowOn = (localBeat >= ntStart && localBeat < ntEnd);
        bool wasOn = st->noteActive[i];
        if (nowOn && !wasOn) {
            // Feature 1: per-track trigger probability gates the drum hit.
            // A failed roll still marks the note active so it won't re-roll.
            if (!rngState || track_roll_probability(rngState, triggerProb)) {
                if (st->buf[voice] && st->len[voice] > 0) {
                    st->pos[voice] = 0;
                    st->playing[voice] = true;
                }
            }
        }
        st->noteActive[i] = nowOn;
    }

    mix_quadrum_active(st, L, R, 1);
}

// Mix all currently-ringing quadrum transients into L/R for one frame.
static inline void mix_quadrum_active(SynthQuadrumState* st, float* L, float* R, int frames) {
    for (int v = 0; v < 8; ++v) {
        if (!st->playing[v] || !st->buf[v] || st->len[v] <= 0) continue;
        for (int f = 0; f < frames; ++f) {
            int p = st->pos[v];
            if (p >= st->len[v]) { st->playing[v] = false; break; }
            float s = st->buf[v][p];
            L[f] += s; R[f] += s;
            st->pos[v] = p + 1;
        }
    }
}

// Report whether the currently-edited synth clip's engine still has ringing
// voices / transients. Used to keep the audio callback alive after a key
// release so Halo release tails and Quadrum one-shot decays finish sounding
// instead of freezing mid-note. Read on the audio thread (the engine state is
// advanced by this same thread, so this is a benign snapshot of the previous
// callback's render).
static inline bool synth_editor_has_ringing(int clipIdx) {
    if (clipIdx < 0 || clipIdx >= MAX_CLIPS) return false;
    if (clipIdx >= g_Seq.clipCount) return false;
    const Clip* c = &g_Seq.clips[clipIdx];
    if (!c->isMidi) return false;
    if (c->clipKind == CLIP_KIND_HALO) {
        const SynthHaloState* st = &g_ClipHalo[clipIdx];
        if (!st->initialized) return false;
        for (int v = 0; v < HALO_MAX_VOICES; ++v) {
            if (st->vm.voices[v].active) return true;
        }
        return false;
    } else if (c->clipKind == CLIP_KIND_QUADRUM) {
        const SynthQuadrumState* st = &g_ClipQuadrum[clipIdx];
        for (int v = 0; v < 8; ++v) {
            if (st->playing[v]) return true;
        }
        return false;
    }
    return false;
}

// Editor audition: render one frame for the currently-edited synth clip.
// keyNote/keyHeld drive the piano-roll key click (Halo: a held note; Quadrum:
// a triggered voice 0-7). The PLAY loop scans the clip's notes at localBeat,
// mirroring the timeline. The audio thread never calls quadrum_render here —
// it only plays back the pre-rendered cached transients. Uses the same per-clip
// engine state as timeline playback, so key clicks and PLAY are heard.
static inline void synth_editor_process_preview(const Clip* c, int clipIdx,
                                                float* L, float* R,
                                                int keyNote, bool keyHeld,
                                                float localBeat, bool playLoop,
                                                float fpb) {
    (void)fpb;
    if (!c) return;
    // Runs under seq_lock, so reading g_Seq.swing is safe.
    const float swing = g_Seq.swing;
    if (c->clipKind == CLIP_KIND_HALO) {
        SynthHaloState* st = &g_ClipHalo[clipIdx];
        if (!st->initialized) {
            voice_manager_init(&st->vm, (double)SAMPLE_RATE);
            st->auditionActivePitch = -1;
            st->auditionVoice = NULL;
            st->initialized = true;
        }
        HaloPatch patch = c->haloPatch;
        // De-click floors: minimum attack prevents note-on clicks; minimum
        // release prevents truncating a waveform mid-cycle at note-off (a pop).
        if (patch.amp_attack  < 0.002) patch.amp_attack  = 0.002;
        if (patch.amp_release < 0.020) patch.amp_release = 0.020;

        // Key-click audition. Dedicated auditionActivePitch + cached voice so
        // dragging across keys releases the previous voice directly (no
        // re-entering the allocator → no pop) and release always releases it.
        if (keyHeld) {
            if (keyNote > 0 && keyNote != st->auditionActivePitch) {
                if (st->auditionVoice) {
                    voice_note_off(st->auditionVoice);
                    st->auditionVoice = NULL;
                }
                HaloVoice* v = voice_manager_alloc(&st->vm, keyNote);
                if (v) voice_note_on(v, keyNote, 0.8f, (double)SAMPLE_RATE, &patch);
                st->auditionActivePitch = keyNote;
                st->auditionVoice = v;
            }
        } else {
            if (st->auditionVoice) {
                voice_note_off(st->auditionVoice);
                st->auditionVoice = NULL;
                st->auditionActivePitch = -1;
            }
        }

        // PLAY loop: scan the clip's notes at the audition playhead, using
        // dedicated per-note auditionNoteActive state + cached voices so the
        // loop never touches the manual key audition and note-off is direct.
        if (playLoop) {
            for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
                const MidiNote* nt = &c->midiNotes[i];
                if (!nt->active) continue;
                int pitch = nt->pitch;
                if (pitch < 0 || pitch >= MIDI_MAX_NOTES) continue;
                float sNote = apply_note_clip_swing(nt->startBeat, swing);
                bool nowOn = (localBeat >= sNote && localBeat < sNote + nt->lengthBeats);
                bool wasOn = st->auditionNoteActive[i];
                if (nowOn && !wasOn) {
                    float vel = (nt->velocity > 1.0f) ? (nt->velocity / 100.0f) : nt->velocity;
                    if (vel < 0.0f) vel = 0.0f; if (vel > 1.0f) vel = 1.0f;
                    HaloVoice* v = voice_manager_alloc(&st->vm, pitch);
                    if (v) {
                        voice_note_on(v, pitch, vel, (double)SAMPLE_RATE, &patch);
                        st->auditionNoteVoice[i] = v;
                    }
                } else if (!nowOn && wasOn) {
                    if (st->auditionNoteVoice[i]) {
                        voice_note_off(st->auditionNoteVoice[i]);
                        st->auditionNoteVoice[i] = NULL;
                    }
                }
                st->auditionNoteActive[i] = nowOn;
            }
        }

        float tmp[2] = { 0.0f, 0.0f };
        halo_process_audio(&st->vm, &patch, tmp, 1);
        L[0] += tmp[0]; R[0] += tmp[1];
    } else if (c->clipKind == CLIP_KIND_QUADRUM) {
        SynthQuadrumState* st = &g_ClipQuadrum[clipIdx];

        // Key-click: trigger the voice on a new key (one-shot keeps ringing).
        // Dedicated auditionActiveVoice; on release clear it AND all pad state
        // so voices 1-7 can never stay latched.
        if (keyHeld && keyNote >= 0 && keyNote < 8 && keyNote != st->auditionActiveVoice) {
            if (st->buf[keyNote] && st->len[keyNote] > 0) { st->pos[keyNote] = 0; st->playing[keyNote] = true; }
            st->auditionActiveVoice = keyNote;
        } else if (!keyHeld) {
            st->auditionActiveVoice = -1;
        }

        // PLAY loop: scan the clip's notes using dedicated auditionNoteActive.
        if (playLoop) {
            for (int i = 0; i < c->midiNoteCount && i < MIDI_MAX_NOTES; ++i) {
                const MidiNote* nt = &c->midiNotes[i];
                if (!nt->active) continue;
                int voice = nt->pitch;
                if (voice < 0 || voice >= 8) continue;
                float sNote = apply_note_clip_swing(nt->startBeat, swing);
                bool nowOn = (localBeat >= sNote && localBeat < sNote + nt->lengthBeats);
                bool wasOn = st->auditionNoteActive[i];
                if (nowOn && !wasOn) {
                    if (st->buf[voice] && st->len[voice] > 0) { st->pos[voice] = 0; st->playing[voice] = true; }
                }
                st->auditionNoteActive[i] = nowOn;
            }
        }

        mix_quadrum_active(st, L, R, 1);
    }
}

#endif /* CSEQ_SYNTH_H */
