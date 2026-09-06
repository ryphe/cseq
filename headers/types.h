#pragma once
#include "config.h"

#define MIDI_KB_MAX 8   // polyphonic note-audition voice cap (mouse + QWERTY)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include "miniaudio.h"
#include "eq.h"
// Synth engine headers are self-contained, allocation-free DSP. They are
// pulled in here (before the Clip struct) so the per-clip patch structs
// (HaloPatch / QuadrumParams) can be embedded inline and therefore ride the
// whole-struct undo memcpy and the CSQY serialization automatically.
#include "synth_halo.h"
#include "synth_quadrum.h"

typedef struct { float min, max; } Peak;

// Lock-free per-track trigger-probability roll. Never call standard rand() on
// the audio path (Hard Rule 3): this is a dedicated xorshift32 RNG with a
// per-track state, so it is reentrant and deterministic. prob is clamped to
// [0,1]; 1.0 always triggers, 0.0 never does, and the 24-bit sample keeps the
// comparison cheap and reproducible across live/export renders.
static inline bool track_roll_probability(uint32_t *state, float prob) {
    if (prob >= 1.0f) return true;
    if (prob <= 0.0f) return false;
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return ((float)(x & 0x00FFFFFF) / (float)0x01000000) < prob;
}

// Multi-resolution peak cache geometry (heap-allocated per AudioSample).
// Level 0 holds one min/max pair per PEAK_BASE_BIN_FRAMES frames; each level
// up aggregates PEAK_LOD_RATIO:1, so the renderer can pick a level whose time
// resolution matches the current zoom instead of interpolating a fixed
// 2048-point staircase (the old blocky-long-file behavior).
#define PEAK_BASE_BIN_FRAMES 512          // ~11.6 ms at 44.1 kHz (~86 peaks/s)
#define PEAK_LOD_RATIO       4
#define PEAK_MAX_LOD_LEVELS  16
// Cap level 0 at 1M entries (~15 h audio, 8 MB); longer files still work,
// level 0 just covers a coarser span and upper levels refine the overview.
#define PEAK_MAX_BASE_ENTRIES (1 << 20)

 
 
typedef struct { uint64_t lo, hi; } TrackMask128;

 
typedef uint64_t BarBitfield[BAR_BITFIELD_WORDS];

 
typedef uint64_t fixed32_32_t;
#define FIXED_ONE       (1ULL << 32)
#define FIXED_FRAC_MASK 0xFFFFFFFFULL

static inline fixed32_32_t float_to_fixed(float f) {
     
    if (!(f > 1e-30f)) return 0;    
    double d = (double)f * (double)FIXED_ONE;
    if (d >= 18446744073709551615.0) return 0xFFFFFFFFFFFFFFFFULL;  
    return (fixed32_32_t)d;
}

 
static inline uint64_t mulshift_u64(uint64_t a, uint64_t b, unsigned shift) {
    uint32_t a0 = (uint32_t)a, a1 = (uint32_t)(a >> 32);
    uint32_t b0 = (uint32_t)b, b1 = (uint32_t)(b >> 32);
    uint64_t ll = (uint64_t)a0 * b0;
    uint64_t lm = (uint64_t)a0 * b1;
    uint64_t ml = (uint64_t)a1 * b0;
    uint64_t mh = (uint64_t)a1 * b1;
    uint64_t mid = (ll >> 32) + (uint32_t)lm + (uint32_t)ml;
    uint64_t hi = mh + (lm >> 32) + (ml >> 32) + (mid >> 32);
    uint64_t lo = ((mid & 0xFFFFFFFFULL) << 32) | (uint32_t)ll;
    if (shift == 0)  return lo;
    if (shift >= 64) return (shift == 64) ? hi : 0;
    return (hi << (64 - shift)) | (lo >> shift);
}

 
typedef struct __declspec(align(64)) TrackAudioHot {
    float   gain_current, gain_target;
    float   pan;                     
    float   volume;                  
    float*  bufferL;                 
    float*  bufferR;
    uint64_t readPos;                
    fixed32_32_t rate;               
    bool    active;                  
} TrackAudioHot;

typedef struct {
    char     name[32];
    uint32_t color;
    bool     muted, solo;
} TrackUICold;

 
typedef struct {
    int   pitch;
    float startBeat;
    float lengthBeats;
    float velocity;
    bool  active;
    bool  isSelected;
    float dragStartBeatOrig, dragLengthOrig;
    int   dragPitchOrig;
} MidiNote;

// Synth module clip kinds. 0 = legacy sample/MIDI clip; 1/2 are MIDI-backed
// clips (isMidi stays true so notes ride the existing storage/serialization)
// rendered and edited as dedicated synth piano rolls.
enum {
    CLIP_KIND_SAMPLE  = 0,
    CLIP_KIND_QUADRUM = 1,   // cyan drum synth - 8 fixed voices
    CLIP_KIND_HALO    = 2    // orange poly synth - traditional piano roll
};

