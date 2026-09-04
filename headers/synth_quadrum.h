#ifndef QUADRUM_ENGINE_H
#define QUADRUM_ENGINE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <immintrin.h>

#define QUADRUM_SR          44100
#define QUADRUM_MAX_SAMPLES (QUADRUM_SR * 2)
#define QUADRUM_PI          3.14159265358979323846
#define QUADRUM_TWO_PI      6.28318530717958647692

/* ========================================================================
 * SIMD Utilities & Hardware Denormal Guards (FTZ / DAZ)
 * ======================================================================== */

static inline unsigned int quadrum_simd_enter(void) {
    unsigned int old_mxcsr = _mm_getcsr();
    /* Bit 15: Flush-to-Zero (FTZ), Bit 6: Denormals-Are-Zero (DAZ) */
    _mm_setcsr(old_mxcsr | 0x8040);
    return old_mxcsr;
}

static inline void quadrum_simd_exit(unsigned int old_mxcsr) {
    _mm_setcsr(old_mxcsr);
}

/* Branchless symmetric clamp [-limit, +limit] */
static inline __m128 mm_clamp_sym_ps(__m128 val, __m128 limit) {
    __m128 neg_limit = _mm_sub_ps(_mm_setzero_ps(), limit);
    return _mm_min_ps(_mm_max_ps(val, neg_limit), limit);
}

/* Branchless conditional select: mask ? a : b */
static inline __m128 mm_select_ps(__m128 mask, __m128 a, __m128 b) {
    return _mm_or_ps(_mm_and_ps(mask, a), _mm_andnot_ps(mask, b));
}

/* Fast branchless rational soft-clipper: x / (1.0 + |x|) */
static inline __m128 mm_softclip_ps(__m128 x) {
    __m128 sign_mask = _mm_set1_ps(-0.0f);
    __m128 abs_x = _mm_andnot_ps(sign_mask, x);
    __m128 denom = _mm_add_ps(_mm_set1_ps(1.0f), abs_x);
    return _mm_div_ps(x, denom);
}

/* ========================================================================
 * Voice Types & Parameters
 * ======================================================================== */

typedef enum {
    VOICE_KICK = 0,
    VOICE_SNARE,
    VOICE_CLAP,
    VOICE_CLOSED_HAT,
    VOICE_OPEN_HAT,
    VOICE_TOM,
    VOICE_COWBELL,
    VOICE_CYMBAL,
    VOICE_COUNT
} VoiceType;

static const char* VOICE_NAMES[VOICE_COUNT] = {
    "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Cowbell", "Cymbal"
};

typedef struct {
    /* Oscillator & Pitch */
    double pitch;          /* Base pitch in Hz (20.0 .. 1500.0) */
    double pitch_env;      /* Pitch envelope sweep depth (0.0 .. 1.0) */
    double pitch_decay;    /* Pitch envelope decay time in seconds (0.002 .. 0.4) */
    double fm_ratio;       /* FM / Overtone frequency multiplier (0.5 .. 8.0) */
    double fm_depth;       /* FM / Saturation depth (0.0 .. 8.0) */

    /* Noise & Transients */
    double noise_mix;      /* Noise-to-Oscillator blend ratio (0.0 .. 1.0) */
    double noise_decay;    /* Noise decay time in seconds (0.005 .. 1.5) */
    double noise_cutoff;   /* Noise band/cutoff frequency in Hz (100.0 .. 18000.0) */
    double click;          /* Transient attack impulse amount (0.0 .. 1.0) */

    /* Filter & Color */
    double filter_cutoff;  /* Master filter cutoff frequency in Hz (100.0 .. 18000.0) */
    double filter_q;       /* Master filter resonance Q (0.3 .. 8.0) */
    double filter_type;    /* 0.0 = Lowpass, 1.0 = Highpass, 2.0 = Bandpass */
    double drive;          /* Saturation / Drive intensity (1.0 .. 6.0) */

    /* Amplitude & Envelope */
    double decay;          /* Master amplitude decay time in seconds (0.01 .. 2.0) */
    double clap_taps;      /* Number of clap bursts (1.0 = standard, 2.0..6.0 = clap) */
    double clap_spread;    /* Delay between clap bursts in seconds (0.005 .. 0.035) */
} QuadrumParams;

