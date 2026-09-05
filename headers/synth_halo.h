#ifndef HALO_ENGINE_H
#define HALO_ENGINE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <immintrin.h>
#include <windows.h>
#include "config.h"

#define HALO_SR          44100
#define HALO_MAX_VOICES  8
#define HALO_MAX_PARTIALS 12
#define HALO_BLOCK_SIZE  64
#define HALO_PRESET_COUNT 8
#define HALO_MAX_SAMPLES (HALO_SR * 2)

#ifndef HALO_PI
#define HALO_PI          3.14159265358979323846
#endif
#ifndef HALO_TWO_PI
#define HALO_TWO_PI      6.28318530717958647692
#endif

/* Global count of simultaneously active Halo voices across all clips. Bounds
   worst-case audio-thread CPU load; new notes are dropped when the cap is
   reached. Defined in main.c, mutated only on the audio thread. */
extern volatile LONG g_haloActiveVoices;

/* ========================================================================
   Numerical Safety Layer (Denormal Hardening & FTZ/DAZ)
   ======================================================================== */

static inline double kill_denorm(double x) {
    return (fabs(x) < 1e-15) ? 0.0 : x;
}

static inline float kill_denorm_f(float x) {
    return (fabsf(x) < 1e-15f) ? 0.0f : x;
}

typedef struct {
    unsigned int old_mxcsr;
} DenormalGuard;

static inline void denormal_guard_enter(DenormalGuard* g) {
    g->old_mxcsr = _mm_getcsr();
    /* Bit 15: Flush-to-Zero (FTZ), Bit 6: Denormals-Are-Zero (DAZ) */
    _mm_setcsr(g->old_mxcsr | 0x8040);
}

static inline void denormal_guard_leave(DenormalGuard* g) {
    _mm_setcsr(g->old_mxcsr);
}

static inline __m128 halo_mm_clamp_sym_ps(__m128 val, __m128 limit) {
    __m128 neg_limit = _mm_sub_ps(_mm_setzero_ps(), limit);
    return _mm_min_ps(_mm_max_ps(val, neg_limit), limit);
}

/* ========================================================================
   PRNG & Noise Generators
   ======================================================================== */

typedef struct {
    uint64_t state;
} HaloPRNG;

static inline void halo_prng_init(HaloPRNG* rng, uint64_t seed) {
    rng->state = seed ? seed : 0x853C49E6748FEA9BULL;
}

static inline uint64_t xorshift64(HaloPRNG* rng) {
    uint64_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return rng->state = x;
}