typedef struct {
    char filename[MAX_PATH], name[64];
    float* pFrames;
    ma_uint64 frameCount;
    // Concatenated LOD peak levels (level 0 first). NULL when no cache has
    // been built yet (e.g. granular own-samples) - renderers draw a flat line.
    Peak* peaks;
    int   peakTotal;                   // entries across all levels
    int   lodCount;                    // number of levels built
    int   lodOffset[PEAK_MAX_LOD_LEVELS];
    int   lodEntries[PEAK_MAX_LOD_LEVELS];
    // 1 = full pyramid built; 0 = only the coarse preview (level 0 strided)
    // is present while a background thread refines it.
    volatile LONG peaksReady;
    bool loaded;
    // Disk-backed PCM cache: when pFrames points into a memory-mapped cache
    // file, hCacheMap holds the mapping handle (NULL = heap-backed buffer).
    // contentHash is the FNV-1a of the decoded PCM, used for import dedup.
    HANDLE hCacheMap;
    uint64_t contentHash;
} AudioSample;

// Frees the peak pyramid and clears the peak fields (PCM frames untouched).
static inline void free_peak_cache(AudioSample* s) {
    if (!s) return;
    free(s->peaks);
    s->peaks = NULL;
    s->peakTotal = 0;
    s->lodCount = 0;
    memset(s->lodOffset, 0, sizeof(s->lodOffset));
    memset(s->lodEntries, 0, sizeof(s->lodEntries));
    InterlockedExchange(&s->peaksReady, 0);
}

typedef struct {
    int sampleIndex, track;
    float startBeat, lengthBeats;
    ma_uint64 sampleOffsetFrames;
    float volume, playbackRate, fadeInBeats, fadeOutBeats;
    uint8_t fadeInType;              
    uint8_t fadeOutType;             
    uint16_t nextClipInBar;          
    bool isSelected;
    bool isGranular;
    bool isMuted;
    bool isMidi;                     
    uint8_t clipKind;                // CLIP_KIND_* (0 = sample/MIDI)
    MidiNote midiNotes[MIDI_MAX_NOTES];
    int midiNoteCount;
    float adsrAttack, adsrDecay, adsrSustain, adsrRelease;
    // Synth-module (Quadrum/Halo) envelope scaffolding. These map directly to
    // the future internal synth engines' per-voice envelopes; the shared
    // adsr* fields above stay the sample/MIDI path's own. The editor's ADSR
    // knobs write here for synth kinds so the values are engine-ready.
    float synthAttack, synthDecay, synthSustain, synthRelease;
    // Synth-module patches. Quadrum is a per-voice (8) parameter bank; Halo is
    // a single polyphonic patch. Inline so undo (whole-Clip memcpy) and the
    // CSQY codec section carry them automatically. The synth* ADSR floats above
    // remain the piano-roll envelope source that feeds the engine's amp env.
    QuadrumParams quadrumParams[8];
    HaloPatch      haloPatch;
    float dragStartBeatOrig;
    float dragStartLengthOrig;
    int dragStartTrackOrig;
    ma_uint64 dragStartOffsetOrig;
} Clip;

typedef struct { COLORREF waveColor, bgColor, selectWaveColor, selectBgColor, borderColor; } TrackTheme;

typedef struct {
    float     phase, pos, rate, amp, panL, panR, velocity;
    float     phaseInc;      // 1.0f/lengthFrames, precomputed at spawn
    int       lengthFrames;
    bool      active;
    ma_uint64 startFrame;
} Grain;

typedef struct {
    bool      active;
    int       midiNote;
    float     velocity, startBeat, lengthBeats;
    bool      isSelected;
    float     dragStartBeatOrig, dragLengthOrig;
    int       dragMidiOrig;
} GranNote;

typedef struct {
    Grain     grains[GRAN_MAX_GRAINS];
    int       grainCount;
    GranNote  notes[GRAN_MAX_NOTES];
    int       noteCount;
    bool      enabled;
    float     grainSizeMs, density, position, posJitter, pitch, pitchJitter, panSpread, attack, release, volume;
    bool      freeze, droneMode;
    int       sampleIndex, octaveShift;
    char      ownSampleName[64];
    float* ownFrames;
    ma_uint64 ownFrameCount;
    bool      ownLoaded;
    AudioSample ownSample;
     
    AudioSample* sampleTable;
    int          sampleTableCount;
    float     spawnAcc;
    int       trackIdx, clipIdx;
    bool      isDraggingParam;
    int       dragParam, selectedNote;
    bool      isDraggingNote, isResizingLeft, isResizingRight, isCtrlDuplicating, hasMovedPastThreshold;
    int       dragStartX, dragStartY;
    float     dragStartBeatOffset, dragLeadBeatOrig;
    int       dragLeadMidiOrig, auditionNote;
    // Polyphonic note audition (mouse strip + QWERTY keys): the engine
    // streams grains across every held note. auditionNote above stays the
    // most-recently-pressed pitch for paint highlighting.
    int       kbHeldNotes[MIDI_KB_MAX];
    int       kbHeldVKs[MIDI_KB_MAX]; // physical key per held pitch (key-up match)
    int       kbHeldCount;
    int       mouseNote;             // pitch held by the mouse strip, -1 none
    int       auditionNotes[MIDI_KB_MAX];
    int       auditionNoteCount;
    int       auditionSpawnIdx;      // round-robin index into auditionNotes
    bool      isMarqueeSelecting;
    int       marqueeStartX, marqueeStartY, marqueeCurX, marqueeCurY;
} GranularEngine;

