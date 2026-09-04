#pragma once
#include "globals.h"
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#define clamp(v, lo, hi) (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))

 
#define DENORMAL_THRESHOLD 1e-18f    

 
#ifdef CSEQ_AUDIO_DEBUG
static uint64_t g_denormFlushScalar = 0;
static uint64_t g_denormFlushSimd   = 0;
static inline void denormal_debug_dump(void) {
    char buf[128];
    snprintf(buf, sizeof(buf), "[cseq denormals] scalar=%llu simd=%llu\n",
             (unsigned long long)g_denormFlushScalar,
             (unsigned long long)g_denormFlushSimd);
    OutputDebugStringA(buf);
}
#define DENORM_COUNT_SCALAR(v) \
    do { if ((v) != 0.0f) ++g_denormFlushScalar; } while (0)
#define DENORM_COUNT_SIMD(m) \
    do { if ((m) != 0) ++g_denormFlushSimd; } while (0)
#else
static inline void denormal_debug_dump(void) { }
#define DENORM_COUNT_SCALAR(v) ((void)0)
#define DENORM_COUNT_SIMD(m)   ((void)0)
#endif

 
static inline float denormal_flush_f(float v) {
    DENORM_COUNT_SCALAR(v);
    return (fabsf(v) < DENORMAL_THRESHOLD) ? 0.0f : v;
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 
static inline __m128 denormal_flush_ps128(__m128 v) {
    const __m128 signMask = _mm_set1_ps(-0.0f);
    __m128 mag  = _mm_andnot_ps(signMask, v);
    __m128 mask = _mm_cmplt_ps(mag, _mm_set1_ps(DENORMAL_THRESHOLD));
    DENORM_COUNT_SIMD(_mm_movemask_ps(mask));
    return _mm_andnot_ps(mask, v);
}
static inline __m256 denormal_flush_ps256(__m256 v) {
    const __m256 signMask = _mm256_set1_ps(-0.0f);
    __m256 mag  = _mm256_andnot_ps(signMask, v);
    __m256 mask = _mm256_cmp_ps(mag, _mm256_set1_ps(DENORMAL_THRESHOLD), _CMP_LT_OQ);
    DENORM_COUNT_SIMD(_mm256_movemask_ps(mask));
    return _mm256_andnot_ps(mask, v);
}
#endif

 
static inline float compute_fade_gain(float t, uint8_t curveType, bool isFadeIn) {
    if (t <= 0.0f) return isFadeIn ? 0.0f : 1.0f;
    if (t >= 1.0f) return isFadeIn ? 1.0f : 0.0f;

    float g = 0.0f;
    switch (curveType) {
    case FADE_CURVE_EXP:
        g = isFadeIn ? (t * t * t) : powf(1.0f - t, 3.0f);
        break;
    case FADE_CURVE_SMOOTH:
        g = isFadeIn ? (0.5f * (1.0f - cosf(3.14159265358979323846f * t)))
                     : (0.5f * (1.0f + cosf(3.14159265358979323846f * t)));
        break;
    case FADE_CURVE_LOG:
        g = isFadeIn ? sqrtf(t) : sqrtf(1.0f - t);
        break;
    case FADE_CURVE_LINEAR:
    default:
        g = isFadeIn ? t : (1.0f - t);
        break;
    }
    return (g < 0.0f) ? 0.0f : ((g > 1.0f) ? 1.0f : g);
}

 

static inline float get_pixels_per_beat(void) {
    return (PIXELS_PER_BEAT_BASE * g_dpiScaleX) * (g_Seq.zoom > 0.05f ? g_Seq.zoom : 1.0f);
}
static inline float total_beats(void) {
    int bars = (g_Seq.visibleBarCount < MIN_BARS) ? MIN_BARS : ((g_Seq.visibleBarCount > MAX_BARS) ? MAX_BARS : g_Seq.visibleBarCount);
    return (float)bars * beats_per_bar();
}

 
static inline float frames_per_beat(float bpm) {
    float safeBpm = (bpm < 20.0f) ? 20.0f : ((bpm > 400.0f) ? 400.0f : bpm);
    return (float)SAMPLE_RATE * (60.0f / safeBpm);
}

 
static inline COLORREF desaturate_color_by_volume(COLORREF base, float vol) {
    if (!(vol < 1.0f)) return base;    
    float k = clamp(vol * 2.0f, 0.0f, 1.0f);    
    k = k * k * (3.0f - 2.0f * k);     

    float r = (float)GetRValue(base) / 255.0f;
    float g = (float)GetGValue(base) / 255.0f;
    float b = (float)GetBValue(base) / 255.0f;
    float mx = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    float mn = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    float l = 0.5f * (mx + mn);
    float d = mx - mn;
    if (d <= 0.0f) return base;        
    float s = ((l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn)) * k;
    float h;
    if      (mx == r) h = (g - b) / d + ((g < b) ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else              h = (r - g) / d + 4.0f;
    h /= 6.0f;

    float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    float t[3] = { h + (1.0f / 3.0f), h, h - (1.0f / 3.0f) };
    float out[3];
    for (int i = 0; i < 3; ++i) {
        float tt = t[i];
        if (tt < 0.0f) tt += 1.0f;
        if (tt > 1.0f) tt -= 1.0f;
        if      (tt < (1.0f / 6.0f)) out[i] = p + (q - p) * 6.0f * tt;
        else if (tt < 0.5f)          out[i] = q;
        else if (tt < (2.0f / 3.0f)) out[i] = p + (q - p) * ((2.0f / 3.0f) - tt) * 6.0f;
        else                         out[i] = p;
    }
    return RGB((BYTE)(out[0] * 255.0f + 0.5f),
               (BYTE)(out[1] * 255.0f + 0.5f),
               (BYTE)(out[2] * 255.0f + 0.5f));
}

 
static inline float grid_division_beat_fraction(int div) {
    switch (div) {
        case GRID_1_16:  return 0.25f;           
        case GRID_1_16T: return (1.0f / 6.0f);   
        case GRID_1_32:  return 0.125f;          
        case GRID_1_32T: return (1.0f / 12.0f);  
        default:         return 0.25f;
    }
}

static inline const char* grid_division_label(int div) {
    switch (div) {
        case GRID_1_16:  return "1/16";
        case GRID_1_16T: return "1/16T";
        case GRID_1_32:  return "1/32";
        case GRID_1_32T: return "1/32T";
        default:         return "1/16";
    }
}

 
static inline float get_min_clip_length_beats(void) {
    float gridBeats = grid_division_beat_fraction(g_Seq.gridDivision);
    return (gridBeats < MIN_CLIP_LENGTH_BEATS) ? gridBeats : MIN_CLIP_LENGTH_BEATS;
}

static inline float quantize_beat_16th(float beat) { 
    
    float frac = grid_division_beat_fraction(g_Seq.gridDivision);
    return floorf(beat / frac + 0.5f) * frac;
}


static inline float apply_clip_swing(float beat, float swing) {
    if (swing <= 0.001f) return beat;
    float pair = floorf(beat * 2.0f) * 0.5f, off = beat - pair;
    if (off >= 0.10f && off < 0.40f) return pair + off + (swing * 0.1667f);
    return beat;
}

// Swing a note event's position within a clip. The clip start is already
// swung via apply_clip_swing; this shifts the note's own beat when it sits on
// an off-beat eighth (the "and"), matching apply_clip_swing's curve so note
// events line up with the swung clip grid.
static inline float apply_note_clip_swing(float noteBeat, float swing) {
    if (swing <= 0.001f) return noteBeat;
    float pair = floorf(noteBeat * 2.0f) * 0.5f, off = noteBeat - pair;
    if (off >= 0.10f && off < 0.40f) return pair + off + (swing * 0.1667f);
    return noteBeat;
}

static inline float frame_to_beat(ma_uint64 frame, float bpm, float swing) {
    (void)swing;
    float fpb = frames_per_beat(bpm);
    return (fpb > 0.0001f) ? (float)((double)frame / (double)fpb) : 0.0f;
}

static inline ma_uint64 beat_to_frame(float beat, float bpm, float swing) {
    if (beat < 0.0f) beat = 0.0f;
    float fpb = frames_per_beat(bpm), sub = beat - floorf(beat), shift = 0.0f;
    if (fabsf(sub - 0.25f) < 0.05f || fabsf(sub - 0.75f) < 0.05f) shift = swing * 0.12f;
    else if (fabsf(sub - 0.50f) < 0.05f) shift = swing * 0.16f;
    return (ma_uint64)((double)(beat + shift) * (double)fpb);
}

 
static inline void peak_biquad_clear(PeakBiquad *b) {
    b->z1L = b->z2L = b->z1R = b->z2R = 0.0f;
}

static inline void peak_biquad_set(PeakBiquad *b, float freqHz, float Q, float gainDb, float sampleRate) {
    if (Q < 0.1f) Q = 0.1f; if (Q > 8.0f) Q = 8.0f;
    if (freqHz < 20.0f) freqHz = 20.0f; if (freqHz > sampleRate * 0.45f) freqHz = sampleRate * 0.45f;

    b->gainDb = gainDb;
    float A = powf(10.0f, gainDb / 40.0f), w0 = 2.0f * 3.14159265f * freqHz / sampleRate;
    float alpha = sinf(w0) / (2.0f * Q), cosw0 = cosf(w0);
    float a0 = 1.0f + alpha / A;
    
    b->b0 = (1.0f + alpha * A) / a0;
    b->b1 = (-2.0f * cosw0) / a0;
    b->b2 = (1.0f - alpha * A) / a0;
    b->a1 = (-2.0f * cosw0) / a0;
    b->a2 = (1.0f - alpha / A) / a0;
}

static inline void peak_biquad_process(PeakBiquad *b, float *L, float *R) {
     
    if (fabsf(b->gainDb) < 0.0001f) {
        b->z1L = b->z2L = b->z1R = b->z2R = 0.0f;
        return;
    }
    float inL = *L, inR = *R;
    // A non-finite input would poison the state registers permanently, so
    // gate it here instead of letting NaN/Inf propagate through the filter.
    if (!_finite(inL)) inL = 0.0f;
    if (!_finite(inR)) inR = 0.0f;
    float outL = b->b0 * inL + b->z1L;
    b->z1L = b->b1 * inL - b->a1 * outL + b->z2L;
    b->z2L = b->b2 * inL - b->a2 * outL;
    float outR = b->b0 * inR + b->z1R;
    b->z1R = b->b1 * inR - b->a1 * outR + b->z2R;
    b->z2R = b->b2 * inR - b->a2 * outR;
     
    b->z1L = denormal_flush_f(b->z1L);
    b->z2L = denormal_flush_f(b->z2L);
    b->z1R = denormal_flush_f(b->z1R);
    b->z2R = denormal_flush_f(b->z2R);
    *L = denormal_flush_f(outL);
    *R = denormal_flush_f(outR);
}


 
static inline bool vis_playhead_try_lock(DWORD maxSpins) {
    for (DWORD i = 0; i < maxSpins; ++i) {
        if (InterlockedCompareExchange(&g_Seq.visualPlayheadLock, 1, 0) == 0) return true;
        YieldProcessor();
    }
    return false;
}

static inline void vis_playhead_unlock(void) {
    InterlockedExchange(&g_Seq.visualPlayheadLock, 0);
}

 
static inline void set_playback_frame(LONG frame) {
    LONG prev = InterlockedExchange(&g_Seq.playbackFrame, frame);

    LONGLONG now = 0;
    QueryPerformanceCounter((LARGE_INTEGER*)&now);

    if (!vis_playhead_try_lock(64)) return;

    double delta = (double)frame - g_Seq.visualPlayheadFrame;
    bool discontinuity =
        !seq_is_playing() ||
        (fabs(delta) > 4096.0) ||
        (frame < prev && delta < -1000.0);

    if (discontinuity) {
        g_Seq.visualSyncFrame     = frame;
        g_Seq.visualSyncQPC       = now;
        g_Seq.visualPlayheadFrame = (double)frame;
    }

    vis_playhead_unlock();
}

 