static inline float halo_prng_next_f(HaloPRNG* rng) {
    return (float)((double)(xorshift64(rng) >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0);
}

#define PINK_STAGES 7
typedef struct {
    double state[PINK_STAGES];
    double coeffs[PINK_STAGES];
    HaloPRNG rng;
} HaloPinkNoise;

static inline void pink_init(HaloPinkNoise* p, uint64_t seed) {
    halo_prng_init(&p->rng, seed);
    for (int i = 0; i < PINK_STAGES; i++) {
        p->state[i] = 0.0;
        p->coeffs[i] = 0.5 / (double)(1 << (i + 1));
    }
}

static inline double pink_tick(HaloPinkNoise* p) {
    double white = (double)halo_prng_next_f(&p->rng);
    double out = 0.0;
    for (int i = 0; i < PINK_STAGES; i++) {
        p->state[i] = p->coeffs[i] * white + (1.0 - p->coeffs[i]) * p->state[i];
        out += p->state[i];
        p->state[i] = kill_denorm(p->state[i]);
    }
    return out;
}

/* ========================================================================
   Envelopes & Voice Shaping
   ======================================================================== */

typedef enum {
    ADSR_IDLE = 0,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
} ADSRState;

typedef struct {
    double attack;
    double decay;
    double sustain;
    double release;
    double level;
    double sampleRate;
    ADSRState state;
    double attack_step;
    double decay_k;
    double release_k;
} ADSREnvelope;

static inline void adsr_init(ADSREnvelope* env, double sr) {
    env->attack = 0.01;
    env->decay = 0.15;
    env->sustain = 0.70;
    env->release = 0.35;
    env->level = 0.0;
    env->sampleRate = sr;
    env->state = ADSR_IDLE;
    env->attack_step = 1.0;
    env->decay_k = 0.0;
    env->release_k = 0.0;
}

static inline void adsr_recompute_rates(ADSREnvelope* env) {
    double dt = 1.0 / env->sampleRate;
    env->attack_step = (env->attack < 0.001) ? 1.0 : (dt / env->attack);
    env->decay_k   = (env->decay   < 0.001) ? 0.0 : exp(-4.0 * dt / env->decay);
    env->release_k = (env->release < 0.001) ? 0.0 : exp(-4.0 * dt / env->release);
}

static inline void adsr_gate(ADSREnvelope* env, int on) {
    if (on) {
        env->state = ADSR_ATTACK;
    } else {
        if (env->state != ADSR_IDLE) {
            env->state = ADSR_RELEASE;
        }
    }
}

static inline double adsr_tick(ADSREnvelope* env) {
    switch (env->state) {
        case ADSR_ATTACK: {
            env->level += env->attack_step;
            if (env->level >= 1.0) {
                env->level = 1.0;
                env->state = ADSR_DECAY;
            }
            break;
        }
        case ADSR_DECAY: {
            env->level = env->sustain + (env->level - env->sustain) * env->decay_k;
            if (env->level <= env->sustain + 1e-4) {
                env->level = env->sustain;
                env->state = ADSR_SUSTAIN;
            }
            break;
        }
        case ADSR_SUSTAIN:
            env->level = env->sustain;
            break;
        case ADSR_RELEASE: {
            env->level *= env->release_k;
            if (env->level <= 1e-4) {
                env->level = 0.0;
                env->state = ADSR_IDLE;
            }
            break;
        }
        default:
            env->level = 0.0;
            break;
    }
    env->level = kill_denorm(env->level);
    return env->level;
}

/* ========================================================================
   Filters: Zero-Delay Feedback (TPT) State-Variable Filter
   ======================================================================== */

typedef struct {
    double s1, s2;
    int    type;
    double drive_warmth;
} SVFilter;

static inline void svf_reset(SVFilter* f) {
    f->s1 = 0.0;
    f->s2 = 0.0;
}

static inline void svf_set_type(SVFilter* f, int type) {
    f->type = (type >= 0 && type <= 3) ? type : 0;
}

static inline double svf_tick(SVFilter* f, double in, double fc, double Q, int type) {
    if (fc < 20.0) fc = 20.0;
    if (fc > HALO_SR * 0.48) fc = HALO_SR * 0.48;
    if (Q < 0.5) Q = 0.5;

    double g = tan(HALO_PI * fc / HALO_SR);
    double k = 1.0 / Q;
    double a1 = 1.0 / (1.0 + g * (g + k));
    double a2 = g * a1;
    double a3 = g * a2;

    double v0 = in;
    double v1 = a1 * f->s1 + a2 * (v0 - f->s2);
    double v2 = f->s2 + a3 * (v0 - f->s2) + a2 * f->s1;

    f->s1 = kill_denorm(2.0 * v1 - f->s1);
    f->s2 = kill_denorm(2.0 * v2 - f->s2);

    switch (type) {
        case 1:  return v0 - k * v1 - v2;   /* HP */
        case 2:  return v1 + v0 * 0.35;     /* BP with dry body blend (never drops silent) */
        case 0:
        case 3:
        default: return v2;                 /* LP */
    }
}

/* ========================================================================
   Nonlinear Saturation & Analog Warmth
   ======================================================================== */

static inline double warm_saturate(double x, double drive) {
    if (drive <= 1.02) return x;
    double d = 1.0 + (drive - 1.0) * 0.35;
    return kill_denorm(tanh(x * d) / tanh(d));
}

/* ========================================================================
   Anti-Aliased Carrier Waveforms (Sine, Triangle, Square, Saw)
   ======================================================================== */

/* Two-sample polyBLEP residual function for step discontinuities */
static inline double halo_poly_blep(double phase, double dt) {
    if (dt < 1e-7) return 0.0;
    if (phase < dt) {
        double t = phase / dt;
        return t + t - t * t - 1.0;
    } else if (phase > 1.0 - dt) {
        double t = (phase - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

/* Discrete carrier waveform evaluator:
   0 = Sine, 1 = Triangle, 2 = Square (level-matched), 3 = Saw (original BLEP saw). */
static inline double halo_waveform(int type, double phase, double dt_phase) {
    switch (type) {
        case 0: /* Sine */
            return sin(phase * HALO_TWO_PI);

        case 1: { /* Triangle (continuous, zero-crossing matched with sine) */
            double tri;
            if (phase < 0.25) {
                tri = 4.0 * phase;
            } else if (phase < 0.75) {
                tri = 2.0 - 4.0 * phase;
            } else {
                tri = 4.0 * phase - 4.0;
            }
            return tri;
        }

        case 2: { /* Square (anti-aliased on both edges, scaled to match perceived loudness) */
            double sq = (phase < 0.5) ? 1.0 : -1.0;
            sq += halo_poly_blep(phase, dt_phase);
            double p05 = phase - 0.5;
            if (p05 < 0.0) p05 += 1.0;
            sq -= halo_poly_blep(p05, dt_phase);
            return sq * 0.75; /* Gain compensation: keeps loudness aligned with saw & sine */
        }

        case 3: /* Saw (exact polyBLEP implementation from original engine) */
        default: {
            double saw = 2.0 * phase - 1.0;
            saw += halo_poly_blep(phase, dt_phase);
            return saw;
        }
    }
}

/* Continuous "wave" knob morpher: smoothly blends between adjacent waveforms */
static inline double halo_waveform_morph(double wave_val, double phase, double dt_phase) {
    if (wave_val <= 0.0) return halo_waveform(0, phase, dt_phase);
    if (wave_val >= 3.0) return halo_waveform(3, phase, dt_phase);

    int i0 = (int)wave_val;
    int i1 = i0 + 1;
    double frac = wave_val - (double)i0;
    if (frac < 1e-4) return halo_waveform(i0, phase, dt_phase);

    double w0 = halo_waveform(i0, phase, dt_phase);
    double w1 = halo_waveform(i1, phase, dt_phase);
    return w0 * (1.0 - frac) + w1 * frac;
}

/* ========================================================================
   LFO Module
   ======================================================================== */

typedef struct {
    double phase;
    double frequency;
    double sampleRate;
    double output;
} HaloLFO;

static inline void lfo_init(HaloLFO* l, double sr) {
    l->phase = 0.0;
    l->frequency = 2.0;
    l->sampleRate = sr;
    l->output = 0.0;
}

static inline double lfo_tick_tri(HaloLFO* l) {
    l->phase += HALO_TWO_PI * l->frequency / l->sampleRate;
    if (l->phase >= HALO_TWO_PI) l->phase -= HALO_TWO_PI;
    double norm = l->phase / HALO_PI;
    double val = (norm <= 1.0) ? (norm * 2.0 - 1.0) : (3.0 - norm * 2.0);
    l->output = kill_denorm(val);
    return l->output;
}

/* ========================================================================
   Additive Partial Engine & Oscillators
   ======================================================================== */

typedef struct {
    double freqRatio;
    double amplitude;
    double phase;
    double decayRate;
    double state;
} HaloPartial;

typedef struct {
    HaloPartial partials[HALO_MAX_PARTIALS];
    int count;
    double tilt_gain[HALO_MAX_PARTIALS];
    double inharm_cache;
    double decay_cache;
    double tilt_cache;
} AdditiveEngine;

static inline void additive_init(AdditiveEngine* add) {
    add->count = 8;
    add->inharm_cache = -1.0;
    add->decay_cache  = -1.0;
    add->tilt_cache   = -1.0;
    for (int i = 0; i < HALO_MAX_PARTIALS; i++) {
        add->partials[i].freqRatio = (double)(i + 1);
        add->partials[i].amplitude = 1.0 / (double)(i + 1);
        add->partials[i].phase = 0.0;
        add->partials[i].decayRate = 1.0;
        add->partials[i].state = 1.0;
        add->tilt_gain[i] = 1.0;
    }
}

static inline void additive_reset_env(AdditiveEngine* add) {
    for (int i = 0; i < HALO_MAX_PARTIALS; i++) {
        add->partials[i].phase = 0.0;
        add->partials[i].state = 1.0;
    }
}

static inline double spectral_bank_tick(AdditiveEngine* add, double fundamental, double dt,
                                        double tilt, double inharm, double brightness, int max_partials) {
    double out = 0.0;
    double energy = 1.0;
    double nyquist = HALO_SR * 0.48;

    for (int i = 0; i < max_partials; i++) {
        double n = (double)(i + 1);
        double freq_mult = n * sqrt(1.0 + inharm * (n * n - 1.0) * 0.05);
        double freq = fundamental * freq_mult;
        if (freq >= nyquist) break;

        HaloPartial* p = &add->partials[i];
        p->phase += HALO_TWO_PI * freq * dt;
        if (p->phase >= HALO_TWO_PI) p->phase -= HALO_TWO_PI;

        double amp = pow(1.0 / n, 1.0 - tilt * 0.5) * exp(-n * (1.0 - brightness) * 0.4);
        amp *= exp(-0.18 * (double)i);
        energy += amp * amp;
        out += sin(p->phase) * amp;
    }
    return kill_denorm(out / sqrt(energy));
}

/* ========================================================================
   Synth Parameters & Preset Architecture
   ======================================================================== */

typedef struct {
    /* OSC / FM */
    double pitch_semi;      /* -24.0 .. +24.0 semitones */
    double waveform;        /* 0=sine, 1=triangle, 2=square, 3=saw (continuous morph) */
    double fm_ratio;        /* 0.5 .. 8.0 multiplier */
    double fm_depth;        /* 0.0 .. 6.0 modulation index */
    double fm_feedback;     /* 0.0 .. 1.0 feedback */
    double osc_mix;         /* 0.0 .. 1.0 FM carrier vs additive bank blend */
    double detune;          /* 0.0 .. 50.0 cents between carrier & partial bank */
    double unison_voices;   /* 1 .. 8 detuned carrier copies */
    double unison_spread;   /* 0.0 .. 50.0 cents of unison detune */

    /* ADDITIVE / NOISE */
    double partial_count;   /* 1.0 .. 12.0 active partials */
    double partial_tilt;    /* -2.0 .. +2.0 spectral slope */
    double noise_mix;       /* 0.0 .. 1.0 noise balance */
    double noise_cutoff;    /* 100.0 .. 16000.0 Hz noise tone */
    double harm_decay;      /* 0.0 .. 1.0 per-partial spectral decay */
    double inharm;          /* 0.0 .. 1.0 inharmonic stretch (bell) */

    /* FILTER / DRIVE */
    double filter_cutoff;   /* 50.0 .. 18000.0 Hz */
    double filter_q;        /* 0.5 .. 20.0 resonance Q */
    double filter_drive;    /* 1.0 .. 8.0 pre-filter saturation gain */
    double drive;           /* 1.0 .. 6.0 saturation drive */
    double filter_type;     /* 0.0=LP, 1.0=HP, 2.0=BP, 3.0=SVF */
    double lfo_filt_depth;  /* 0.0 .. 2000.0 Hz LFO to cutoff depth */
    double key_track;       /* 0.0 .. 1.0 cutoff key tracking */

    /* ENVELOPE / MOD */
    double amp_attack;      /* 0.002 .. 2.0 seconds */
    double amp_decay;       /* 0.05 .. 4.0 seconds decay */
    double amp_release;     /* 0.02 .. 6.0 seconds release */
    double filter_env_depth;/* -1.0 .. +1.0 envelope to cutoff */
    double lfo_rate;        /* 0.1 .. 20.0 Hz */
    double amp_sustain;     /* 0.0 .. 1.0 sustain level */
    double vibrato;         /* 0.0 .. 100.0 cents LFO pitch wobble */
} HaloPatch;

static const char* HALO_PRESET_NAMES[HALO_PRESET_COUNT] = {
    "Obsidian Pad", "Solar Lead", "Prism Bell", "Sub Bass",
    "Vocal Shimmer", "Amber Brass", "Cyber Pluck", "Cosmic Drift"
};

static void halo_get_preset(int index, HaloPatch* p) {
    p->pitch_semi        = 0.0;
    p->waveform          = 0.0;     /* 0 = Sine carrier */
    p->fm_ratio          = 1.0;
    p->fm_depth          = 0.25;
    p->fm_feedback       = 0.08;
    p->osc_mix           = 0.70;
    p->detune            = 4.0;
    p->unison_voices     = 4.0;
    p->unison_spread     = 24.0;

    p->partial_count     = 8.0;
    p->partial_tilt      = -0.5;
    p->noise_mix         = 0.02;
    p->noise_cutoff      = 3200.0;
    p->harm_decay        = 0.20;
    p->inharm            = 0.0;

    p->filter_cutoff     = 2200.0;
    p->filter_q          = 1.1;
    p->filter_drive      = 1.0;
    p->drive             = 1.15;
    p->filter_type       = 3.0;     /* SVF low-pass */
    p->lfo_filt_depth    = 350.0;
    p->key_track         = 0.40;

    p->amp_attack        = 0.06;
    p->amp_decay         = 0.80;
    p->amp_release       = 0.65;
    p->filter_env_depth  = 0.35;
    p->lfo_rate          = 0.8;
    p->amp_sustain       = 0.75;
    p->vibrato           = 0.0;

    switch (index) {
        case 1: /* Solar Lead */
            p->fm_ratio = 1.0;
            p->fm_depth = 0.4;
            p->fm_feedback = 0.1;
            p->osc_mix = 0.35;
            p->detune = 18.0;
            p->waveform = 3.0;      /* Saw carrier */
            p->unison_voices = 6.0;
            p->unison_spread = 22.0;
            p->partial_count = 10.0;
            p->partial_tilt = -0.5;
            p->noise_mix = 0.08;
            p->noise_cutoff = 2200.0;
            p->harm_decay = 0.0;
            p->inharm = 0.10;
            p->filter_cutoff = 1600.0;
            p->filter_q = 1.2;
            p->filter_drive = 1.0;
            p->drive = 1.5;
            p->filter_type = 3.0;
            p->lfo_filt_depth = 400.0;
            p->key_track = 0.20;
            p->amp_attack = 0.65;
            p->amp_decay = 1.80;
            p->amp_release = 2.40;
            p->filter_env_depth = 0.35;
            p->lfo_rate = 0.8;
            p->amp_sustain = 0.80;
            p->vibrato = 3.0;
            break;

        case 2: /* Prism Bell */
            p->pitch_semi = 12.0;
            p->fm_ratio = 3.5;
            p->fm_depth = 2.8;
            p->fm_feedback = 0.3;
            p->osc_mix = 0.75;
            p->detune = 2.0;
            p->waveform = 3.0;      /* Saw harmonic base */
            p->unison_voices = 2.0;
            p->unison_spread = 7.0;
            p->partial_count = 12.0;
            p->partial_tilt = 0.6;
            p->noise_mix = 0.02;
            p->noise_cutoff = 8000.0;
            p->harm_decay = 0.55;
            p->inharm = 0.65;
            p->filter_cutoff = 5500.0;
            p->filter_q = 3.0;
            p->filter_drive = 1.0;
            p->drive = 1.2;
            p->filter_type = 0.0;
            p->lfo_filt_depth = 150.0;
            p->key_track = 0.30;
            p->amp_attack = 0.003;
            p->amp_decay = 1.20;
            p->amp_release = 1.80;
            p->filter_env_depth = 0.40;
            p->lfo_rate = 3.0;
            p->amp_sustain = 0.10;
            p->vibrato = 0.0;
            break;

        case 3: /* Sub Bass */
            p->pitch_semi = -12.0;
            p->fm_ratio = 1.0;
            p->fm_depth = 0.2;
            p->fm_feedback = 0.15;
            p->osc_mix = 0.45;
            p->detune = 3.0;
            p->waveform = 2.0;      /* Square (now warm, anti-aliased, and punchy) */
            p->unison_voices = 2.0;
            p->unison_spread = 4.0;
            p->partial_count = 2.0;
            p->partial_tilt = -1.2;
            p->noise_mix = 0.01;
            p->noise_cutoff = 800.0;
            p->harm_decay = 0.10;
            p->inharm = 0.0;
            p->filter_cutoff = 420.0;
            p->filter_q = 1.8;
            p->filter_drive = 2.5;
            p->drive = 3.2;
            p->filter_type = 0.0;
            p->lfo_filt_depth = 60.0;
            p->key_track = 0.10;
            p->amp_attack = 0.008;
            p->amp_decay = 0.35;
            p->amp_release = 0.18;
            p->filter_env_depth = 0.60;
            p->lfo_rate = 0.5;
            p->amp_sustain = 0.75;
            p->vibrato = 0.0;
            break;

            case 4: /* Vocal Shimmer */
            p->fm_ratio          = 2.0;      /* Octave harmonic for airy upper sparkle */
            p->fm_depth          = 0.45;     /* Delicate shimmer, no harsh sidebands */
            p->fm_feedback       = 0.08;
            p->osc_mix           = 0.40;     /* 60% partial bank to let the harmonics sing */
            p->detune            = 8.0;      /* Gentle chorus detune */
            p->waveform          = 0.35;     /* Sine with a touch of triangle warmth */
            p->unison_voices     = 6.0;      /* Rich choir density */
            p->unison_spread     = 24.0;     /* Wide stereo wash */

            p->partial_count     = 10.0;     /* Extra high partials for crystalline highs */
            p->partial_tilt      = 0.15;     /* Air/treble lift */
            p->noise_mix         = 0.06;     /* Subtle breath/air texture without hiss */
            p->noise_cutoff      = 6500.0;   /* Bright "air" band */
            p->harm_decay        = 0.08;     /* Overtones sustain instead of muting */
            p->inharm            = 0.05;     /* Near-harmonic chime */

            p->filter_cutoff     = 3400.0;   /* Open vocal formant territory */
            p->filter_q          = 1.35;     /* Wide musical band, no volume choking */
            p->filter_drive      = 1.0;      /* Clean & transparent */
            p->drive             = 1.6;      /* Open headroom for highs to breathe */
            p->filter_type       = 2.0;      /* Formant band-pass */
            p->lfo_filt_depth    = 650.0;    /* Sparkling animated sweep */
            p->key_track         = 0.55;     /* Filter tracks played pitch for brightness across keys */

            /* De-enveloped: immediate bloom and holding sustain */
            p->amp_attack        = 0.04;     /* Quick, click-free response */
            p->amp_decay         = 0.60;
            p->amp_sustain       = 0.88;     /* Stays loud and vibrant while held */
            p->amp_release       = 1.60;     /* Lush, floating release tail */
            p->filter_env_depth  = 0.15;     /* Mild accent rather than a huge wah/clamp */
            p->lfo_rate          = 1.8;      /* Slow, liquid modulation instead of frantic wobble */
            p->vibrato           = 4.0;      /* Subtle organic movement */
            break;

        case 5: /* Amber Brass */
            p->fm_ratio = 1.0;
            p->fm_depth = 0.7;
            p->fm_feedback = 0.2;
            p->osc_mix = 0.62;
            p->detune = 10.0;
            p->waveform = 3.0;      /* Classic rich analog Saw brass */
            p->unison_voices = 3.0;
            p->unison_spread = 10.0;
            p->partial_count = 6.0;
            p->partial_tilt = -0.2;
            p->noise_mix = 0.03;
            p->noise_cutoff = 2500.0;
            p->harm_decay = 0.20;
            p->inharm = 0.05;
            p->filter_cutoff = 1900.0;
            p->filter_q = 1.6;
            p->filter_drive = 2.0;
            p->drive = 2.0;
            p->filter_type = 0.0;
            p->lfo_filt_depth = 300.0;
            p->key_track = 0.45;
            p->amp_attack = 0.07;
            p->amp_decay = 0.60;
            p->amp_release = 0.25;
            p->filter_env_depth = 0.70;
            p->lfo_rate = 2.0;
            p->amp_sustain = 0.70;
            p->vibrato = 4.0;
            break;

        case 6: /* Cyber Pluck */
            p->fm_ratio = 2.0;
            p->fm_depth = 2.2;
            p->fm_feedback = 0.4;
            p->osc_mix = 0.58;
            p->detune = 5.0;
            p->waveform = 3.0;      /* Saw pluck */
            p->unison_voices = 2.0;
            p->unison_spread = 6.0;
            p->partial_count = 7.0;
            p->partial_tilt = 0.0;
            p->noise_mix = 0.12;
            p->noise_cutoff = 5000.0;
            p->harm_decay = 0.70;
            p->inharm = 0.15;
            p->filter_cutoff = 3200.0;
            p->filter_q = 2.5;
            p->filter_drive = 1.5;
            p->drive = 2.6;
            p->filter_type = 0.0;
            p->lfo_filt_depth = 180.0;
            p->key_track = 0.20;
            p->amp_attack = 0.002;
            p->amp_decay = 0.22;
            p->amp_release = 0.12;
            p->filter_env_depth = 0.80;
            p->lfo_rate = 6.0;
            p->amp_sustain = 0.0;
            p->vibrato = 0.0;
            break;

        case 7: /* Cosmic Drift */
            p->fm_ratio = 1.414;
            p->fm_depth = 1.8;
            p->fm_feedback = 0.35;
            p->osc_mix = 0.40;
            p->detune = 22.0;
            p->waveform = 0.0;      /* Sine carrier */
            p->unison_voices = 7.0;
            p->unison_spread = 28.0;
            p->partial_count = 12.0;
            p->partial_tilt = -0.1;
            p->noise_mix = 0.10;
            p->noise_cutoff = 4000.0;
            p->harm_decay = 0.05;
            p->inharm = 0.30;
            p->filter_cutoff = 2800.0;
            p->filter_q = 2.0;
            p->filter_drive = 1.0;
            p->drive = 1.9;
            p->filter_type = 3.0;
            p->lfo_filt_depth = 600.0;
            p->key_track = 0.15;
            p->amp_attack = 0.40;
            p->amp_decay = 1.50;
            p->amp_release = 3.20;
            p->filter_env_depth = -0.40;
            p->lfo_rate = 1.2;
            p->amp_sustain = 0.72;
            p->vibrato = 8.0;
            break;

        case 0:
        default:
            break;
    }
}

/* ========================================================================
   Master Stereo Effects: Spectral Dimension / Ensemble
   ======================================================================== */

#define CHORUS_MAX_LFO_PHASE (HALO_TWO_PI)
static const double CHORUS_BASE_DELAY_S = 0.018;
static const double CHORUS_DEPTH_S      = 0.006;
static const double CHORUS_RATE_HZ      = 0.30;
static const double CHORUS_MIX          = 0.35;

typedef struct {
    double lfo_phase;
    double buf_l[(int)(0.001 * HALO_SR) + 1 + (int)(2 * 0.006 * HALO_SR) + 4];
    double buf_r[(int)(0.001 * HALO_SR) + 1 + (int)(2 * 0.006 * HALO_SR) + 4];
    int    buf_size;
    int    write_pos;
} HaloChorus;

static inline int chorus_delay_max_samples(void) {
    return (int)(0.001 * HALO_SR) + 1 + (int)(2 * 0.006 * HALO_SR) + 4;
}

static inline void chorus_init(HaloChorus* ch) {
    ch->lfo_phase = 0.0;
    ch->buf_size = chorus_delay_max_samples();
    ch->write_pos = 0;
    memset(ch->buf_l, 0, sizeof(ch->buf_l));
    memset(ch->buf_r, 0, sizeof(ch->buf_r));
}

static inline double chorus_tap(const double* buf, int size, int write_pos, double delay_s) {
    double d = delay_s * HALO_SR;
    if (d < 0.0) d = 0.0;
    if (d > (double)(size - 2)) d = (double)(size - 2);
    double read = (double)write_pos - d;
    if (read < 0.0) read += (double)size;
    int i0 = (int)read;
    int i1 = (i0 + 1) % size;
    double frac = read - (double)i0;
    return buf[i0] * (1.0 - frac) + buf[i1] * frac;
}

static inline void chorus_process(HaloChorus* ch, float* io, int frames) {
    double dt = 1.0 / HALO_SR;
    for (int i = 0; i < frames; i++) {
        double l = io[2 * i];
        double r = io[2 * i + 1];

        double lfo_s = sin(ch->lfo_phase);
        double lfo_c = cos(ch->lfo_phase);
        double dl = CHORUS_BASE_DELAY_S + CHORUS_DEPTH_S * lfo_s;
        double dr = CHORUS_BASE_DELAY_S + CHORUS_DEPTH_S * lfo_c;

        double wet_l = chorus_tap(ch->buf_l, ch->buf_size, ch->write_pos, dl);
        double wet_r = chorus_tap(ch->buf_r, ch->buf_size, ch->write_pos, dr);

        double out_l = l + wet_l * CHORUS_MIX - l * CHORUS_MIX * 0.3;
        double out_r = r + wet_r * CHORUS_MIX - r * CHORUS_MIX * 0.3;

        ch->buf_l[ch->write_pos] = l;
        ch->buf_r[ch->write_pos] = r;
        ch->write_pos = (ch->write_pos + 1) % ch->buf_size;
        ch->lfo_phase += HALO_TWO_PI * CHORUS_RATE_HZ * dt;
        if (ch->lfo_phase >= CHORUS_MAX_LFO_PHASE) ch->lfo_phase -= CHORUS_MAX_LFO_PHASE;

        io[2 * i]     = (float)out_l;
        io[2 * i + 1] = (float)out_r;
    }
}

/* ========================================================================
   Polyphonic Voice & Voice Manager
   ======================================================================== */

typedef struct {
    double current;
    double target;
    double coef;
} SmoothedParam;

static inline void smooth_param_init(SmoothedParam* p, double initial, double time_sec, double sr) {
    p->current = initial;
    p->target = initial;
    p->coef = 1.0 - exp(-1.0 / (time_sec * sr));
}

static inline double smooth_param_tick(SmoothedParam* p) {
    p->current += p->coef * (p->target - p->current);
    return kill_denorm(p->current);
}

static inline void smooth_param_set(SmoothedParam* p, double v) {
    p->target = v;
}

typedef struct {
    int          active;
    int          note;
    float        velocity;
    double       frequency;
    double       mod_phase;
    double       feedback_sample;
    double       feedback_last;
    double       warm_lp_l;
    double       warm_lp_r;
    double       time_active;

    /* Unison carrier stack */
    double       uni_phase[8];
    double       fm_norm;
    double       uni_detune[8];
    double       uni_gain_l[8];
    double       uni_gain_r[8];
    double       uni_spread_cache;
    int          uni_count;

    /* Smoothed modulation sources */
    SmoothedParam smooth_cutoff;
    SmoothedParam smooth_drive;
    SmoothedParam smooth_fm_depth;

    /* Derived / cached state */
    double       detune_factor;
    double       detune_cache;
    double       cutoff_key_scale;
    double       key_track_cache;
    double       noise_lp_a;
    double       noise_lp_cutoff;
    double       noise_lp_z;

    ADSREnvelope amp_env;
    ADSREnvelope filter_env;
    SVFilter     filter;
    HaloPinkNoise noise;
    AdditiveEngine additive;
    HaloLFO      lfo;
} HaloVoice;

typedef struct {
    HaloVoice voices[HALO_MAX_VOICES];
    double    sample_rate;
    HaloChorus chorus;
    float     limiter_gain;
    float     mix_gain;      // Continuous smoothed mix gain (eliminates polyphony step pops)
    int       tail_frames;   // Chorus drain hangover frames after voices deactivate
} HaloVoiceManager;

static inline void voice_force_idle(HaloVoice* v) {
    if (v->active) {
        InterlockedDecrement(&g_haloActiveVoices);
        v->active = 0;
    }
    v->amp_env.state = ADSR_IDLE;
    v->amp_env.level = 0.0;
    svf_reset(&v->filter);
    v->mod_phase = 0.0;
    v->fm_norm = 0.0;
    v->feedback_sample = 0.0;
    v->feedback_last = 0.0;
    v->time_active = 0.0;
    v->noise_lp_z = 0.0;
    v->warm_lp_l = 0.0;
    v->warm_lp_r = 0.0;
    for (int i = 0; i < 8; i++) v->uni_phase[i] = 0.0;
}

static inline void voice_manager_init(HaloVoiceManager* vm, double sr) {
    vm->sample_rate = sr;
    vm->limiter_gain = 0.0f;
    vm->mix_gain = 1.0f;
    vm->tail_frames = 0;
    chorus_init(&vm->chorus);
    for (int i = 0; i < HALO_MAX_VOICES; i++) {
        HaloVoice* v = &vm->voices[i];
        v->active = 0;
        v->note = -1;
        v->velocity = 0.0f;
        v->frequency = 440.0;
        v->mod_phase = 0.0;
        v->fm_norm = 0.0;
        v->feedback_sample = 0.0;
        v->feedback_last = 0.0;
        v->time_active = 0.0;

        v->detune_factor = 1.0;
        v->detune_cache = -1.0;
        v->cutoff_key_scale = 1.0;
        v->key_track_cache = -1.0;
        v->noise_lp_a = 1.0;
        v->noise_lp_cutoff = -1.0;
        v->noise_lp_z = 0.0;
        for (int u = 0; u < 8; u++) {
            v->uni_phase[u] = 0.0;
            v->uni_detune[u] = 1.0;
            v->uni_gain_l[u] = 1.0;
            v->uni_gain_r[u] = 1.0;
        }
        v->uni_spread_cache = -1.0;
        v->uni_count = 0;

        smooth_param_init(&v->smooth_cutoff, 1000.0, 0.02, sr);
        smooth_param_init(&v->smooth_drive, 1.0, 0.02, sr);
        smooth_param_init(&v->smooth_fm_depth, 0.0, 0.02, sr);

        adsr_init(&v->amp_env, sr);
        adsr_init(&v->filter_env, sr);
        v->filter.type = 0;
        svf_reset(&v->filter);

        pink_init(&v->noise, 1337000ULL + (uint64_t)i * 123ULL);
        additive_init(&v->additive);
        lfo_init(&v->lfo, sr);
    }
}

static inline int voice_manager_active_count(const HaloVoiceManager* vm) {
    int n = 0;
    for (int i = 0; i < HALO_MAX_VOICES; i++) {
        if (vm->voices[i].active) n++;
    }
    return n;
}

static inline HaloVoice* voice_manager_alloc(HaloVoiceManager* vm, int note) {
    // 1. Prioritize completely inactive voices so any releasing voice can
    // ring out smoothly without having its phases reset or its tail cut.
    for (int i = 0; i < HALO_MAX_VOICES; i++) {
        if (!vm->voices[i].active) {
            return &vm->voices[i];
        }
    }

    // 2. If no voices are free, check for an actively sustaining voice on the
    // same note (legato / retrigger). Do NOT steal if in ADSR_RELEASE.
    for (int i = 0; i < HALO_MAX_VOICES; i++) {
        if (vm->voices[i].active && vm->voices[i].note == note &&
            vm->voices[i].amp_env.state != ADSR_RELEASE &&
            vm->voices[i].amp_env.state != ADSR_IDLE) {
            return &vm->voices[i];
        }
    }

    // 3. All voices are active. Prioritize stealing a voice currently in RELEASE,
    // picking the one closest to silence (lowest amp_env.level).
    int steal_idx = -1;
    double min_level = 1e9;
    for (int i = 0; i < HALO_MAX_VOICES; i++) {
        if (vm->voices[i].amp_env.state == ADSR_RELEASE ||
            vm->voices[i].amp_env.state == ADSR_IDLE) {
            if (vm->voices[i].amp_env.level < min_level) {
                min_level = vm->voices[i].amp_env.level;
                steal_idx = i;
            }
        }
    }

    // 4. If all voices are held, steal the oldest voice by time_active.
    if (steal_idx < 0) {
        double max_time = -1.0;
        for (int i = 0; i < HALO_MAX_VOICES; i++) {
            if (vm->voices[i].time_active > max_time) {
                max_time = vm->voices[i].time_active;
                steal_idx = i;
            }
        }
    }

    if (steal_idx >= 0) {
        voice_force_idle(&vm->voices[steal_idx]);
        return &vm->voices[steal_idx];
    }

    return &vm->voices[0];
}

static inline void voice_note_on(HaloVoice* v, int note, float vel, double sr, const HaloPatch* patch) {
    if (!v->active) {
        LONG newCount = InterlockedIncrement(&g_haloActiveVoices);
        if (newCount > HALO_MAX_GLOBAL_VOICES) {
            InterlockedDecrement(&g_haloActiveVoices);
            return; /* voice stays idle; allocation fails (silence) */
        }
    }
    v->active = 1;
    v->note = note;
    v->velocity = vel;
    v->time_active = 0.0;

    double note_freq = 440.0 * pow(2.0, ((double)note + patch->pitch_semi - 69.0) / 12.0);
    v->frequency = note_freq;

    v->amp_env.attack = patch->amp_attack;
    v->amp_env.decay = patch->amp_decay * 0.6f;
    v->amp_env.sustain = patch->amp_sustain;
    v->amp_env.release = patch->amp_release;
    if (v->amp_env.release < 0.02f) v->amp_env.release = 0.02f; // 20ms minimum
    adsr_recompute_rates(&v->amp_env);
    v->amp_env.level = 0.0; // Clean attack starting from zero amplitude
    adsr_gate(&v->amp_env, 1);

    v->filter_env.attack = patch->amp_attack * 0.8f;
    v->filter_env.decay = patch->amp_decay * 0.4f;
    v->filter_env.sustain = 0.20f;
    v->filter_env.release = patch->amp_release * 0.75f;
    if (v->filter_env.release < 0.02f) v->filter_env.release = 0.02f; // 20ms minimum
    adsr_recompute_rates(&v->filter_env);
    v->filter_env.level = 0.0;
    adsr_gate(&v->filter_env, 1);

    svf_reset(&v->filter); // Clear resonant memory from previous notes

    v->lfo.frequency = patch->lfo_rate;
    v->additive.count = (int)(patch->partial_count + 0.5);
    if (v->additive.count < 1) v->additive.count = 1;
    if (v->additive.count > HALO_MAX_PARTIALS) v->additive.count = HALO_MAX_PARTIALS;

    {
        HaloPRNG rng;
        halo_prng_init(&rng, (uint64_t)(note * 2654435761u) ^ (uint64_t)(v->time_active * 1000.0) ^ 0x9E3779B97F4A7C15ULL);
        for (int i = 0; i < HALO_MAX_PARTIALS; i++) {
            v->additive.partials[i].phase = (double)(xorshift64(&rng) >> 11) * (HALO_TWO_PI / 9007199254740992.0);
            v->additive.partials[i].state = 1.0;
        }
    }

    {
        int n = (int)(patch->unison_voices + 0.5);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        v->uni_count = n;
        double spread = patch->unison_spread;
        v->uni_spread_cache = spread;
        for (int u = 0; u < n; u++) {
            double pos = (n > 1) ? ((double)u / (double)(n - 1)) * 2.0 - 1.0 : 0.0;
            v->uni_detune[u] = pow(2.0, (pos * spread) / 1200.0);
            v->uni_phase[u] = (double)u / (double)n;

            double pan_pos = 0.5 + (pos * (patch->unison_spread / 50.0)) * 0.5;
            if (pan_pos < 0.0) pan_pos = 0.0;
            if (pan_pos > 1.0) pan_pos = 1.0;
            double angle = pan_pos * (HALO_PI * 0.5);
            v->uni_gain_l[u] = cos(angle);
            v->uni_gain_r[u] = sin(angle);
        }
    }

    v->key_track_cache = patch->key_track;
    v->cutoff_key_scale = pow(2.0, patch->key_track * (double)(note - 60) / 12.0);

    v->detune_cache = patch->detune;
    v->detune_factor = pow(2.0, patch->detune / 1200.0);

    smooth_param_init(&v->smooth_cutoff, patch->filter_cutoff, 0.02, HALO_SR);
    smooth_param_init(&v->smooth_drive, patch->drive, 0.02, HALO_SR);
    smooth_param_init(&v->smooth_fm_depth, patch->fm_depth, 0.02, HALO_SR);

    v->noise_lp_z = 0.0;
    v->noise_lp_cutoff = -1.0;

    v->feedback_sample = 0.0;
    v->feedback_last = 0.0;
    v->fm_norm = 0.0;
    v->mod_phase = 0.0;
    v->warm_lp_l = 0.0;
    v->warm_lp_r = 0.0;

    (void)sr;
}

static inline void voice_note_off(HaloVoice* v) {
    if (v->active) {
        adsr_gate(&v->amp_env, 0);
        adsr_gate(&v->filter_env, 0);
    }
}

/* ========================================================================
   DSP Render Block
   ======================================================================== */

static inline void voice_render_block(HaloVoice* v, float* out_buf, int frames, const HaloPatch* patch) {
    double dt = 1.0 / HALO_SR;
    int f_type = (int)(patch->filter_type + 0.5);
    if (f_type < 0) f_type = 0;
    if (f_type > 3) f_type = 3;

    double base_freq = 440.0 * pow(2.0, ((double)v->note + patch->pitch_semi - 69.0) / 12.0);
    v->lfo.frequency = patch->lfo_rate;
    int partial_count = (int)(patch->partial_count + 0.5);
    if (partial_count < 1) partial_count = 1;
    if (partial_count > HALO_MAX_PARTIALS) partial_count = HALO_MAX_PARTIALS;
    v->additive.count = partial_count;

    v->amp_env.attack = patch->amp_attack;
    v->amp_env.decay = patch->amp_decay * 0.6;
    v->amp_env.sustain = patch->amp_sustain;
    if (v->amp_env.release != patch->amp_release) {
        v->amp_env.release = patch->amp_release;
        if (v->amp_env.release < 0.02f) v->amp_env.release = 0.02f; // 20ms minimum
    }
    adsr_recompute_rates(&v->amp_env);
    v->filter_env.attack = patch->amp_attack * 0.75f;
    v->filter_env.decay = patch->amp_decay * 0.4f;
    v->filter_env.release = patch->amp_release * 0.75f;
    if (v->filter_env.release < 0.02f) v->filter_env.release = 0.02f; // 20ms minimum
    adsr_recompute_rates(&v->filter_env);

    smooth_param_set(&v->smooth_cutoff, patch->filter_cutoff);
    smooth_param_set(&v->smooth_drive, patch->drive);
    smooth_param_set(&v->smooth_fm_depth, patch->fm_depth);
    double drive_val = smooth_param_tick(&v->smooth_drive);

    double vib_depth = patch->vibrato * (1.0 / 1200.0);
    double osc_w     = patch->osc_mix;
    double add_w     = 1.0 - osc_w;
    double noise_mix = patch->noise_mix;
    double osc_mix   = 1.0 - noise_mix;

    int uni_n = v->uni_count;
    if (uni_n < 1) uni_n = 1;
    if (uni_n != (int)(patch->unison_voices + 0.5) ||
        patch->unison_spread != v->uni_spread_cache) {
        int n = (int)(patch->unison_voices + 0.5);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        v->uni_count = uni_n = n;
        v->uni_spread_cache = patch->unison_spread;
        for (int u = 0; u < n; u++) {
            double pos = (n > 1) ? ((double)u / (double)(n - 1)) * 2.0 - 1.0 : 0.0;
            v->uni_detune[u] = pow(2.0, (pos * patch->unison_spread) / 1200.0);
            double pan_pos = 0.5 + (pos * (patch->unison_spread / 50.0)) * 0.5;
            if (pan_pos < 0.0) pan_pos = 0.0;
            if (pan_pos > 1.0) pan_pos = 1.0;
            double angle = pan_pos * (HALO_PI * 0.5);
            v->uni_gain_l[u] = cos(angle);
            v->uni_gain_r[u] = sin(angle);
        }
    }
    double uni_gain = (uni_n > 1) ? (1.0 / sqrt((double)uni_n)) : 1.0;

    double wave_val = patch->waveform;
    if (wave_val < 0.0) wave_val = 0.0;
    if (wave_val > 3.0) wave_val = 3.0;

    for (int i = 0; i < frames; i++) {
        v->time_active += dt;

        double lfo_val = lfo_tick_tri(&v->lfo);
        double vib = 1.0 + lfo_val * vib_depth;

        if (patch->detune != v->detune_cache) {
            v->detune_cache = patch->detune;
            v->detune_factor = pow(2.0, patch->detune / 1200.0);
        }

        double fm_depth_val = smooth_param_tick(&v->smooth_fm_depth);

        /* 1. Modulator oscillator with anti-fold feedback */
        double fb_smoothed = (v->feedback_sample + v->feedback_last) * 0.5;
        v->feedback_last = v->feedback_sample;

        double mod_freq = base_freq * patch->fm_ratio * vib;
        v->fm_norm += mod_freq * dt;
        v->fm_norm -= floor(v->fm_norm);
        v->mod_phase = v->fm_norm * HALO_TWO_PI;
        double mod_sig = sin(v->mod_phase) + fb_smoothed * (patch->fm_feedback * 0.15);

        /* Bounded Linear FM multiplier: prevents carrier folding and phase explosion */
        double f_mult = 1.0 + mod_sig * fm_depth_val * 0.15;
        if (f_mult < 0.05) f_mult = 0.05;

        /* Carrier stack (unison with continuous waveform morphing) */
        double osc_l = 0.0, osc_r = 0.0;
        for (int u = 0; u < uni_n; u++) {
            double det = v->uni_detune[u];
            double turns_inc = base_freq * vib * det * f_mult * dt;
            v->uni_phase[u] += turns_inc;
            v->uni_phase[u] -= floor(v->uni_phase[u]);
            double ph = v->uni_phase[u];

            double s = halo_waveform_morph(wave_val, ph, turns_inc);
            osc_l += s * v->uni_gain_l[u];
            osc_r += s * v->uni_gain_r[u];
        }
        osc_l *= uni_gain;
        osc_r *= uni_gain;
        v->feedback_sample = kill_denorm((osc_l + osc_r) * 0.5);

        /* 2. Additive bank */
        double add_fund = base_freq * v->detune_factor * vib;
        double add_out = spectral_bank_tick(&v->additive, add_fund, dt,
                                            patch->partial_tilt, patch->inharm,
                                            1.0 - patch->harm_decay * 0.8, partial_count);

        /* 3. Noise generation */
        double noise_out = pink_tick(&v->noise);
        if (noise_mix > 0.001) {
            if (fabs(patch->noise_cutoff - v->noise_lp_cutoff) > 1.0) {
                v->noise_lp_cutoff = patch->noise_cutoff;
                double fc = patch->noise_cutoff;
                if (fc < 20.0) fc = 20.0;
                if (fc > HALO_SR * 0.48) fc = HALO_SR * 0.48;
                v->noise_lp_a = 1.0 - exp(-HALO_TWO_PI * fc * dt);
            }
            v->noise_lp_z += v->noise_lp_a * (noise_out - v->noise_lp_z);
            v->noise_lp_z = kill_denorm(v->noise_lp_z);
            noise_out = v->noise_lp_z;
        }

        double blended_l = (osc_l * osc_w + add_out * add_w) * osc_mix + (noise_out * 0.4) * noise_mix;
        double blended_r = (osc_r * osc_w + add_out * add_w) * osc_mix + (noise_out * 0.4) * noise_mix;

        /* 4. Envelopes */
        double amp = adsr_tick(&v->amp_env);
        double f_env = adsr_tick(&v->filter_env);

        if (v->amp_env.state == ADSR_IDLE) {
            voice_force_idle(v);
            for (int rem = i; rem < frames; rem++) {
                out_buf[2 * rem] = 0.0f;
                out_buf[2 * rem + 1] = 0.0f;
            }
            break;
        }

        /* 5. Filter */
        if (patch->key_track != v->key_track_cache) {
            v->key_track_cache = patch->key_track;
            v->cutoff_key_scale = pow(2.0, patch->key_track * (double)(v->note - 60) / 12.0);
        }

        double cutoff_knob = smooth_param_tick(&v->smooth_cutoff);
        double mod_cutoff = cutoff_knob * v->cutoff_key_scale
                          + (patch->filter_env_depth * 4800.0 * f_env)
                          + (lfo_val * patch->lfo_filt_depth);
        if (mod_cutoff < 20.0) mod_cutoff = 20.0;
        if (mod_cutoff > HALO_SR * 0.48) mod_cutoff = HALO_SR * 0.48;

        double fd = patch->filter_drive;
        double filt_in_l = (fd > 1.2) ? warm_saturate(blended_l, fd * 0.7) : blended_l;
        double filt_in_r = (fd > 1.2) ? warm_saturate(blended_r, fd * 0.7) : blended_r;

        double filtered_l = svf_tick(&v->filter, filt_in_l, mod_cutoff, patch->filter_q, f_type);
        double filtered_r = svf_tick(&v->filter, filt_in_r, mod_cutoff, patch->filter_q, f_type);

        /* 6. Saturation & master voice VCA */
        double sat_l = warm_saturate(filtered_l, drive_val);
        double sat_r = warm_saturate(filtered_r, drive_val);

        v->warm_lp_l += 0.55 * (sat_l - v->warm_lp_l);
        v->warm_lp_r += 0.55 * (sat_r - v->warm_lp_r);
        double sample_l = kill_denorm(v->warm_lp_l);
        double sample_r = kill_denorm(v->warm_lp_r);

        out_buf[2 * i]     = kill_denorm_f((float)(sample_l * amp * (double)v->velocity));
        out_buf[2 * i + 1] = kill_denorm_f((float)(sample_r * amp * (double)v->velocity));
    }
}

static inline void halo_process_audio(HaloVoiceManager* vm, const HaloPatch* patch, float* out_buf, int frames) {
    DenormalGuard guard;
    denormal_guard_enter(&guard);

    for (int i = 0; i < frames * 2; i++) {
        out_buf[i] = 0.0f;
    }

    float block[HALO_BLOCK_SIZE * 2];

    int active_voices = 0;
    for (int v = 0; v < HALO_MAX_VOICES; v++) {
        if (!vm->voices[v].active) continue;
        active_voices++;

        int processed = 0;
        while (processed < frames) {
            int chunk = (frames - processed > HALO_BLOCK_SIZE) ? HALO_BLOCK_SIZE : (frames - processed);
            voice_render_block(&vm->voices[v], block, chunk, patch);
            for (int i = 0; i < chunk; i++) {
                out_buf[(processed + i) * 2]     += block[2 * i];
                out_buf[(processed + i) * 2 + 1] += block[2 * i + 1];
            }
            processed += chunk;
        }
    }

    // Keep the chorus wet buffer alive after the last voice releases (~35 ms)
    if (active_voices > 0) {
        vm->tail_frames = (int)(0.035 * HALO_SR);
    } else if (vm->tail_frames > 0) {
        vm->tail_frames -= frames;
        if (vm->tail_frames < 0) vm->tail_frames = 0;
    }

    chorus_process(&vm->chorus, out_buf, frames);

    // Target polyphony mix gain smoothed continuously with a 25 ms one-pole filter
    float target_gain = (active_voices > 0) ? (1.0f / sqrtf((float)active_voices)) : 1.0f;
    float gain_slew = 1.0f - expf(-1.0f / (0.025f * (float)HALO_SR));

    // True one-pole exponential tracking coefficients for limiter
    float attack_coef  = 1.0f - expf(-1.0f / (0.001f * (float)HALO_SR)); // 1 ms attack
    float release_coef = 1.0f - expf(-1.0f / (0.120f * (float)HALO_SR)); // 120 ms release
    float ceiling = 0.95f;

    for (int i = 0; i < frames; ++i) {
        vm->mix_gain += gain_slew * (target_gain - vm->mix_gain);

        float l = out_buf[i * 2]     * vm->mix_gain * 0.8f;
        float r = out_buf[i * 2 + 1] * vm->mix_gain * 0.8f;

        // Linked stereo peak detection
        float peak = fmaxf(fabsf(l), fabsf(r));
        if (peak > vm->limiter_gain) {
            vm->limiter_gain += attack_coef * (peak - vm->limiter_gain);
        } else {
            vm->limiter_gain += release_coef * (peak - vm->limiter_gain);
        }

        float g = (vm->limiter_gain > ceiling) ? (ceiling / vm->limiter_gain) : 1.0f;
        l *= g;
        r *= g;

        if (l > 0.8f)       l = 0.8f + tanhf((l - 0.8f) / 0.2f) * 0.2f;
        else if (l < -0.8f) l = -0.8f + tanhf((l + 0.8f) / 0.2f) * 0.2f;

        if (r > 0.8f)       r = 0.8f + tanhf((r - 0.8f) / 0.2f) * 0.2f;
        else if (r < -0.8f) r = -0.8f + tanhf((r + 0.8f) / 0.2f) * 0.2f;

        out_buf[i * 2]     = l;
        out_buf[i * 2 + 1] = r;
    }

    denormal_guard_leave(&guard);
}

static inline int halo_render_offline(const HaloPatch* patch, int note, float velocity, float duration_sec, float* out_buf, int max_samples) {
    int total_samples = (int)(duration_sec * (double)HALO_SR);
    if (total_samples > max_samples) total_samples = max_samples;
    if (total_samples <= 0) return 0;

    HaloVoiceManager vm;
    voice_manager_init(&vm, HALO_SR);
    HaloVoice* v = voice_manager_alloc(&vm, note);
    if (!v) return 0;
    voice_note_on(v, note, velocity, HALO_SR, patch);

    int processed = 0;
    int block_size = 128;
    float temp[128 * 2];

    while (processed < total_samples) {
        int chunk = (total_samples - processed > block_size) ? block_size : (total_samples - processed);
        halo_process_audio(&vm, patch, temp, chunk);
        memcpy(&out_buf[processed * 2], temp, (size_t)chunk * 2 * sizeof(float));
        processed += chunk;
        if (processed >= (int)(duration_sec * 0.75 * HALO_SR) && v->amp_env.state == ADSR_SUSTAIN) {
            voice_note_off(v);
        }
    }
    return total_samples;
}

#endif /* HALO_ENGINE_H */