typedef struct {
    bool      enabled;
    float     grainSizeMs, density, position, posJitter, pitch, pitchJitter, panSpread, attack, release, volume;
    bool      freeze, droneMode;
    int       sampleIndex, octaveShift;
    GranNote  notes[GRAN_MAX_NOTES];
    int       noteCount;
     
    bool      ownLoaded;
    float*    ownFrames;
    ma_uint64 ownFrameCount;
} GranClipSnapshot;

typedef struct {
    int sampleIndex, trackOffset;
    float beatOffset, lengthBeats;
    ma_uint64 sampleOffsetFrames;
    float volume, playbackRate, fadeInBeats, fadeOutBeats;
    uint8_t fadeInType;              
    uint8_t fadeOutType;             
    bool isMuted;
    bool isGranular;
    bool isMidi;
    uint8_t clipKind;
    float adsrAttack, adsrDecay, adsrSustain, adsrRelease;
    float synthAttack, synthDecay, synthSustain, synthRelease;
    QuadrumParams quadrumParams[8];
    HaloPatch      haloPatch;
    MidiNote* midiNotes;
    int midiNoteCount;
    GranClipSnapshot* granSnapshot;
} ClipboardItem;

typedef struct {
    Clip* clips;
    GranClipSnapshot* clipGran;
    GranClipSnapshot* trackGran;
    struct FxTrackSnapshot* trackFx;
    int8_t trackSidechainSource[MAX_TRACKS];
    int               clipCount;
} UndoSnapshot;

typedef struct {
    float b0, b1, b2, a1, a2, z1L, z2L, z1R, z2R;
    float gainDb;    
} PeakBiquad;

// Per-track filter plotter: up to four cascaded RBJ biquads (LP/HP/BP/Notch)
// applied before the FX chain. Bands stack - any combination can be active at
// once - and a master enable bypasses everything. All bands share one
// cutoff/Q (the graph drag + wheel).
//
// Thread/smoothing model: the UI thread writes ONLY the target params
// (frequency/q/typeMask/enabled) under seq_lock; the audio thread slews
// curFrequency/curQ toward them at a 16-sample control rate and rebuilds the
// band coefficients itself. Instant coefficient replacement would make the
// TDF-II delay states step-discontinuously (zipper noise / crackle while
// dragging), and clearing the states per update clicks on every mouse tick -
// so states are only cleared on init/project load.
// magnitude[] is render-only data owned by the UI thread; the audio thread
// never reads it.
#define TRACK_FILTER_POINTS 512
#define TRACK_FILTER_FMIN   20.0f
#define TRACK_FILTER_FMAX   20000.0f

enum {
    TRACK_FILTER_LP = 0,
    TRACK_FILTER_HP,
    TRACK_FILTER_BP,
    TRACK_FILTER_NOTCH,
    TRACK_FILTER_TYPE_COUNT
};
#define TRACK_FILTER_BIT(t) (1u << (t))
#define TRACK_FILTER_MASK_ALL ((1u << TRACK_FILTER_TYPE_COUNT) - 1u)

#define TRACK_FILTER_TWO_PI 6.28318530717958647692

// One series biquad: normalized coefficients + per-channel TDF-II state.
typedef struct {
    float b0, b1, b2, a1, a2;
    float s1L, s2L, s1R, s2R;
} TrackFilterBiquad;

typedef struct {
    bool     enabled;      // master: false bypasses every band
    uint32_t typeMask;     // TRACK_FILTER_BIT(type) per active band
    float    frequency;    // target cutoff Hz (UI writes under seq_lock)
    float    q;            // target Q (UI writes under seq_lock)
    // Smoothed values the running coefficients follow (audio thread only).
    float    curFrequency;
    float    curQ;
    int      ctlCounter;   // control-rate divider for smoothing/rebuild
    TrackFilterBiquad band[TRACK_FILTER_TYPE_COUNT];
    // Combined magnitude response in dB of the enabled bands, at
    // TRACK_FILTER_POINTS log-spaced frequencies (20 Hz..20 kHz).
    // UI-thread only (computed from the targets, never the smoothed values).
    float magnitude[TRACK_FILTER_POINTS];
} TrackFilter;

// Double-precision |H|^2 of one biquad at cos(w), cos(2w). The notch/LP
// denominator cancels O(1) terms down to ~1e-7 at f0, which float32 rounding
// destroys (renders as striping on the plot), so this math stays in double.
static inline double track_filter_band_db(const TrackFilterBiquad* b, double c, double c2) {
    double num = (double)b->b0 * b->b0 + (double)b->b1 * b->b1 + (double)b->b2 * b->b2
               + 2.0 * (double)b->b1 * ((double)b->b0 + b->b2) * c
               + 2.0 * (double)b->b0 * b->b2 * c2;
    double den = 1.0 + (double)b->a1 * b->a1 + (double)b->a2 * b->a2
               + 2.0 * (double)b->a1 * (1.0 + (double)b->a2) * c
               + 2.0 * (double)b->a2 * c2;
    double mag2 = (den > 1e-12) ? num / den : 0.0;
    if (mag2 < 1e-12) mag2 = 1e-12;
    return 10.0 * log10(mag2);
}