/* ========================================================================
 * DSP Primitives: PRNG, Transposed Direct-Form II Biquad, Soft Saturator
 * ======================================================================== */

typedef struct {
    uint64_t state;
} QuadrumPRNG;

static inline void prng_init(QuadrumPRNG* r, uint64_t seed) {
    r->state = seed ? seed : 0x853C49E6748FEA9BULL;
}

static inline float prng_next_f(QuadrumPRNG* r) {
    r->state ^= (r->state >> 12);
    r->state ^= (r->state << 25);
    r->state ^= (r->state >> 27);
    uint64_t val = r->state * 0x2545F4914F6CDD1DULL;
    return (float)((double)(val >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0);
}

typedef struct {
    double b0, b1, b2, a1, a2;
    double w1, w2;
} QuadBiquad;

static inline void biquad_reset(QuadBiquad* f) {
    f->w1 = 0.0;
    f->w2 = 0.0;
}

static inline void biquad_setup(QuadBiquad* f, int type, double fc, double Q, double sr) {
    if (fc < 10.0) fc = 10.0;
    if (fc > sr * 0.48) fc = sr * 0.48;
    if (Q < 0.1) Q = 0.1;
    if (Q > 15.0) Q = 15.0;

    double w0 = QUADRUM_TWO_PI * fc / sr;
    double c = cos(w0);
    double s = sin(w0);
    double alpha = s / (2.0 * Q);
    double a0, b0, b1, b2, a1, a2;

    switch (type) {
        case 1: /* Highpass */
            b0 = (1.0 + c) * 0.5;
            b1 = -(1.0 + c);
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha;
            break;
        case 2: /* Bandpass */
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha;
            break;
        case 0: /* Lowpass */
        default:
            b0 = (1.0 - c) * 0.5;
            b1 = 1.0 - c;
            b2 = b0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * c;
            a2 = 1.0 - alpha;
            break;
    }

    double inv_a0 = 1.0 / a0;
    f->b0 = b0 * inv_a0;
    f->b1 = b1 * inv_a0;
    f->b2 = b2 * inv_a0;
    f->a1 = a1 * inv_a0;
    f->a2 = a2 * inv_a0;
}

static inline double biquad_tick(QuadBiquad* f, double in) {
    double out = f->b0 * in + f->w1;
    f->w1 = f->b1 * in - f->a1 * out + f->w2;
    f->w2 = f->b2 * in - f->a2 * out;
    return out;
}

/* Studio Soft-Knee Saturation (Cubic Hermite Clipper) */
static inline float soft_saturate_f(float x, float drive) {
    if (drive <= 0.1f) drive = 0.1f;
    float in = x * drive;
    if (in > 1.0f)  return 1.0f;
    if (in < -1.0f) return -1.0f;
    return in * (1.5f - 0.5f * in * in);
}

/* ========================================================================
 * Factory Presets
 * ======================================================================== */

static void quadrum_get_preset(VoiceType voice, QuadrumParams* p) {
    switch (voice) {
        case VOICE_KICK:
            p->pitch = 50.0;
            p->pitch_env = 0.82;
            p->pitch_decay = 0.022;
            p->fm_ratio = 1.0;
            p->fm_depth = 0.15;
            p->noise_mix = 0.02;
            p->noise_decay = 0.015;
            p->noise_cutoff = 2200.0;
            p->click = 0.32;
            p->filter_cutoff = 2800.0;
            p->filter_q = 0.85;
            p->filter_type = 0.0;
            p->drive = 2.2;
            p->decay = 0.48;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_SNARE:
            p->pitch = 175.0;
            p->pitch_env = 0.42;
            p->pitch_decay = 0.018;
            p->fm_ratio = 1.62;
            p->fm_depth = 0.08;
            p->noise_mix = 0.65;
            p->noise_decay = 0.19;
            p->noise_cutoff = 4200.0;
            p->click = 0.55;
            p->filter_cutoff = 8500.0;
            p->filter_q = 0.707;
            p->filter_type = 0.0;
            p->drive = 1.8;
            p->decay = 0.22;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_CLAP:
            p->pitch = 1100.0;
            p->pitch_env = 0.0;
            p->pitch_decay = 0.010;
            p->fm_ratio = 1.0;
            p->fm_depth = 0.0;
            p->noise_mix = 1.0;
            p->noise_decay = 0.22;
            p->noise_cutoff = 1150.0;
            p->click = 0.40;
            p->filter_cutoff = 2200.0;
            p->filter_q = 1.8;
            p->filter_type = 2.0;
            p->drive = 1.9;
            p->decay = 0.32;
            p->clap_taps = 4.0;
            p->clap_spread = 0.012;
            break;

        case VOICE_CLOSED_HAT:
            p->pitch = 440.0;
            p->pitch_env = 0.0;
            p->pitch_decay = 0.01;
            p->fm_ratio = 2.83;
            p->fm_depth = 1.5;
            p->noise_mix = 0.78;
            p->noise_decay = 0.050;
            p->noise_cutoff = 9500.0;
            p->click = 0.35;
            p->filter_cutoff = 7200.0;
            p->filter_q = 0.85;
            p->filter_type = 1.0;
            p->drive = 1.8;
            p->decay = 0.075;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_OPEN_HAT:
            p->pitch = 420.0;
            p->pitch_env = 0.0;
            p->pitch_decay = 0.01;
            p->fm_ratio = 2.83;
            p->fm_depth = 0.6;
            p->noise_mix = 0.85;
            p->noise_decay = 0.48;
            p->noise_cutoff = 9000.0;
            p->click = 0.25;
            p->filter_cutoff = 6800.0;
            p->filter_q = 0.80;
            p->filter_type = 1.0;
            p->drive = 1.6;
            p->decay = 0.52;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_TOM:
            p->pitch = 110.0;
            p->pitch_env = 0.60;
            p->pitch_decay = 0.060;
            p->fm_ratio = 1.0;
            p->fm_depth = 0.2;
            p->noise_mix = 0.10;
            p->noise_decay = 0.035;
            p->noise_cutoff = 2500.0;
            p->click = 0.60;
            p->filter_cutoff = 1800.0;
            p->filter_q = 0.9;
            p->filter_type = 0.0;
            p->drive = 1.5;
            p->decay = 0.38;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_COWBELL:
            p->pitch = 540.0;
            p->pitch_env = 0.0;
            p->pitch_decay = 0.010;
            p->fm_ratio = 1.4815;
            p->fm_depth = 0.40;
            p->noise_mix = 0.0;
            p->noise_decay = 0.01;
            p->noise_cutoff = 1000.0;
            p->click = 0.40;
            p->filter_cutoff = 950.0;
            p->filter_q = 1.6;
            p->filter_type = 2.0;
            p->drive = 1.10;
            p->decay = 0.36;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;

        case VOICE_CYMBAL:
            p->pitch = 260.0;
            p->pitch_env = 0.0;
            p->pitch_decay = 0.01;
            p->fm_ratio = 2.45;
            p->fm_depth = 0.35;
            p->noise_mix = 0.90;
            p->noise_decay = 1.15;
            p->noise_cutoff = 7800.0;
            p->click = 0.0;
            p->filter_cutoff = 5400.0;
            p->filter_q = 0.707;
            p->filter_type = 1.0;
            p->drive = 1.1;
            p->decay = 1.25;
            p->clap_taps = 1.0;
            p->clap_spread = 0.01;
            break;
    }
}

/* ========================================================================
 * Drum Synthesis Engine
 * ======================================================================== */

static int quadrum_render(const QuadrumParams* p, float* out_buf, int max_len) {
    if (!p || !out_buf || max_len <= 0) return 0;

    /* Enable FTZ + DAZ for the duration of the render pass to avoid
       subnormal float microcode slowdowns in the inner loop. */
    unsigned int prev_mxcsr = quadrum_simd_enter();

    /* Clamp parameters to safe musical bounds */
    double safe_decay     = (p->decay < 0.01) ? 0.01 : ((p->decay > 2.0) ? 2.0 : p->decay);
    double safe_n_decay   = (p->noise_decay < 0.002) ? 0.002 : ((p->noise_decay > 2.0) ? 2.0 : p->noise_decay);
    double safe_pitch     = (p->pitch < 20.0) ? 20.0 : ((p->pitch > 2000.0) ? 2000.0 : p->pitch);
    double safe_noise_mix = (p->noise_mix < 0.0) ? 0.0 : ((p->noise_mix > 1.0) ? 1.0 : p->noise_mix);
    double safe_drive     = (p->drive < 0.2) ? 0.2 : ((p->drive > 6.0) ? 6.0 : p->drive);
    double safe_p_decay   = (p->pitch_decay < 0.002) ? 0.002 : p->pitch_decay;

    int num_taps = (int)(p->clap_taps + 0.5);
    if (num_taps < 1) num_taps = 1;
    if (num_taps > 8) num_taps = 8;

    double safe_spread = (p->clap_spread < 0.004) ? 0.004 : ((p->clap_spread > 0.050) ? 0.050 : p->clap_spread);

    int filt_type = (int)(p->filter_type + 0.5);
    if (filt_type < 0) filt_type = 0;
    if (filt_type > 2) filt_type = 2;

    /* Buffer size tracks the master amp decay + clap flam time */
    double extra_time = (num_taps > 1) ? ((double)num_taps * safe_spread) : 0.0;
    int total_samples = (int)((safe_decay * 1.5 + extra_time + 0.06) * (double)QUADRUM_SR);

    if (total_samples > max_len) total_samples = max_len;
    if (total_samples > QUADRUM_MAX_SAMPLES) total_samples = QUADRUM_MAX_SAMPLES;

    /* Voice Category Classification */
    int is_clap    = (num_taps > 1);
    int is_kick    = (!is_clap && filt_type == 0 && safe_pitch <= 90.0);
    int is_snare   = (!is_clap && filt_type == 0 && safe_pitch >= 120.0 && safe_pitch <= 290.0);
    int is_cowbell = (!is_clap && filt_type == 2 && safe_pitch > 350.0);
    int is_cymbal  = (!is_clap && filt_type == 1 && safe_decay > 0.70);
    int is_metal   = (!is_clap && filt_type == 1);

    /* PRNG and Filter Initialization */
    QuadrumPRNG prng;
    prng_init(&prng, 1337420ULL);

    QuadBiquad noise_filt;
    biquad_setup(&noise_filt, 2, p->noise_cutoff, is_snare ? 0.9 : 1.5, QUADRUM_SR);
    biquad_reset(&noise_filt);

    QuadBiquad master_filt;
    biquad_setup(&master_filt, filt_type, p->filter_cutoff, p->filter_q, QUADRUM_SR);
    biquad_reset(&master_filt);

    /* Oscillator Phase Tracking */
    double phase_carrier = 0.0;
    double phase_mod1    = 0.0;
    double phase_mod2    = 0.0;
    double inv_sr        = 1.0 / (double)QUADRUM_SR;
    double nyquist_limit = (double)QUADRUM_SR * 0.45;

    /* Parameter Smoothers */
    double smoothed_pitch = safe_pitch;
    double smoothed_mix   = safe_noise_mix;
    double smooth_coeff   = 0.0035;

    /* Tonal Decay Time Constant */
    double tonal_tau = safe_decay * 0.45;
    if (is_snare)  tonal_tau = safe_decay * 0.35;
    if (is_cymbal) tonal_tau = safe_decay * 0.15;
    if (tonal_tau < 0.002) tonal_tau = 0.002;

    for (int i = 0; i < total_samples; i++) {
        double t = (double)i * inv_sr;

        /* Parameter smoothing */
        smoothed_pitch += smooth_coeff * (safe_pitch - smoothed_pitch);
        smoothed_mix   += smooth_coeff * (safe_noise_mix - smoothed_mix);

        /* 1. Pitch Envelope Sweep */
        double pitch_scale = is_snare ? 1.5 : 4.0;
        double f_inst = smoothed_pitch + (smoothed_pitch * pitch_scale * p->pitch_env) * exp(-t / safe_p_decay);
        if (f_inst < 10.0) f_inst = 10.0;
        if (f_inst > nyquist_limit) f_inst = nyquist_limit;

        /* 2. Modulator Phases */
        double fm_freq1 = f_inst * p->fm_ratio;
        if (fm_freq1 > nyquist_limit) fm_freq1 = nyquist_limit;
        phase_mod1 += fm_freq1 * QUADRUM_TWO_PI * inv_sr;
        if (phase_mod1 >= QUADRUM_TWO_PI) phase_mod1 -= QUADRUM_TWO_PI;

        double fm_freq2 = fm_freq1 * 1.34164078;
        if (fm_freq2 > nyquist_limit) fm_freq2 = nyquist_limit;
        phase_mod2 += fm_freq2 * QUADRUM_TWO_PI * inv_sr;
        if (phase_mod2 >= QUADRUM_TWO_PI) phase_mod2 -= QUADRUM_TWO_PI;

        /* 3. Carrier Phase */
        phase_carrier += f_inst * QUADRUM_TWO_PI * inv_sr;
        if (phase_carrier >= QUADRUM_TWO_PI) phase_carrier -= QUADRUM_TWO_PI;

        /* 4. Oscillator Generation */
        double osc = 0.0;
        if (is_clap) {
            osc = 0.0;
        } else if (is_cowbell) {
            double drive_mode = 1.0 + p->fm_depth * 1.25;
            double sq1 = tanh(sin(phase_carrier) * drive_mode);
            double sq2 = tanh(sin(phase_mod1) * drive_mode);
            double high_mode_decay = exp(-t / (safe_decay * 0.40 + 0.001));
            osc = 0.56 * sq1 + (0.44 * sq2 * high_mode_decay);
        } else if (is_metal) {
            double m1 = sin(phase_carrier);
            double m2 = sin(phase_mod1);
            double m3 = sin(phase_mod2);
            double metal_blend = 0.44 * m1 + 0.36 * m2 + 0.20 * m3;
            osc = soft_saturate_f((float)metal_blend, (float)(1.0 + p->fm_depth * 0.35));
        } else {
            double mod_env = exp(-t / (safe_p_decay * 2.0 + 0.01));
            double fm_signal = sin(phase_mod1);
            double phase_modulated = phase_carrier + fm_signal * p->fm_depth * mod_env;

            if (is_snare) {
                osc = sin(phase_modulated) * 0.70 + sin(phase_mod1) * 0.30;
            } else {
                osc = sin(phase_modulated);
            }
        }

        /* 5. Tonal Decay Window */
        double tonal_amp_env = 0.0;
        if (!is_clap) {
            tonal_amp_env = exp(-t / tonal_tau);
            if (is_cymbal) tonal_amp_env *= 0.20;
        }

        /* 6. Noise Envelope & Multi-Tap Generation */
        float raw_prng = prng_next_f(&prng);
        double filtered_noise = biquad_tick(&noise_filt, raw_prng) * 1.8;
        double noise_env = 0.0;

        // update clap tail branch to respond to safe decay
        if (num_taps > 1) {
            int tap_idx = (int)(t / safe_spread);
            if (tap_idx < num_taps - 1) {
                double dt = t - ((double)tap_idx * safe_spread);
                double burst_tau = (safe_spread * 0.65 < 0.012) ? safe_spread * 0.65 : 0.012;
                noise_env = exp(-dt / burst_tau) * (1.0 - dt / safe_spread);
            } else {
                double tail_t = t - ((double)(num_taps - 1) * safe_spread);
                if (tail_t >= 0.0) {
                    /* Tail responds directly to both noise decay and amp decay */
                    double tail_tau = (safe_decay < safe_n_decay) ? safe_decay : safe_n_decay;
                    double tail = exp(-tail_t / (tail_tau * 0.6 + 0.001));
                    noise_env = tail * tail;
                }
            }
        } else {
            double raw_env = exp(-t / safe_n_decay);
            noise_env = raw_env * raw_env;
            if (is_cymbal) {
                double soft_attack = 1.0 - exp(-t / 0.012);
                noise_env *= soft_attack;
            }
        }

        /* 7. Acoustic Transient Snap */
        double click_val = 0.0;
        if (i < 100 && p->click > 0.001) {
            double click_t = (double)i * inv_sr;
            if (is_kick) {
                double click_env = exp(-click_t / 0.0025);
                double click_osc = sin(click_t * QUADRUM_TWO_PI * 1400.0);
                click_val = click_env * click_osc * p->click * 0.85;
            } else {
                double click_env = exp(-click_t / 0.0018);
                double click_osc = sin(click_t * QUADRUM_TWO_PI * 2800.0);
                double click_rnd = (double)prng_next_f(&prng);
                click_val = click_env * (click_osc * 0.6 + click_rnd * 0.4) * p->click * 1.2;
            }
        }

        /* 8. Blend Components with Master VCA Amplitude Envelope */
        double master_amp = exp(-t / (safe_decay * 0.38 + 0.001));
        if (is_clap && t < extra_time) {
            master_amp = 1.0; /* Preserve initial clap flam transient burst volume */
        }

        double osc_gain = 1.0 - smoothed_mix;
        double combined = ((osc * tonal_amp_env * osc_gain)
                        + (filtered_noise * noise_env * smoothed_mix)
                        + click_val) * master_amp;

        /* 9. Master Filtering & Saturation */
        double filtered = biquad_tick(&master_filt, combined);
        out_buf[i] = soft_saturate_f((float)filtered, (float)safe_drive);
    }

    /* Micro-fade attack to eliminate startup DC transients */
    int attack_len = 8;
    if (attack_len > total_samples) attack_len = total_samples;
    for (int i = 0; i < attack_len; i++) {
        out_buf[i] *= (float)((double)i / (double)attack_len);
    }

    /* Raised-cosine tail fade (guarantees exact 0.0 termination) */
    int fade_len = 512;
    if (fade_len > total_samples / 4) fade_len = total_samples / 4;
    for (int i = 0; i < fade_len; i++) {
        int idx = total_samples - fade_len + i;
        double phase = QUADRUM_PI * (double)(i + 1) / (double)fade_len;
        out_buf[idx] *= (float)(0.5 * (1.0 + cos(phase)));
    }

    /* Calibrated Headroom Stage (SIMD 4-wide + Scalar Tail) */
    int i = 0;
    int simd_limit = total_samples & ~3;
    __m128 gain_vec = _mm_set1_ps(0.52f);
    __m128 one_vec  = _mm_set1_ps(1.0f);

    for (; i < simd_limit; i += 4) {
        __m128 v = _mm_loadu_ps(&out_buf[i]);
        v = _mm_mul_ps(v, gain_vec);
        v = mm_clamp_sym_ps(v, one_vec);
        _mm_storeu_ps(&out_buf[i], v);
    }

    for (; i < total_samples; ++i) {
        float v = out_buf[i] * 0.52f;
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        out_buf[i] = v;
    }

    quadrum_simd_exit(prev_mxcsr);
    return total_samples;
}

/* ========================================================================
 * 32-bit Floating Point WAV File Exporter
 * ======================================================================== */

#pragma pack(push, 1)
typedef struct {
    char     riff_id[4];        /* "RIFF" */
    uint32_t riff_size;
    char     wave_id[4];        /* "WAVE" */
    char     fmt_id[4];         /* "fmt " */
    uint32_t fmt_size;          /* 16 for PCM/IEEE float */
    uint16_t audio_format;      /* 3 = WAVE_FORMAT_IEEE_FLOAT */
    uint16_t num_channels;      /* 1 */
    uint32_t sample_rate;       /* 44100 */
    uint32_t byte_rate;         /* sample_rate * num_channels * (bits/8) */
    uint16_t block_align;       /* num_channels * (bits/8) */
    uint16_t bits_per_sample;   /* 32 */
    char     data_id[4];        /* "data" */
    uint32_t data_size;
} QuadrumWavHeader;
#pragma pack(pop)

/* NOTE: The standalone quadrum_export_wav() helper was removed as dead code --
   the UI performs WAV export asynchronously via async_export_worker(). */

#endif /* QUADRUM_ENGINE_H */