// Rebuild all band coefficients from the SMOOTHED curFrequency/curQ (audio
// thread, control rate). Cheap: one sinf/cosf pair, no curve computation.
static inline void track_filter_rebuild_coeffs(TrackFilter* f, float sampleRate) {
    if (!f) return;
    if (sampleRate <= 0.0f) sampleRate = (float)SAMPLE_RATE;
    f->typeMask &= TRACK_FILTER_MASK_ALL;

    float f0 = f->curFrequency;
    if (f0 < TRACK_FILTER_FMIN) f0 = TRACK_FILTER_FMIN;
    if (f0 > sampleRate * 0.495f) f0 = sampleRate * 0.495f;
    float q = f->curQ;
    if (q < 0.05f) q = 0.05f;
    if (q > 100.0f) q = 100.0f;

    // All four RBJ types share the same denominator at a given f0/Q, so the
    // trig is computed once and only the numerators differ per band.
    const float TWO_PI = 6.28318530717958647692f;
    float w0 = TWO_PI * f0 / sampleRate;
    float cosW = cosf(w0);
    float sinW = sinf(w0);
    float alpha = sinW / (2.0f * q);

    float bNum[TRACK_FILTER_TYPE_COUNT][3];
    // LP
    bNum[TRACK_FILTER_LP][0] = (1.0f - cosW) * 0.5f;
    bNum[TRACK_FILTER_LP][1] =  1.0f - cosW;
    bNum[TRACK_FILTER_LP][2] = (1.0f - cosW) * 0.5f;
    // HP
    bNum[TRACK_FILTER_HP][0] =  (1.0f + cosW) * 0.5f;
    bNum[TRACK_FILTER_HP][1] = -(1.0f + cosW);
    bNum[TRACK_FILTER_HP][2] =  (1.0f + cosW) * 0.5f;
    // BP (constant 0 dB peak gain)
    bNum[TRACK_FILTER_BP][0] =  alpha;
    bNum[TRACK_FILTER_BP][1] =  0.0f;
    bNum[TRACK_FILTER_BP][2] = -alpha;
    // Notch
    bNum[TRACK_FILTER_NOTCH][0] =  1.0f;
    bNum[TRACK_FILTER_NOTCH][1] = -2.0f * cosW;
    bNum[TRACK_FILTER_NOTCH][2] =  1.0f;

    float a0 = 1.0f + alpha;
    float invA0 = 1.0f / a0;
    float a1n = (-2.0f * cosW) * invA0;
    float a2n = (1.0f - alpha) * invA0;

    for (int t = 0; t < TRACK_FILTER_TYPE_COUNT; ++t) {
        TrackFilterBiquad* b = &f->band[t];
        b->b0 = bNum[t][0] * invA0;
        b->b1 = bNum[t][1] * invA0;
        b->b2 = bNum[t][2] * invA0;
        b->a1 = a1n;
        b->a2 = a2n;
    }
}

// UI-side update: recompute coefficients from the targets AND the magnitude
// curve (render data). Heavy (512 double-precision evals) - the plotter calls
// this AFTER releasing seq_lock, on its own state copy, so the audio thread
// never waits behind the curve math.
static inline void track_filter_update(TrackFilter* f, float sampleRate) {
    if (!f) return;
    if (sampleRate <= 0.0f) sampleRate = (float)SAMPLE_RATE;
    f->typeMask &= TRACK_FILTER_MASK_ALL;
    f->curFrequency = f->frequency;
    f->curQ = f->q;
    track_filter_rebuild_coeffs(f, sampleRate);

    // Combined dB curve of the enabled bands (bands cascade => dB adds).
    double logStep = log(TRACK_FILTER_FMAX / TRACK_FILTER_FMIN)
                   / (double)(TRACK_FILTER_POINTS - 1);
    for (int i = 0; i < TRACK_FILTER_POINTS; ++i) {
        float probe = TRACK_FILTER_FMIN * expf((float)((double)i * logStep));
        if (probe > sampleRate * 0.495f) probe = sampleRate * 0.495f;
        double w = TRACK_FILTER_TWO_PI * probe / sampleRate;
        double c = cos(w);
        double c2 = cos(2.0 * w);
        double db = 0.0;
        for (int t = 0; t < TRACK_FILTER_TYPE_COUNT; ++t) {
            if (f->typeMask & (1u << t))
                db += track_filter_band_db(&f->band[t], c, c2);
        }
        f->magnitude[i] = (float)db;
    }
}

static inline void track_filter_clear_state(TrackFilter* f) {
    if (!f) return;
    for (int t = 0; t < TRACK_FILTER_TYPE_COUNT; ++t) {
        TrackFilterBiquad* b = &f->band[t];
        b->s1L = b->s2L = b->s1R = b->s2R = 0.0f;
    }
}

static inline void track_filter_init_defaults(TrackFilter* f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->enabled = false;
    f->typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP);
    f->frequency = 1000.0f;
    f->q = 0.7071067811865475f;
    track_filter_update(f, (float)SAMPLE_RATE);
}

// UI-side target setter (called under seq_lock): writes targets only. The
// audio path slews its smoothed copies toward them - no coefficient jump, no
// state clear, no click.
static inline void track_filter_set_target(TrackFilter* f, float freqHz, float q) {
    if (!f) return;
    if (freqHz < TRACK_FILTER_FMIN) freqHz = TRACK_FILTER_FMIN;
    if (freqHz > TRACK_FILTER_FMAX) freqHz = TRACK_FILTER_FMAX;
    if (q < 0.05f) q = 0.05f;
    if (q > 100.0f) q = 100.0f;
    f->frequency = freqHz;
    f->q = q;
}

// Clamp helpers mirroring set_target (used by rebuild + project load).
static inline void track_filter_clamp_params(TrackFilter* f) {
    if (!f) return;
    track_filter_set_target(f, f->frequency, f->q);
    f->curFrequency = f->frequency;
    f->curQ = f->q;
}

static inline bool track_filter_any_active(const TrackFilter* f) {
    return f->enabled && f->typeMask != 0;
}

// Flushes with the same 1e-18 threshold as dsp.h's denormal_flush_f (dsp.h
// includes this header, so the shared helper can't be referenced from here).
#include <float.h>

static inline float track_filter_flushf(float v) {
    return (fabsf(v) < 1e-18f) ? 0.0f : v;
}

// Series pass through every enabled band's TDF-II biquad (audio thread).
static inline void track_filter_process(TrackFilter* f, float* L, float* R) {
    if (!track_filter_any_active(f)) return;
    float inL = *L, inR = *R;
    // A non-finite input would poison the state registers permanently, so
    // gate it here instead of letting NaN/Inf propagate through the filters.
    if (!_finite(inL)) inL = 0.0f;
    if (!_finite(inR)) inR = 0.0f;

    for (int t = 0; t < TRACK_FILTER_TYPE_COUNT; ++t) {
        if (!(f->typeMask & (1u << t))) continue;
        TrackFilterBiquad* b = &f->band[t];

        float yL = b->b0 * inL + b->s1L;
        b->s1L = b->b1 * inL - b->a1 * yL + b->s2L;
        b->s2L = b->b2 * inL - b->a2 * yL;

        float yR = b->b0 * inR + b->s1R;
        b->s1R = b->b1 * inR - b->a1 * yR + b->s2R;
        b->s2R = b->b2 * inR - b->a2 * yR;

        inL = yL;
        inR = yR;
    }

    // Flush once per sample over the cascaded result; per-stage states decay
    // with the same cascade output, so this bounds every register.
    float fl = track_filter_flushf(inL);
    float fr = track_filter_flushf(inR);
    for (int t = 0; t < TRACK_FILTER_TYPE_COUNT; ++t) {
        TrackFilterBiquad* b = &f->band[t];
        b->s1L = track_filter_flushf(b->s1L);
        b->s2L = track_filter_flushf(b->s2L);
        b->s1R = track_filter_flushf(b->s1R);
        b->s2R = track_filter_flushf(b->s2R);
    }
    *L = fl;
    *R = fr;
}

// Audio-side per-block entry: slew curFrequency/curQ toward the targets at a
// 16-sample control rate and rebuild coefficients while settling. Exponential
// one-pole slewing gives ~15 ms settling - fast enough to feel glued to the
// drag, slow enough that the TDF-II states never see a coefficient step.
#define TRACK_FILTER_CTL_INTERVAL 16
#define TRACK_FILTER_SLEW         0.25f

static inline void track_filter_process_block(TrackFilter* f, float sampleRate,
                                              float* L, float* R, int n) {
    if (!track_filter_any_active(f)) return;
    for (int i = 0; i < n; ++i) {
        if ((f->ctlCounter++ & (TRACK_FILTER_CTL_INTERVAL - 1)) == 0) {
            f->curFrequency += TRACK_FILTER_SLEW * (f->frequency - f->curFrequency);
            f->curQ         += TRACK_FILTER_SLEW * (f->q         - f->curQ);
            // Settled? Snap and stop rebuilding (steady state costs nothing).
            if (fabsf(f->frequency - f->curFrequency) < 0.01f &&
                fabsf(f->q - f->curQ) < 0.001f) {
                f->curFrequency = f->frequency;
                f->curQ = f->q;
            }
            track_filter_rebuild_coeffs(f, sampleRate);
        }
        track_filter_process(f, L + i, R + i);
    }
}

 
typedef enum {
    GRID_1_16 = 0,
    GRID_1_16T,
    GRID_1_32,
    GRID_1_32T
} GridDivision;

typedef struct {
    CRITICAL_SECTION lock;

    AudioSample samples[MAX_SAMPLES];
    int sampleCount;
    Clip clips[MAX_CLIPS];
    int clipCount;

    int trackCount;
    bool trackMuted[MAX_TRACKS];
    bool trackSolo[MAX_TRACKS];
    float trackVolume[MAX_TRACKS];
    float trackPan[MAX_TRACKS];
    float trackWidth[MAX_TRACKS];
    // Per-track external sidechain routing. -1 = internal (self) detection,
    // otherwise the index of the track whose pre-FX signal ducks this track.
    int8_t trackSidechainSource[MAX_TRACKS];
    // Per-track global trigger probability (0.0f..1.0f, default 1.0f) applied
    // to every clip/note-on on the track, plus a dedicated lock-free xorshift32
    // RNG state per track (initialized to (track_index * 1337) + 1). The PRNG
    // state is deliberately per-track and seeded deterministically so playback
    // and export agree bit-for-bit given the same state.
    float    trackTriggerProb[MAX_TRACKS];
    uint32_t trackRngState[MAX_TRACKS];
    TrackTheme trackThemes[MAX_TRACKS];

    ClipboardItem clipboard[MAX_CLIPS];
    int clipboardCount;
    UndoSnapshot undoStack[MAX_UNDO_STATES], redoStack[MAX_UNDO_STATES];
    int undoCount, redoCount;

    float bpm, swing, zoom;
    bool quantizeEnabled, isLofi, playFromStartOnPlay;
     
    volatile LONG isPlaying;
     
    int visibleBarCount;
    int gridDivision;  
    int timeSigNum, timeSigDen;

     
    char currentProjectName[MAX_PATH];
    char currentProjectFile[MAX_PATH];
    int  exportBitDepth;  
    bool isModified;

    volatile LONG playbackFrame;

     
    double   visualPlayheadFrame;
    LONGLONG visualSyncQPC;
    LONG     visualSyncFrame;
    volatile LONG visualPlayheadLock;

    ma_device device;
    bool deviceInitialized;

    int scrollX, scrollY;
    char exportMsg[96];
    bool exportMsgActive;
    ULONGLONG exportMsgExpiry;    

    ULONGLONG rateUndoDebounceTimer;  // 0 = no pending undo, >0 = expiry time in ms
     
    volatile LONG isBusy;           
    volatile LONG jobProgress;      
    volatile LONG jobKind;          
    char          jobPath[MAX_PATH];

    int draggedClipIndex, dragOrigTrack, marqueeStartX, marqueeStartY, marqueeCurX, marqueeCurY;
    float dragStartBeatOffset, resizeOrigStartBeat, resizeOrigLengthBeats, dragStartVolume;
    ma_uint64 resizeOrigOffsetFrames, dragStartSampleOffset;
    int dragStartX, dragStartY;
    bool isDraggingClip, isCtrlDuplicating, hasMovedPastThreshold, isVolumeDragging, isSlipDragging, isMarqueeSelecting;
    bool isResizingLeft, isResizingRight, isFadeInDragging, isFadeOutDragging;
    bool isStretchResizing;   // Shift+right-edge resize: rate adjusts with length
    float resizeOrigRate;     // playback rate at stretch-resize start

    bool isMiddlePanning;
    int panStartX, panStartY, panOrigScrollX, panOrigScrollY;
    int volumePopupClip, hoveredClip, mouseX, mouseY;
    ULONGLONG volumePopupExpiry;  

    // Track-header drag reorder state. Reordering requires Shift+drag so it
    // never conflicts with the plain click-to-mute binding.
    bool  isTrackHeaderDragging;
    int   dragTrackOrig;          // track index where the drag started
    int   dragTrackCur;           // current target track index during the drag

    // Duplicate-select mode: toggled by the DUP button. While active the
    // timeline greys out and the hovered track highlights in color; clicking
    // a track duplicates it and exits the mode.
    bool  isDupSelectMode;

    // Track the most recently clicked timeline track so spawned synth clips
    // and Media-Explorer "Add to Canvas" land on it (defaults to 0 = track 1).
    int   lastClickedTrack;

    float lofiLpL, lofiLpR;

     
    int   lofiBitDepth;       
    float lofiSampleRate;     

     
    float masterVolume;    
    int   masterMode;      

     
    SmoothEQ3 trackEQ[MAX_TRACKS];
    float trackEqLow[MAX_TRACKS], trackEqMid[MAX_TRACKS], trackEqHigh[MAX_TRACKS];
    bool trackEqActive[MAX_TRACKS];
    PeakBiquad trackPeak[MAX_TRACKS][3];
    float trackEqFreq[MAX_TRACKS][3], trackEqQ[MAX_TRACKS][3];

    // Per-track filter plotter (one biquad per track, before the FX chain)
    TrackFilter trackFilter[MAX_TRACKS];

    bool   pendingPlayheadSet;
    LONG   pendingPlayheadFrame;   

    LONG   origPlaybackFrame;     
    bool   shouldRevertPlayhead;  

     
    int    pendingSingleSelectClip;

     
    bool   ctrlClickOrigSelected;

     
     
    TrackAudioHot trackAudio[MAX_TRACKS];
     
    TrackUICold   trackUI[MAX_TRACKS];

     
    uint16_t barToClip[MAX_TRACKS][MAX_BARS];

    BarBitfield  barPresence;    
    BarBitfield  barDirty;       
    BarBitfield  barValid;       

    TrackMask128 soloMask;       
    TrackMask128 muteMask;       
    TrackMask128 hasAudioMask;   
    TrackMask128 activeMask;     

} SequencerState;

 
#define MIDI_DRAG_NONE      0
#define MIDI_DRAG_MOVE      1
#define MIDI_DRAG_RESIZE_R  2
#define MIDI_DRAG_RESIZE_L  3
#define MIDI_DRAG_MARQUEE   4

typedef struct {
    HWND  hwnd;
    int   clipIdx;
    int   selNote;               

    // Which roll flavor is being edited: 0 = standard MIDI, 1 = Quadrum drum
    // voices (8 fixed rows, no octaves), 2 = Halo pure synth (no sample UI).
    int   editKind;
    int   dragNote;              
    int   dragMode;              
    float dragGrabOffset;        

     
    int   auditionNote;          
    bool  isAuditionPlaying;     
    // True while a key/voice is held for audition. Distinct from the note
    // pitch because Quadrum's Kick is voice 0, which would collide with the
    // "no note" sentinel if we keyed off auditionNote > 0 alone.
    bool  auditionHeld;          
    double auditionPlayheadBeat;
    int   octaveShift;           

    // Polyphonic keyboard audition: up to 8 simultaneously held keys (QWERTY
    // mapping). auditionNotes/auditionNoteCount is the effective held set
    // (mouse + keyboard union) consumed by the audio thread; auditionNote /
    // auditionHeld above stay as the "last pressed" note for paint
    // highlighting.
    int   auditionNotes[MIDI_KB_MAX];
    int   auditionNoteCount;
    int   kbHeldNotes[MIDI_KB_MAX];  // keyboard-held pitches (resolved at press time)
    int   kbHeldVKs[MIDI_KB_MAX];    // physical key per held pitch, so key-up
                                     // matches even if the octave changed mid-hold
    int   kbHeldCount;
    int   mousePitch;                // pitch held by the mouse key strip, -1 none           

     
    bool  isMarqueeSelecting;
    int   marqueeStartX, marqueeStartY, marqueeCurX, marqueeCurY;

     
    bool  isDraggingNote, isResizingLeft, isResizingRight, isCtrlDuplicating, hasMovedPastThreshold;
    int   dragStartX, dragStartY;
    float dragStartBeatOffset, dragLeadBeatOrig;
    int   dragLeadPitchOrig;

     
    MidiNote copyNotes[MIDI_MAX_NOTES];
    int      copyCount;

     
    int      adsrDragKnob;         
    int      adsrDragStartY;
    float    adsrDragStartVal;

     
    int      pendingSingleSelectNote;
} MidiEditContext;

extern HWND             g_midiHwnd;
extern MidiEditContext  g_midiEdit;

 
extern CRITICAL_SECTION g_midiLock;

static inline bool midi_editor_is_open(void) {
    return (g_midiHwnd != NULL && IsWindow(g_midiHwnd));
}

// --- Shared polyphonic audition set (MIDI/Quadrum/Halo piano rolls) --------
// The audio thread consumes g_midiEdit.auditionNotes[] (the union of the
// mouse-held pitch and all keyboard-held pitches, up to MIDI_KB_MAX). Every
// helper below must run under midi_lock.

static inline void midi_audition_rebuild_poly(void) {
    g_midiEdit.auditionNoteCount = 0;
    if (g_midiEdit.mousePitch >= 0)
        g_midiEdit.auditionNotes[g_midiEdit.auditionNoteCount++] = g_midiEdit.mousePitch;
    for (int i = 0; i < g_midiEdit.kbHeldCount && g_midiEdit.auditionNoteCount < MIDI_KB_MAX; ++i) {
        int p = g_midiEdit.kbHeldNotes[i];
        bool dup = false;
        for (int j = 0; j < g_midiEdit.auditionNoteCount; ++j) {
            if (g_midiEdit.auditionNotes[j] == p) { dup = true; break; }
        }
        if (!dup) g_midiEdit.auditionNotes[g_midiEdit.auditionNoteCount++] = p;
    }
    g_midiEdit.auditionHeld = (g_midiEdit.auditionNoteCount > 0);
    g_midiEdit.auditionNote = g_midiEdit.auditionHeld
        ? g_midiEdit.auditionNotes[g_midiEdit.auditionNoteCount - 1]
        : 0;
}

// Keyboard entries are keyed by the physical key (VK), not the pitch: the
// pitch is resolved at press time, so an octave change while a key is held
// must not break the key-up match (a pitch-matched remove would strand the
// held note until some later key-up re-resolved to it).
static inline void midi_audition_kb_add(int vk, int pitch) {
    for (int i = 0; i < g_midiEdit.kbHeldCount; ++i) {
        if (g_midiEdit.kbHeldVKs[i] == vk) return;   // key already held
    }
    if (g_midiEdit.kbHeldCount >= MIDI_KB_MAX) {
        memmove(&g_midiEdit.kbHeldNotes[0], &g_midiEdit.kbHeldNotes[1],
                sizeof(int) * (MIDI_KB_MAX - 1));
        memmove(&g_midiEdit.kbHeldVKs[0], &g_midiEdit.kbHeldVKs[1],
                sizeof(int) * (MIDI_KB_MAX - 1));
        g_midiEdit.kbHeldCount = MIDI_KB_MAX - 1;
    }
    g_midiEdit.kbHeldVKs[g_midiEdit.kbHeldCount] = vk;
    g_midiEdit.kbHeldNotes[g_midiEdit.kbHeldCount++] = pitch;
    midi_audition_rebuild_poly();
}

static inline void midi_audition_kb_remove(int vk) {
    for (int i = 0; i < g_midiEdit.kbHeldCount; ++i) {
        if (g_midiEdit.kbHeldVKs[i] == vk) {
            for (int j = i; j < g_midiEdit.kbHeldCount - 1; ++j) {
                g_midiEdit.kbHeldNotes[j] = g_midiEdit.kbHeldNotes[j + 1];
                g_midiEdit.kbHeldVKs[j]   = g_midiEdit.kbHeldVKs[j + 1];
            }
            g_midiEdit.kbHeldCount--;
            break;
        }
    }
    midi_audition_rebuild_poly();
}

static inline void midi_audition_set_mouse(int pitch) {
    g_midiEdit.mousePitch = (pitch < 0) ? -1 : pitch;
    midi_audition_rebuild_poly();
}

static inline void midi_audition_clear_poly(void) {
    g_midiEdit.kbHeldCount = 0;
    g_midiEdit.mousePitch = -1;
    midi_audition_rebuild_poly();
}

// --- Granular engine audition set (mirror of the above for the engine) -----
// Same union model; must run under seq_lock.
static inline void gran_audition_rebuild(GranularEngine* e) {
    if (!e) return;
    e->auditionNoteCount = 0;
    if (e->mouseNote >= 0)
        e->auditionNotes[e->auditionNoteCount++] = e->mouseNote;
    for (int i = 0; i < e->kbHeldCount && e->auditionNoteCount < MIDI_KB_MAX; ++i) {
        int p = e->kbHeldNotes[i];
        bool dup = false;
        for (int j = 0; j < e->auditionNoteCount; ++j) {
            if (e->auditionNotes[j] == p) { dup = true; break; }
        }
        if (!dup) e->auditionNotes[e->auditionNoteCount++] = p;
    }
    e->auditionNote = (e->auditionNoteCount > 0)
        ? e->auditionNotes[e->auditionNoteCount - 1] : 0;
    if (e->auditionSpawnIdx >= e->auditionNoteCount) e->auditionSpawnIdx = 0;
}

// Same VK-keyed model as the MIDI roll above: entries are matched by the
// physical key so an octave change mid-hold can't strand a held note.
static inline void gran_audition_kb_add(GranularEngine* e, int vk, int pitch) {
    if (!e) return;
    for (int i = 0; i < e->kbHeldCount; ++i) {
        if (e->kbHeldVKs[i] == vk) return;
    }
    if (e->kbHeldCount >= MIDI_KB_MAX) {
        memmove(&e->kbHeldNotes[0], &e->kbHeldNotes[1], sizeof(int) * (MIDI_KB_MAX - 1));
        memmove(&e->kbHeldVKs[0], &e->kbHeldVKs[1], sizeof(int) * (MIDI_KB_MAX - 1));
        e->kbHeldCount = MIDI_KB_MAX - 1;
    }
    e->kbHeldVKs[e->kbHeldCount] = vk;
    e->kbHeldNotes[e->kbHeldCount++] = pitch;
    gran_audition_rebuild(e);
}

static inline void gran_audition_kb_remove(GranularEngine* e, int vk) {
    if (!e) return;
    for (int i = 0; i < e->kbHeldCount; ++i) {
        if (e->kbHeldVKs[i] == vk) {
            for (int j = i; j < e->kbHeldCount - 1; ++j) {
                e->kbHeldNotes[j] = e->kbHeldNotes[j + 1];
                e->kbHeldVKs[j]   = e->kbHeldVKs[j + 1];
            }
            e->kbHeldCount--;
            break;
        }
    }
    gran_audition_rebuild(e);
}

static inline void gran_audition_set_mouse(GranularEngine* e, int pitch) {
    if (!e) return;
    e->mouseNote = (pitch < 0) ? -1 : pitch;
    gran_audition_rebuild(e);
}

static inline void gran_audition_clear(GranularEngine* e) {
    if (!e) return;
    e->kbHeldCount = 0;
    e->mouseNote = -1;
    gran_audition_rebuild(e);
}

// QWERTY piano-roll keyboard mapping: white keys A S D F G H J K L, black
// keys W E T Y U O P (C C# D D# E F F# G G# A A# B C C# D D#). Returns the
// semitone offset (0-15) for the pressed key, or -1 when unmapped. Layout
// aware via MapVirtualKey so AZERTY keyboards play the note printed on the
// key. Callers gate on their own Ctrl/Shift/Alt checks.
static inline int pr_key_to_semitone(int vkCode) {
    int ch = (int)(MapVirtualKeyA((UINT)vkCode, MAPVK_VK_TO_CHAR) & 0xFF);
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    switch (ch) {
        case 'a': return 0;  case 'w': return 1;
        case 's': return 2;  case 'e': return 3;
        case 'd': return 4;  case 'f': return 5;
        case 't': return 6;  case 'g': return 7;
        case 'y': return 8;  case 'h': return 9;
        case 'u': return 10; case 'j': return 11;
        case 'k': return 12; case 'o': return 13;
        case 'l': return 14; case 'p': return 15;
        default:  return -1;
    }
}