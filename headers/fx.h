#pragma once

#include "config.h"
#include "dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FX_MAX_PARAMS 8
#define FX_MAX_SLOTS  8

#define DSP_PI_F      3.14159265358979323846f
#define DSP_TWO_PI_F  6.28318530717958647692f

enum { FX_PARAM_SLIDER, FX_PARAM_KNOB, FX_PARAM_TOGGLE, FX_PARAM_KNOB_LOG };
enum { FX_TYPE_NONE = 0, FX_TYPE_TEST, FX_TYPE_BUFF, FX_TYPE_DELAY, FX_TYPE_REVERB,
       FX_TYPE_LOFI, FX_TYPE_PHASER, FX_TYPE_CHORUS, FX_TYPE_COMPRESSOR,
       FX_TYPE_RESONATOR };

typedef struct { const char* name; int kind; float min, max, def; const char* fmt; } FxParamDef;
typedef struct FxInstance FxInstance;
typedef struct {
    int type; const char* name;
    int paramCount; const FxParamDef* params;
    void (*init)(FxInstance*, int sr);
    void (*free)(FxInstance*);
    void (*process)(FxInstance*, float* L, float* R);    
} FxDescriptor;
struct FxInstance {
    const FxDescriptor* desc;
    float params[FX_MAX_PARAMS];        
    void* state;                        
};
typedef struct { int count; FxInstance slots[FX_MAX_SLOTS]; } FxChain;

 
extern FxChain g_TrackFx[MAX_TRACKS];

 
typedef struct { int sr; } FxTestState;

static void fx_test_init(FxInstance* fx, int sr) {
    FxTestState* s = (FxTestState*)calloc(1, sizeof(FxTestState));
    if (s) s->sr = sr;
    fx->state = s;
}

static void fx_test_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static void fx_test_process(FxInstance* fx, float* L, float* R) {
    FxTestState* s = (FxTestState*)fx->state;
    if (!s) return;
    float g = fx->params[0];
    *L = denormal_flush_f(*L * g);
    *R = denormal_flush_f(*R * g);
}

 
#define FX_BUFF_MAX_MS 1000.0f

typedef struct {
    float* bufL;
    float* bufR;
    int    cap;           
    int    sr;
    int    writePos;
    float  phase;         
    float  readPos;       
} FxBuffState;

static void fx_buff_init(FxInstance* fx, int sr) {
    FxBuffState* s = (FxBuffState*)calloc(1, sizeof(FxBuffState));
    if (!s) return;
    s->cap = (int)(0.001f * FX_BUFF_MAX_MS * (float)sr) + 8;
    s->bufL = (float*)calloc((size_t)s->cap, sizeof(float));
    s->bufR = (float*)calloc((size_t)s->cap, sizeof(float));
    if (!s->bufL || !s->bufR) {
        free(s->bufL); free(s->bufR); free(s);
        return;
    }
    s->sr = sr;
    fx->state = s;
}

static void fx_buff_free(FxInstance* fx) {
    FxBuffState* s = (FxBuffState*)fx->state;
    if (s) {
        free(s->bufL);
        free(s->bufR);
    }
    free(s);
    fx->state = NULL;
}

static void fx_buff_process(FxInstance* fx, float* L, float* R) {
    FxBuffState* s = (FxBuffState*)fx->state;
    if (!s || !s->bufL || !s->bufR) return;
    float sizeMs = fx->params[0];
     
    float freq   = fx->params[1] * 0.001f;
     
    float rate   = fx->params[2];
    float mix    = fx->params[3] * 0.01f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    if (freq < 0.0f) freq = 0.0f;
    if (rate < 0.01f) rate = 0.01f;
    if (rate > 2.0f)  rate = 2.0f;

    int size = (int)((float)s->sr * 0.001f * sizeMs);
    if (size < 32) size = 32;
    if (size > s->cap) size = s->cap;

    if (s->writePos >= size) s->writePos = 0;
    float inL = *L, inR = *R;
    s->bufL[s->writePos] = inL;
    s->bufR[s->writePos] = inR;

     
    s->readPos += rate;
    if (s->readPos >= (float)size) s->readPos -= (float)size;

     
    s->phase += freq / (float)s->sr;
    if (s->phase >= 1.0f) {
        s->phase -= 1.0f;
        // retrigger: restart at the oldest sample of the current loop window
        int start = s->writePos + 1;
        if (start >= size) start = 0;
        s->readPos = (float)start;
    }

     
    float readPos = s->readPos;
    int ip = (int)readPos;
    if (ip >= size) ip = size - 1;
    float fr = readPos - (float)ip;
    int ip1 = ip + 1;
    if (ip1 >= size) ip1 = 0;
    float wetL = s->bufL[ip] + fr * (s->bufL[ip1] - s->bufL[ip]);
    float wetR = s->bufR[ip] + fr * (s->bufR[ip1] - s->bufR[ip]);

    s->writePos++;
    if (s->writePos >= size) s->writePos = 0;

    *L = denormal_flush_f(inL * (1.0f - mix) + wetL * mix);
    *R = denormal_flush_f(inR * (1.0f - mix) + wetR * mix);
}
 
// --- Delay -------------------------------------------------------------------
#define FX_DELAY_MAX_MS 2000.0f
#define FX_DELAY_SMOOTH 0.002f    

typedef struct {
    float* bufL;
    float* bufR;
    int    cap;
    int    writePos;
    int    sr;
    float  currentDly;     
    float  lpL, lpR;       
    float  hpL, hpR;       // High-pass filter state to prevent feedback mud
    float  lastTone;       
    float  alpha;          
} FxDelayState;

 
static inline float fx_softclip(float x) {
    // Cubic in [-1,1], clamped to ±2/3 outside. Branchless via min/max so the
    // per-sample callers (delay/phaser/chorus/resonator) avoid two branches.
    float y = x - x * x * x * (1.0f / 3.0f);
    return fminf(0.66666667f, fmaxf(-0.66666667f, y));
}

 
static inline float fx_hermite(float p0, float p1, float p2, float p3, float t) {
    float c0 = p1;
    float c1 = 0.5f * (p2 - p0);
    float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// Log-scale parameter mapping for FX_PARAM_KNOB_LOG knobs: the stored param
// value stays in real units (Hz, Q) while the knob position is the normalized
// log distance inside [min, max]. Shared by the rack UI (draw/drag/wheel) and
// the process callbacks (clamping).
static inline float fx_param_norm_log(const FxParamDef* pd, float v) {
    float lo = (pd->min > 0.0001f) ? pd->min : 0.0001f;
    float hi = (pd->max > lo * 4.0f) ? pd->max : lo * 4.0f;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return logf(v / lo) / logf(hi / lo);
}

static inline float fx_param_from_norm_log(const FxParamDef* pd, float norm) {
    float lo = (pd->min > 0.0001f) ? pd->min : 0.0001f;
    float hi = (pd->max > lo * 4.0f) ? pd->max : lo * 4.0f;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return lo * expf(norm * logf(hi / lo));
}

static void fx_delay_init(FxInstance* fx, int sr) {
    FxDelayState* s = (FxDelayState*)calloc(1, sizeof(FxDelayState));
    if (!s) return;
    s->cap = (int)(0.001f * FX_DELAY_MAX_MS * (float)sr) + 8;
    s->bufL = (float*)calloc((size_t)s->cap, sizeof(float));
    s->bufR = (float*)calloc((size_t)s->cap, sizeof(float));
    if (!s->bufL || !s->bufR) {
        free(s->bufL); free(s->bufR); free(s);
        return;
    }
    s->sr = sr;
    s->lastTone = -1.0f;    
    fx->state = s;
}

static void fx_delay_free(FxInstance* fx) {
    FxDelayState* s = (FxDelayState*)fx->state;
    if (s) {
        free(s->bufL);
        free(s->bufR);
    }
    free(s);
    fx->state = NULL;
}

static void fx_delay_process(FxInstance* fx, float* L, float* R) {
    FxDelayState* s = (FxDelayState*)fx->state;
    if (!s || !s->bufL || !s->bufR) return;
    float timeMs   = fx->params[0];
    float fb       = fx->params[1] * 0.01f;
    float toneHz   = fx->params[2];
    float sat      = fx->params[3] * 0.01f;
    int   pingpong = fx->params[4] > 0.5f;
    float mix      = fx->params[5] * 0.01f;
    if (fb < 0.0f) fb = 0.0f;
    if (fb > 0.95f) fb = 0.95f;
    if (toneHz < 500.0f) toneHz = 500.0f;
    if (toneHz > 16000.0f) toneHz = 16000.0f;
    if (sat < 0.0f) sat = 0.0f;
    if (sat > 1.0f) sat = 1.0f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    // Smooth delay time
    float targetDly = 0.001f * timeMs * (float)s->sr;
    if (targetDly < 32.0f) targetDly = 32.0f;
    if (targetDly > (float)(s->cap - 4)) targetDly = (float)(s->cap - 4);
    s->currentDly = denormal_flush_f(s->currentDly + FX_DELAY_SMOOTH * (targetDly - s->currentDly));
    if (s->currentDly < 32.0f) s->currentDly = 32.0f;

    // Tone lowpass filter coefficient
    if (fabsf(toneHz - s->lastTone) > 0.5f) {
        s->alpha = 1.0f - expf(-6.2831853f * toneHz / (float)s->sr);
        if (s->alpha < 0.0f) s->alpha = 0.0f;
        if (s->alpha > 1.0f) s->alpha = 1.0f;
        s->lastTone = toneHz;
    }

    float drive = 1.0f + sat * 2.0f;
    float inL = *L, inR = *R;

    // Hermite fractional delay interpolation
    int dlyI = (int)s->currentDly;
    float dlyF = s->currentDly - (float)dlyI;
    int i1  = s->writePos - dlyI;  if (i1  < 0) i1  += s->cap;
    int i0  = i1 - 1;              if (i0  < 0) i0  += s->cap;
    int im1 = i0 - 1;              if (im1 < 0) im1 += s->cap;
    int i2  = i1 + 1;              if (i2  >= s->cap) i2 -= s->cap;
    float dl = fx_hermite(s->bufL[im1], s->bufL[i0], s->bufL[i1], s->bufL[i2], 1.0f - dlyF);
    float dr = fx_hermite(s->bufR[im1], s->bufR[i0], s->bufR[i1], s->bufR[i2], 1.0f - dlyF);

    // Tone shaping: 1-pole Low-Pass + 1-pole High-Pass (~60 Hz) in feedback loop
    s->lpL = denormal_flush_f(s->lpL + s->alpha * (dl - s->lpL));
    s->lpR = denormal_flush_f(s->lpR + s->alpha * (dr - s->lpR));
    s->hpL = denormal_flush_f(s->hpL + 0.008f * (s->lpL - s->hpL));
    s->hpR = denormal_flush_f(s->hpR + 0.008f * (s->lpR - s->hpR));
    float fltL = s->lpL - s->hpL;
    float fltR = s->lpR - s->hpR;

    // Ping-Pong Routing:
    // If pingpong is active, send mono sum to Left only, and cross feedback.
    float feedL = pingpong ? (inL + inR) * 0.5f : inL;
    float feedR = pingpong ? 0.0f               : inR;
    float fbL   = pingpong ? fltR               : fltL;
    float fbR   = pingpong ? fltL               : fltR;

    s->bufL[s->writePos] = denormal_flush_f(feedL + fx_softclip(fbL * drive) * fb / drive);
    s->bufR[s->writePos] = denormal_flush_f(feedR + fx_softclip(fbR * drive) * fb / drive);
    s->writePos++;
    if (s->writePos >= s->cap) s->writePos = 0;

    // Equal-power dry/wet crossfade (no mid-dial volume dip)
    float mixAngle = mix * (0.5f * DSP_PI_F);
    float dryGain = cosf(mixAngle);
    float wetGain = sinf(mixAngle);

    *L = denormal_flush_f(inL * dryGain + dl * wetGain);
    *R = denormal_flush_f(inR * dryGain + dr * wetGain);
}

// --- Reverb ------------------------------------------------------------------
#define FX_RV_STEREO_SPREAD 23
#define FX_RV_PREDELAY_MAX_MS 150.0f
#define FX_RV_WET_TRIM 0.80f

static const int kFxRvCombBase[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };   
static const int kFxRvApBase[4]   = { 556, 441, 341, 225 };
#define FX_RV_MAX_BASE 1640    

typedef struct { float* buf; int cap; int idx; float store; } FxRvComb;
typedef struct { float* buf; int cap; int idx; }              FxRvAllpass;

typedef struct {
    FxRvComb    combL[8], combR[8];
    FxRvAllpass apL[4], apR[4];
    float*      preL;               
    float*      preR;
    int         preCap;
    int         prePos;
    float       preDlyCur;          
    float       lfoPhase1, lfoPhase2;
    float       srRatio;            
    int         sr;
    int         clenL[8], clenR[8];
    int         alen[4];
    float       fbL[8], fbR[8];     
    float       lastDecay;
    float       hpL, hpR;       // Subsonic rumble filter
} FxReverbState;

static inline float fx_rv_comb_process(FxRvComb* c, float input, int len, float feedback, float dampK) {
    if (c->idx >= len) c->idx = 0;
    float out = c->buf[c->idx];
    c->store = denormal_flush_f(out * (1.0f - dampK) + c->store * dampK);
    c->buf[c->idx] = denormal_flush_f(input + c->store * feedback);
    c->idx++;
    if (c->idx >= len) c->idx = 0;
    return out;
}

// Canonical 1-buffer Schroeder Allpass: exactly flat |H(w)| == 1.0 across all frequencies
static inline float fx_rv_allpass_process(FxRvAllpass* a, float input, int len, float modOff) {
    float delay = (float)len + modOff;
    float rPos = (float)a->idx - delay;
    while (rPos < 0.0f) rPos += (float)a->cap;

    int i0 = (int)rPos;
    float fr = rPos - (float)i0;
    int i1 = i0 + 1;
    if (i0 >= a->cap) i0 -= a->cap;
    if (i1 >= a->cap) i1 -= a->cap;

    float bufout = a->buf[i0] * (1.0f - fr) + a->buf[i1] * fr;
    float output = -0.5f * input + bufout;
    a->buf[a->idx] = denormal_flush_f(input + 0.5f * output);

    a->idx++;
    if (a->idx >= a->cap) a->idx = 0;
    return output;
}

static void fx_reverb_init(FxInstance* fx, int sr) {
    FxReverbState* s = (FxReverbState*)calloc(1, sizeof(FxReverbState));
    if (!s) return;
    int cap = (int)((float)FX_RV_MAX_BASE * (float)sr / 44100.0f) + 64;
    int preCap = (int)(0.001f * FX_RV_PREDELAY_MAX_MS * (float)sr) + 64;
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        s->combL[i].buf = (float*)calloc((size_t)cap, sizeof(float));
        s->combR[i].buf = (float*)calloc((size_t)cap, sizeof(float));
        s->combL[i].cap = s->combR[i].cap = cap;
        if (!s->combL[i].buf || !s->combR[i].buf) ok = false;
    }
    for (int i = 0; i < 4; ++i) {
        s->apL[i].buf = (float*)calloc((size_t)cap, sizeof(float));
        s->apR[i].buf = (float*)calloc((size_t)cap, sizeof(float));
        s->apL[i].cap = s->apR[i].cap = cap;
        if (!s->apL[i].buf || !s->apR[i].buf) ok = false;
    }
    if (ok) {
        s->preL = (float*)calloc((size_t)preCap, sizeof(float));
        s->preR = (float*)calloc((size_t)preCap, sizeof(float));
        if (!s->preL || !s->preR) ok = false;
    }
    if (!ok) {
        for (int i = 0; i < 8; ++i) { free(s->combL[i].buf); free(s->combR[i].buf); }
        for (int i = 0; i < 4; ++i) { free(s->apL[i].buf);   free(s->apR[i].buf); }
        free(s->preL); free(s->preR);
        free(s);
        return;
    }
    s->preCap = preCap;
    s->srRatio = (float)sr / 44100.0f;
    s->sr = sr;
     
    for (int i = 0; i < 8; ++i) {
        s->clenL[i] = (int)((float)kFxRvCombBase[i] * s->srRatio);
        s->clenR[i] = (int)((float)(kFxRvCombBase[i] + FX_RV_STEREO_SPREAD) * s->srRatio);
        if (s->clenL[i] < 8) s->clenL[i] = 8;
        if (s->clenR[i] < 8) s->clenR[i] = 8;
        if (s->clenL[i] > cap) s->clenL[i] = cap;
        if (s->clenR[i] > cap) s->clenR[i] = cap;
    }
    for (int i = 0; i < 4; ++i) {
        s->alen[i] = (int)((float)kFxRvApBase[i] * s->srRatio);
        if (s->alen[i] < 32) s->alen[i] = 32;
        if (s->alen[i] > cap) s->alen[i] = cap;
    }
    s->lastDecay = -1.0f;    
    fx->state = s;
}

static void fx_reverb_free(FxInstance* fx) {
    FxReverbState* s = (FxReverbState*)fx->state;
    if (s) {
        for (int i = 0; i < 8; ++i) { free(s->combL[i].buf); free(s->combR[i].buf); }
        for (int i = 0; i < 4; ++i) { free(s->apL[i].buf);   free(s->apR[i].buf); }
        free(s->preL);
        free(s->preR);
    }
    free(s);
    fx->state = NULL;
}

static void fx_reverb_process(FxInstance* fx, float* L, float* R) {
    FxReverbState* s = (FxReverbState*)fx->state;
    if (!s || !s->preL || !s->preR) return;
    float decay = fx->params[0];
    float preMs = fx->params[1];
    float damp  = fx->params[2] * 0.01f;
    float mod   = fx->params[3] * 0.01f;
    float width = fx->params[4] * 0.01f;
    float mix   = fx->params[5] * 0.01f;
    if (decay < 0.1f) decay = 0.1f;
    if (decay > 10.0f) decay = 10.0f;
    if (preMs < 0.0f) preMs = 0.0f;
    if (preMs > FX_RV_PREDELAY_MAX_MS) preMs = FX_RV_PREDELAY_MAX_MS;
    if (damp < 0.0f) damp = 0.0f;
    if (damp > 1.0f) damp = 1.0f;
    if (mod < 0.0f) mod = 0.0f;
    if (mod > 1.0f) mod = 1.0f;
    if (width < 0.0f) width = 0.0f;
    if (width > 1.0f) width = 1.0f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    float dampK = damp * 0.9f;
    float dryL = *L, dryR = *R;
    float inL = dryL, inR = dryR;

    // Predelay interpolation
    {
        float target = 0.001f * preMs * (float)s->sr;
        if (target > (float)(s->preCap - 2)) target = (float)(s->preCap - 2);
        s->preDlyCur = denormal_flush_f(s->preDlyCur + FX_DELAY_SMOOTH * (target - s->preDlyCur));
        if (s->preDlyCur < 0.0f) s->preDlyCur = 0.0f;
        int pi = (int)s->preDlyCur;
        float pf = s->preDlyCur - (float)pi;

        s->preL[s->prePos] = inL;
        s->preR[s->prePos] = inR;

        int r0 = s->prePos - pi;  if (r0 < 0) r0 += s->preCap;
        int rm = r0 - 1;          if (rm < 0) rm += s->preCap;
        float pl = s->preL[r0] * (1.0f - pf) + s->preL[rm] * pf;
        float pr = s->preR[r0] * (1.0f - pf) + s->preR[rm] * pf;
        s->prePos++;
        if (s->prePos >= s->preCap) s->prePos = 0;
        inL = denormal_flush_f(pl);
        inR = denormal_flush_f(pr);
    }

    // Decay feedback coefficients
    if (fabsf(decay - s->lastDecay) > 0.005f) {
        for (int i = 0; i < 8; ++i) {
            float tL = (float)s->clenL[i] / (float)s->sr;
            float tR = (float)s->clenR[i] / (float)s->sr;
            s->fbL[i] = expf(-6.907755f * tL / decay);
            s->fbR[i] = expf(-6.907755f * tR / decay);
        }
        s->lastDecay = decay;
    }

    // Acoustic spatial cross-feed into parallel comb filters
    float inCombL = (inL * 0.75f + inR * 0.25f) * 0.2f;
    float inCombR = (inR * 0.75f + inL * 0.25f) * 0.2f;
    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < 8; ++i) {
        sumL += fx_rv_comb_process(&s->combL[i], inCombL, s->clenL[i], s->fbL[i], dampK);
        sumR += fx_rv_comb_process(&s->combR[i], inCombR, s->clenR[i], s->fbR[i], dampK);
    }
    sumL = denormal_flush_f(sumL * 0.5f);
    sumR = denormal_flush_f(sumR * 0.5f);

    // Advance LFOs
    s->lfoPhase1 = denormal_flush_f(s->lfoPhase1 + 0.6f  / (float)s->sr);
    if (s->lfoPhase1 >= 1.0f) s->lfoPhase1 -= 1.0f;
    s->lfoPhase2 = denormal_flush_f(s->lfoPhase2 + 0.85f / (float)s->sr);
    if (s->lfoPhase2 >= 1.0f) s->lfoPhase2 -= 1.0f;
    
    // Stereo quadrature modulation: Left uses sin(), Right uses cos()
    float depth = mod * 3.0f * s->srRatio;
    float off1L = depth * (0.5f + 0.5f * sinf(DSP_TWO_PI_F * s->lfoPhase1));
    float off2L = depth * (0.5f + 0.5f * sinf(DSP_TWO_PI_F * s->lfoPhase2 + 1.6f));
    float off1R = depth * (0.5f + 0.5f * cosf(DSP_TWO_PI_F * s->lfoPhase1));
    float off2R = depth * (0.5f + 0.5f * cosf(DSP_TWO_PI_F * s->lfoPhase2 + 1.6f));

    // Allpass diffusion cascade:
    // Detuning Right channel delay lengths decorrelates the stereo field
    sumL = fx_rv_allpass_process(&s->apL[0], sumL, s->alen[0], 0.0f);
    sumL = fx_rv_allpass_process(&s->apL[1], sumL, s->alen[1], 0.0f);
    sumL = fx_rv_allpass_process(&s->apL[2], sumL, s->alen[2], off1L);
    sumL = fx_rv_allpass_process(&s->apL[3], sumL, s->alen[3], off2L);

    sumR = fx_rv_allpass_process(&s->apR[0], sumR, s->alen[0] + 19, 0.0f);
    sumR = fx_rv_allpass_process(&s->apR[1], sumR, s->alen[1] - 13, 0.0f);
    sumR = fx_rv_allpass_process(&s->apR[2], sumR, s->alen[2] + 23, off1R);
    sumR = fx_rv_allpass_process(&s->apR[3], sumR, s->alen[3] - 17, off2R);

    // High-pass filter (~60 Hz) on wet output to avoid subsonic rumble accumulation
    s->hpL = denormal_flush_f(s->hpL + 0.008f * (sumL - s->hpL));
    s->hpR = denormal_flush_f(s->hpR + 0.008f * (sumR - s->hpR));
    sumL -= s->hpL;
    sumR -= s->hpR;

    // Stereo Width M/S Matrix
    float mid  = 0.5f * (sumL + sumR);
    float side = 0.5f * (sumL - sumR);
    sumL = mid + width * side;
    sumR = mid - width * side;

    sumL *= FX_RV_WET_TRIM;
    sumR *= FX_RV_WET_TRIM;

    // Equal-power dry/wet crossfade
    float th = mix * (0.5f * DSP_PI_F);
    float cd = cosf(th);
    float sw = sinf(th);

    *L = denormal_flush_f(dryL * cd + sumL * sw);
    *R = denormal_flush_f(dryR * cd + sumR * sw);
}
 
typedef struct {
    float phase;
    float holdL, holdR;
    float lpL, lpR;
} FxLofiState;

static void fx_lofi_init(FxInstance* fx, int sr) {
    (void)sr;
    fx->state = (FxLofiState*)calloc(1, sizeof(FxLofiState));
}

static void fx_lofi_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static void fx_lofi_process(FxInstance* fx, float* L, float* R) {
    FxLofiState* s = (FxLofiState*)fx->state;
    if (!s) return;
    float rate = fx->params[0];
    float bits = fx->params[1];
    float mix  = fx->params[2] * 0.01f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    float inL = *L, inR = *R;

    float step = rate / (float)SAMPLE_RATE;
    if (step < 0.05f) step = 0.05f;
    if (step > 1.0f)  step = 1.0f;

    s->phase += step;
    if (s->phase >= 1.0f) {
        s->phase -= floorf(s->phase);
        int b = (int)(bits + 0.5f);
        if (b < 8)  b = 8;
        if (b > 12) b = 12;
        float levels = (float)(1 << (b - 1));
        s->holdL = floorf(inL * levels + 0.5f) / levels;
        s->holdR = floorf(inR * levels + 0.5f) / levels;
    }
    s->holdL = denormal_flush_f(s->holdL);
    s->holdR = denormal_flush_f(s->holdR);

    s->lpL = denormal_flush_f(s->lpL + 0.45f * (s->holdL - s->lpL));
    s->lpR = denormal_flush_f(s->lpR + 0.45f * (s->holdR - s->lpR));

    *L = denormal_flush_f(inL * (1.0f - mix) + s->lpL * mix);
    *R = denormal_flush_f(inR * (1.0f - mix) + s->lpR * mix);
}

// --- Phaser ------------------------------------------------------------------
// 6-stage allpass cascade with a quadrature LFO (L/R 90 deg apart) sweeping the
// stage cutoffs and a feedback path around the cascade. Classic through-zero
// style phasing; feedback beyond 0.7 gets aggressively resonant.
#define FX_PHASER_STAGES 6

typedef struct {
    float x1, y1;
} FxPhaserAllpass1;

typedef struct {
    float sr;
    float lfoPhase;
    float fbL, fbR;
    FxPhaserAllpass1 stagesL[FX_PHASER_STAGES];
    FxPhaserAllpass1 stagesR[FX_PHASER_STAGES];
} FxPhaserState;

static void fx_phaser_init(FxInstance* fx, int sr) {
    FxPhaserState* s = (FxPhaserState*)calloc(1, sizeof(FxPhaserState));
    if (s) s->sr = (float)sr;
    fx->state = s;
}

static void fx_phaser_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static void fx_phaser_process(FxInstance* fx, float* L, float* R) {
    FxPhaserState* s = (FxPhaserState*)fx->state;
    if (!s || s->sr <= 0.0f) return;
    // Rack knobs arrive in percent (see kFxPhaserParams); convert to 0..1.
    float rate   = fx->params[0];
    float depth  = fx->params[1] * 0.01f;
    float fbk    = fx->params[2] * 0.01f;
    float mix    = fx->params[3] * 0.01f;
    float minF   = fx->params[4];
    float maxF   = fx->params[5];
    if (rate < 0.05f) rate = 0.05f;
    if (rate > 8.0f)  rate = 8.0f;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    if (fbk < -0.95f) fbk = -0.95f;
    if (fbk >  0.95f) fbk =  0.95f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    // Keep the sweep range sane and always min <= max so tanf stays in region.
    if (minF < 40.0f)  minF = 40.0f;
    if (minF > 4000.0f) minF = 4000.0f;
    if (maxF < minF) maxF = minF;
    if (maxF > s->sr * 0.45f) maxF = s->sr * 0.45f;

    const float inL = *L, inR = *R;

    // Unipolar quadrature LFO: L and R sweep the same range, 90 deg apart.
    s->lfoPhase += rate / s->sr;
    if (s->lfoPhase >= 1.0f) s->lfoPhase -= 1.0f;
    float lfoL = 0.5f * (1.0f + sinf(DSP_TWO_PI_F * s->lfoPhase));
    float lfoR = 0.5f * (1.0f + sinf(DSP_TWO_PI_F * (s->lfoPhase + 0.25f)));
    s->lfoPhase = denormal_flush_f(s->lfoPhase);

    // Exponential (log-pitch) sweep: the LFO glides through musical octaves
    // evenly instead of parking in the harsh 1-4 kHz range of a linear ramp.
    float cutL = minF * powf(maxF / minF, lfoL * depth);
    float cutR = minF * powf(maxF / minF, lfoR * depth);

    // First-order allpass coefficient: a = (tan(pi*fc/fs) - 1) / (tan(pi*fc/fs) + 1)
    float wL = tanf(DSP_PI_F * cutL / s->sr);
    float aL = (wL - 1.0f) / (wL + 1.0f);
    float wR = tanf(DSP_PI_F * cutR / s->sr);
    float aR = (wR - 1.0f) / (wR + 1.0f);

    // Feedback passes through a soft saturator: the 6-stage allpass loop has
    // up to 1/(1-|g|) resonant gain, so raw feedback >0.7 shrieks; the clipper
    // bounds it into a warm controlled resonance instead.
    float curL = denormal_flush_f(inL + fx_softclip(s->fbL * fbk));
    float curR = denormal_flush_f(inR + fx_softclip(s->fbR * fbk));
    for (int st = 0; st < FX_PHASER_STAGES; ++st) {
        FxPhaserAllpass1* ap = &s->stagesL[st];
        float y = aL * curL + ap->x1 - aL * ap->y1;
        ap->x1 = curL;
        ap->y1 = denormal_flush_f(y);
        curL = ap->y1;
    }
    for (int st = 0; st < FX_PHASER_STAGES; ++st) {
        FxPhaserAllpass1* ap = &s->stagesR[st];
        float y = aR * curR + ap->x1 - aR * ap->y1;
        ap->x1 = curR;
        ap->y1 = denormal_flush_f(y);
        curR = ap->y1;
    }
    s->fbL = denormal_flush_f(curL);
    s->fbR = denormal_flush_f(curR);

    // Linear (equal-amplitude) mix: a phaser's comb notches are deepest when
    // dry and wet sum at equal amplitude; an equal-power law would shallow
    // them at 50%. 0.5*(dry+wet) keeps unity gain with both fully present.
    *L = denormal_flush_f((inL + curL) * (0.5f * mix) + inL * (1.0f - mix));
    *R = denormal_flush_f((inR + curR) * (0.5f * mix) + inR * (1.0f - mix));
}

// --- Chorus (4-Voice Dual-Diffused Dimension Architecture) -------------------
#define FX_CHORUS_BUF_SIZE 8192
#define FX_CHORUS_BUF_MASK (FX_CHORUS_BUF_SIZE - 1)

typedef struct {
    float sr;
    float bufL[FX_CHORUS_BUF_SIZE];
    float bufR[FX_CHORUS_BUF_SIZE];
    int   writePos;
    float lfoPhase;
    float fbL, fbR;
    // BBD analog modeling tone filters
    float lpL, lpR;
    float hpL, hpR;
    float hpInL, hpInR;
} FxChorusState;

static void fx_chorus_init(FxInstance* fx, int sr) {
    FxChorusState* s = (FxChorusState*)calloc(1, sizeof(FxChorusState));
    if (s) s->sr = (float)sr;
    fx->state = s;
}

static void fx_chorus_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static inline float fx_chorus_read_hermite(const float* buf, float rPos) {
    while (rPos < 0.0f) rPos += (float)FX_CHORUS_BUF_SIZE;
    int i1 = (int)rPos;
    float fr = rPos - (float)i1;
    int i0 = (i1 - 1) & FX_CHORUS_BUF_MASK;
    int i2 = (i1 + 1) & FX_CHORUS_BUF_MASK;
    int i3 = (i1 + 2) & FX_CHORUS_BUF_MASK;
    i1 &= FX_CHORUS_BUF_MASK;
    return fx_hermite(buf[i0], buf[i1], buf[i2], buf[i3], fr);
}

static void fx_chorus_process(FxInstance* fx, float* L, float* R) {
    FxChorusState* s = (FxChorusState*)fx->state;
    if (!s || s->sr <= 0.0f) return;

    float rate     = fx->params[0];
    float depthMs  = fx->params[1];
    float delayMs  = fx->params[2];
    float fbk      = fx->params[3] * 0.01f;
    float mix      = fx->params[4] * 0.01f;

    if (rate < 0.05f) rate = 0.05f;
    if (rate > 5.0f)  rate = 5.0f;
    if (depthMs < 0.1f) depthMs = 0.1f;
    if (depthMs > 8.0f) depthMs = 8.0f;
    if (delayMs < 5.0f) delayMs = 5.0f;
    if (delayMs > 35.0f) delayMs = 35.0f;
    if (fbk < -0.85f) fbk = -0.85f;
    if (fbk >  0.85f) fbk =  0.85f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    const float inL = *L, inR = *R;
    const float msToSamples = s->sr * 0.001f;
    const float baseSamples = delayMs * msToSamples;
    const float depthSamples = depthMs * msToSamples;

    // Advance master LFO
    s->lfoPhase += rate / s->sr;
    if (s->lfoPhase >= 1.0f) s->lfoPhase -= 1.0f;
    s->lfoPhase = denormal_flush_f(s->lfoPhase);

    // 4-phase quadrature modulators
    float ph = s->lfoPhase;
    float mod1L = sinf(DSP_TWO_PI_F * ph);                 // 0 deg
    float mod2L = sinf(DSP_TWO_PI_F * (ph + 0.5f));        // 180 deg
    float mod1R = sinf(DSP_TWO_PI_F * (ph + 0.25f));       // 90 deg
    float mod2R = sinf(DSP_TWO_PI_F * (ph + 0.75f));       // 270 deg

    // Staggered base delays prevent notch collision
    float d1L = (baseSamples * 0.88f) + depthSamples * mod1L;
    float d2L = (baseSamples * 1.12f) + depthSamples * mod2L;
    float d1R = (baseSamples * 0.88f) + depthSamples * mod1R;
    float d2R = (baseSamples * 1.12f) + depthSamples * mod2R;

    // Feed delay line with soft-clipped feedback
    s->bufL[s->writePos] = denormal_flush_f(inL + fx_softclip(s->fbL * fbk));
    s->bufR[s->writePos] = denormal_flush_f(inR + fx_softclip(s->fbR * fbk));

    // Dual-voice Hermite reads per channel
    float r1L = (float)s->writePos - d1L;
    float r2L = (float)s->writePos - d2L;
    float wetL = 0.55f * fx_chorus_read_hermite(s->bufL, r1L) +
                 0.45f * fx_chorus_read_hermite(s->bufL, r2L);

    float r1R = (float)s->writePos - d1R;
    float r2R = (float)s->writePos - d2R;
    float wetR = 0.55f * fx_chorus_read_hermite(s->bufR, r1R) +
                 0.45f * fx_chorus_read_hermite(s->bufR, r2R);

    // Analog BBD tone conditioning:
    // 1-pole HPF (~90 Hz) keeps bass/kick punch center-solid and un-phased
    const float hpAlpha = 0.988f;
    s->hpL = denormal_flush_f(hpAlpha * (s->hpL + wetL - s->hpInL));
    s->hpInL = wetL;
    wetL = s->hpL;

    s->hpR = denormal_flush_f(hpAlpha * (s->hpR + wetR - s->hpInR));
    s->hpInR = wetR;
    wetR = s->hpR;

    // 1-pole LPF (~7.5 kHz) eliminates harsh BBD clock/comb edge
    const float lpAlpha = 0.58f;
    s->lpL = denormal_flush_f(s->lpL + lpAlpha * (wetL - s->lpL));
    s->lpR = denormal_flush_f(s->lpR + lpAlpha * (wetR - s->lpR));
    wetL = s->lpL;
    wetR = s->lpR;

    s->fbL = wetL;
    s->fbR = wetR;
    s->writePos = (s->writePos + 1) & FX_CHORUS_BUF_MASK;

    // Equal-power crossfade (no volume dip at 50% mix)
    float mixAngle = mix * (0.5f * DSP_PI_F);
    float dryGain = cosf(mixAngle);
    float wetGain = sinf(mixAngle);

    *L = denormal_flush_f(inL * dryGain + wetL * wetGain);
    *R = denormal_flush_f(inR * dryGain + wetR * wetGain);
}

// --- Compressor --------------------------------------------------------------
// Feed-forward stereo-linked compressor (Giannoulis et al. style): peak
// sidechain in the dB domain with decoupled attack/release ballistics and a
// quadratic soft knee.
typedef struct {
    float sr;
    float alphaAttack;
    float alphaRelease;
    float scAlpha;        // ~5 ms sidechain pre-smoother coefficient
    float sidechainEnv;   // linear pre-smoothed rectified peak (bridges zero crossings)
    float envDb;
    float lastAttackMs;
    float lastReleaseMs;
} FxCompressorState;

static void fx_compressor_init(FxInstance* fx, int sr) {
    FxCompressorState* s = (FxCompressorState*)calloc(1, sizeof(FxCompressorState));
    if (s) {
        s->sr = (float)sr;
        s->envDb = 0.0f;   // unity gain: -120 here would crush on startup
        s->sidechainEnv = 0.0f;
        s->scAlpha = (sr > 0) ? (1.0f - expf(-1.0f / ((float)sr * 0.005f))) : 0.1f;
        s->lastAttackMs = -1.0f;
        s->lastReleaseMs = -1.0f;
    }
    fx->state = s;
}

static void fx_compressor_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static void fx_compressor_process(FxInstance* fx, float* L, float* R) {
    FxCompressorState* s = (FxCompressorState*)fx->state;
    if (!s || s->sr <= 0.0f) return;
    float threshDb = fx->params[0];
    float ratio    = fx->params[1];
    float attackMs = fx->params[2];
    float releaseMs= fx->params[3];
    float kneeDb   = fx->params[4];
    float makeupDb = fx->params[5];
    if (threshDb < -60.0f) threshDb = -60.0f;
    if (threshDb > 0.0f)   threshDb = 0.0f;
    if (ratio < 1.0f) ratio = 1.0f;
    if (ratio > 30.0f) ratio = 30.0f;
    if (attackMs < 0.1f) attackMs = 0.1f;
    if (attackMs > 100.0f) attackMs = 100.0f;
    if (releaseMs < 10.0f) releaseMs = 10.0f;
    if (releaseMs > 1000.0f) releaseMs = 1000.0f;
    if (kneeDb < 0.0f) kneeDb = 0.0f;
    if (kneeDb > 20.0f) kneeDb = 20.0f;
    if (makeupDb < 0.0f) makeupDb = 0.0f;
    if (makeupDb > 30.0f) makeupDb = 30.0f;

    // Ballistic coefficients only change when the knob moved (log/expf cost).
    if (fabsf(attackMs - s->lastAttackMs) > 0.01f) {
        s->alphaAttack = expf(-1.0f / (s->sr * attackMs * 0.001f));
        s->lastAttackMs = attackMs;
    }
    if (fabsf(releaseMs - s->lastReleaseMs) > 0.01f) {
        s->alphaRelease = expf(-1.0f / (s->sr * releaseMs * 0.001f));
        s->lastReleaseMs = releaseMs;
    }

    const float inL = *L, inR = *R;

    // Stereo-linked peak detection with a ~5 ms linear pre-smoother: an
    // instantaneous rectifier drops to 0 at every zero crossing, which made
    // fast releases stutter at 2x the signal frequency (bass buzz). The
    // smoother bridges the crossings so the dB conversion sees a steady
    // envelope.
    float absL = fabsf(inL);
    float absR = fabsf(inR);
    float peak = (absL > absR) ? absL : absR;
    s->sidechainEnv = denormal_flush_f(s->sidechainEnv + s->scAlpha * (peak - s->sidechainEnv));
    float sc = s->sidechainEnv;
    if (sc < 1e-6f) sc = 1e-6f;
    float inDb = 20.0f * log10f(sc);

    // Static curve with quadratic soft knee around the threshold.
    float halfKnee = kneeDb * 0.5f;
    float delta = inDb - threshDb;
    float targetGrDb = 0.0f;
    if (delta <= -halfKnee) {
        targetGrDb = 0.0f;
    } else if (delta > halfKnee || kneeDb < 0.001f) {
        targetGrDb = (1.0f / ratio - 1.0f) * delta;
    } else {
        float x = delta + halfKnee;
        targetGrDb = (1.0f / ratio - 1.0f) * (x * x) / (2.0f * kneeDb);
    }

    // Decoupled ballistics in the dB domain (attack down, release up).
    if (targetGrDb < s->envDb) {
        s->envDb = s->alphaAttack * s->envDb + (1.0f - s->alphaAttack) * targetGrDb;
    } else {
        s->envDb = s->alphaRelease * s->envDb + (1.0f - s->alphaRelease) * targetGrDb;
    }
    s->envDb = denormal_flush_f(s->envDb);
    if (s->envDb < -120.0f) s->envDb = -120.0f;

    // Reduction + makeup folded into one powf (3 transcendental calls per
    // sample -> 2: the smoother's expf is precomputed at init/param change).
    float gain = powf(10.0f, (s->envDb + makeupDb) * 0.05f);
    *L = denormal_flush_f(inL * gain);
    *R = denormal_flush_f(inR * gain);
}

// --- Resonator (ZDF SVF Note-Tuned Resonator) ---------------------------------
typedef struct {
    float sr;
    float s1L, s2L, s1R, s2R;
    float g, k;
    float bpGain;
    float lastNote;
    float lastQ;
} FxResonatorState;

static void fx_resonator_init(FxInstance* fx, int sr) {
    FxResonatorState* s = (FxResonatorState*)calloc(1, sizeof(FxResonatorState));
    if (s) {
        s->sr = (float)sr;
        s->lastNote = -999.0f;
        s->lastQ    = -999.0f;
    }
    fx->state = s;
}

static void fx_resonator_free(FxInstance* fx) {
    free(fx->state);
    fx->state = NULL;
}

static void fx_resonator_process(FxInstance* fx, float* L, float* R) {
    FxResonatorState* s = (FxResonatorState*)fx->state;
    if (!s || s->sr <= 0.0f) return;

    // Clean 3-parameter layout: exactly matches paramCount = 3
    float note = fx->params[0];
    float q    = fx->params[1];
    float mix  = fx->params[2] * 0.01f;

    if (note < 24.0f) note = 24.0f;     // C1 (32.7 Hz)
    if (note > 96.0f) note = 96.0f;     // C7 (2093.0 Hz)
    if (q < 1.0f) q = 1.0f;
    if (q > 35.0f) q = 35.0f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    // Recalculate coefficients only when controls move
    if (fabsf(note - s->lastNote) > 0.001f || fabsf(q - s->lastQ) > 0.01f) {
        // Standard MIDI note to Hertz formula: A4 (Note 69) = 440 Hz
        float freq = 440.0f * powf(2.0f, (note - 69.0f) * (1.0f / 12.0f));
        if (freq < 20.0f) freq = 20.0f;
        if (freq > s->sr * 0.45f) freq = s->sr * 0.45f;

        // Bilinear pre-warped integrator gain
        s->g = tanf(DSP_PI_F * freq / s->sr);
        s->k = 1.0f / q;

        // ENERGY COMPENSATION:
        // v1 has peak gain = Q. Normalized bandpass is (v1 / Q).
        // To maintain equal perceived loudness across all Q values, the target
        // peak gain is sqrt(Q). Therefore: gain = (1 / Q) * sqrt(Q) = 1 / sqrt(Q).
        s->bpGain = 1.0f / sqrtf(q);

        s->lastNote = note;
        s->lastQ    = q;
    }

    const float inL = *L, inR = *R;
    const float g = s->g, k = s->k;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    // Left Channel ZDF SVF
    float v3L = inL - s->s2L;
    float v1L = a1 * s->s1L + a2 * v3L; // Bandpass tap (peak gain = Q)
    float v2L = s->s2L + a2 * s->s1L + a3 * v3L;
    s->s1L = denormal_flush_f(2.0f * v1L - s->s1L);
    s->s2L = denormal_flush_f(2.0f * v2L - s->s2L);

    // Right Channel ZDF SVF
    float v3R = inR - s->s2R;
    float v1R = a1 * s->s1R + a2 * v3R; // Bandpass tap (peak gain = Q)
    float v2R = s->s2R + a2 * s->s1R + a3 * v3R;
    s->s1R = denormal_flush_f(2.0f * v1R - s->s1R);
    s->s2R = denormal_flush_f(2.0f * v2R - s->s2R);

    // Apply exact energy compensation + musical softclip
    float wetL = fx_softclip(v1L * s->bpGain);
    float wetR = fx_softclip(v1R * s->bpGain);

    *L = denormal_flush_f(inL * (1.0f - mix) + wetL * mix);
    *R = denormal_flush_f(inR * (1.0f - mix) + wetR * mix);
}

 
static const FxParamDef kFxTestParams[] = {
    { "Gain", FX_PARAM_SLIDER, 0.0f, 2.0f, 1.0f, "%.2fx" },
};

static const FxParamDef kFxBuffParams[] = {
    { "Size", FX_PARAM_KNOB, 10.0f, 1000.0f, 250.0f, "%.0f ms" },
    { "Ramp", FX_PARAM_KNOB, 30.0f, 30000.0f, 1000.0f, "%.0f mHz" },
    { "Rate", FX_PARAM_KNOB, 0.01f, 2.0f, 1.0f, "%.2fx" },
    { "Mix",  FX_PARAM_KNOB, 0.0f, 100.0f, 50.0f, "%.0f%%" },
};

static const FxParamDef kFxDelayParams[] = {
    { "Time", FX_PARAM_KNOB, 10.0f,  2000.0f,  250.0f,  "%.0f ms" },
    { "Feedback", FX_PARAM_KNOB, 0.0f,   95.0f,   40.0f,   "%.0f%%" },
    { "Tone", FX_PARAM_KNOB, 500.0f, 16000.0f, 6000.0f, "%.0f Hz" },
    { "Saturation",  FX_PARAM_KNOB, 0.0f,   100.0f,  15.0f,   "%.0f%%" },
    { "Mode", FX_PARAM_TOGGLE, 0.0f,   1.0f,    0.0f,    "%.0f" },
    { "Mix",  FX_PARAM_KNOB,   0.0f,   100.0f,  30.0f,   "%.0f%%" },
};

static const FxParamDef kFxReverbParams[] = {
    { "Decay",  FX_PARAM_KNOB, 0.1f,  10.0f,  2.5f,   "%.1f s" },
    { "Predelay", FX_PARAM_KNOB, 0.0f,  150.0f, 20.0f,  "%.0f ms" },
    { "Damp",   FX_PARAM_KNOB, 0.0f,  100.0f, 40.0f,  "%.0f%%" },
    { "Mod",    FX_PARAM_KNOB, 0.0f,  100.0f, 25.0f,  "%.0f%%" },
    { "Width",  FX_PARAM_KNOB, 0.0f,  100.0f, 100.0f, "%.0f%%" },
    { "Mix",    FX_PARAM_KNOB, 0.0f,  100.0f, 25.0f,  "%.0f%%" },
};

static const FxParamDef kFxLofiParams[] = {
    { "Rate", FX_PARAM_SLIDER, 8000.0f, 32000.0f, 14700.0f, "%.0f Hz" },
    { "Bits", FX_PARAM_SLIDER, 8.0f, 12.0f, 12.0f, "%.0f bit" },
    { "Mix",  FX_PARAM_KNOB,   0.0f, 100.0f, 100.0f, "%.0f%%" },
};

static const FxParamDef kFxPhaserParams[] = {
    { "Rate",  FX_PARAM_KNOB,     0.1f,   5.0f,    0.5f,    "%.2f Hz" },
    { "Depth", FX_PARAM_KNOB,     0.0f,   100.0f,  75.0f,   "%.0f%%" },
    { "Feedback",  FX_PARAM_KNOB,     -95.0f, 95.0f,   60.0f,   "%.0f%%" },
    { "Mix",   FX_PARAM_KNOB,     0.0f,   100.0f,  50.0f,   "%.0f%%" },
    { "Low",   FX_PARAM_KNOB_LOG, 20.0f,  2000.0f, 440.0f,  "%.0f Hz" },
    { "High",  FX_PARAM_KNOB_LOG, 200.0f, 20000.0f, 3200.0f, "%.0f Hz" },
};

static const FxParamDef kFxChorusParams[] = {
    { "Rate",  FX_PARAM_KNOB, 0.1f,  3.0f,  0.8f,  "%.2f Hz" },
    { "Depth", FX_PARAM_KNOB, 0.1f,  5.0f,  2.0f,  "%.1f ms" },
    { "Delay", FX_PARAM_KNOB, 5.0f,  25.0f, 12.0f, "%.1f ms" },
    { "Feedback",  FX_PARAM_KNOB, -70.0f, 70.0f, 20.0f, "%.0f%%" },
    { "Mix",   FX_PARAM_KNOB, 0.0f,  100.0f, 50.0f, "%.0f%%" },
};

static const FxParamDef kFxCompressorParams[] = {
    { "Threshold", FX_PARAM_KNOB, -60.0f, 0.0f,   -12.0f, "%.0f dB" },
    { "Ratio",  FX_PARAM_KNOB, 1.0f,   30.0f,  4.0f,   "%.1f:1" },
    { "Attack", FX_PARAM_KNOB, 0.1f,   100.0f, 10.0f,  "%.1f ms" },
    { "Release",    FX_PARAM_KNOB, 10.0f,  1000.0f, 100.0f, "%.0f ms" },
    { "Knee",   FX_PARAM_KNOB, 0.0f,   20.0f,  4.0f,   "%.0f dB" },
    { "Makeup", FX_PARAM_KNOB, 0.0f,   30.0f,  0.0f,   "%.1f dB" },
};

static const FxParamDef kFxResonatorParams[] = {
    { "Note", FX_PARAM_SLIDER, 24.0f, 96.0f,  60.0f, "%.0f" }, // MIDI note 24 (C1) to 96 (C7), def 60 (C4)
    { "Resonance",    FX_PARAM_SLIDER, 1.0f,  35.0f,  10.0f, "%.1f" }, // Musical Q range
    { "Mix",  FX_PARAM_KNOB,   0.0f,  100.0f, 50.0f, "%.0f%%" },
};

static const FxDescriptor kFxDescriptors[] = {
    { FX_TYPE_TEST,   "Gain",   1, kFxTestParams,   fx_test_init,   fx_test_free,   fx_test_process   },
    { FX_TYPE_BUFF,   "Buff",   4, kFxBuffParams,   fx_buff_init,   fx_buff_free,   fx_buff_process   },
    { FX_TYPE_DELAY,  "Delay",  6, kFxDelayParams,  fx_delay_init,  fx_delay_free,  fx_delay_process  },
    { FX_TYPE_REVERB, "Reverb", 6, kFxReverbParams, fx_reverb_init, fx_reverb_free, fx_reverb_process },
    { FX_TYPE_LOFI,   "Lofi",   3, kFxLofiParams,   fx_lofi_init,   fx_lofi_free,   fx_lofi_process   },
    { FX_TYPE_PHASER,      "Phaser",      6, kFxPhaserParams,      fx_phaser_init,      fx_phaser_free,      fx_phaser_process      },
    { FX_TYPE_CHORUS,      "Chorus",      5, kFxChorusParams,      fx_chorus_init,      fx_chorus_free,      fx_chorus_process      },
    { FX_TYPE_COMPRESSOR,  "Compressor",  6, kFxCompressorParams,  fx_compressor_init,  fx_compressor_free,  fx_compressor_process  },
    { FX_TYPE_RESONATOR, "Resonator", 3, kFxResonatorParams, fx_resonator_init, fx_resonator_free, fx_resonator_process },
};

#define FX_DESCRIPTOR_COUNT ((int)(sizeof(kFxDescriptors) / sizeof(kFxDescriptors[0])))

static inline const FxDescriptor* fx_descriptor_by_index(int i) {
    if (i < 0 || i >= FX_DESCRIPTOR_COUNT) return NULL;
    return &kFxDescriptors[i];
}

static inline const FxDescriptor* fx_descriptor_by_type(int type) {
    for (int i = 0; i < FX_DESCRIPTOR_COUNT; ++i)
        if (kFxDescriptors[i].type == type) return &kFxDescriptors[i];
    return NULL;
}

 
static inline void fx_instance_init(FxInstance* fx, int sr) {
    if (fx->desc && fx->desc->init) fx->desc->init(fx, sr);
}

static inline void fx_instance_free(FxInstance* fx) {
    if (fx->desc && fx->desc->free) fx->desc->free(fx);
    fx->state = NULL;
}

 
static inline bool fx_chain_insert(FxChain* c, const FxDescriptor* desc, int slot, int sr) {
    if (!c || !desc || c->count < 0 || c->count >= FX_MAX_SLOTS) return false;
    if (slot < 0 || slot > c->count) slot = c->count;
    for (int i = c->count; i > slot; --i) c->slots[i] = c->slots[i - 1];
    FxInstance* fx = &c->slots[slot];
    fx->desc = desc;
    for (int p = 0; p < FX_MAX_PARAMS; ++p) fx->params[p] = 0.0f;
    for (int p = 0; p < desc->paramCount && p < FX_MAX_PARAMS; ++p)
        fx->params[p] = desc->params[p].def;
    fx->state = NULL;
    fx_instance_init(fx, sr);
    c->count++;
    return true;
}

static inline void fx_chain_remove_at(FxChain* c, int slot) {
    if (!c || slot < 0 || slot >= c->count) return;
    fx_instance_free(&c->slots[slot]);
    for (int i = slot; i < c->count - 1; ++i) c->slots[i] = c->slots[i + 1];
    c->count--;
    memset(&c->slots[c->count], 0, sizeof(FxInstance));
}

static inline void fx_chain_move(FxChain* c, int from, int to) {
    if (!c || from < 0 || from >= c->count) return;
    if (to < 0) to = 0;
    if (to > c->count - 1) to = c->count - 1;
    if (to == from) return;
    FxInstance tmp = c->slots[from];
    if (to < from) {
        for (int i = from; i > to; --i) c->slots[i] = c->slots[i - 1];
    } else {
        for (int i = from; i < to; ++i) c->slots[i] = c->slots[i + 1];
    }
    c->slots[to] = tmp;
}

static inline void fx_chain_clear(FxChain* c) {
    if (!c) return;
    for (int i = 0; i < c->count && i < FX_MAX_SLOTS; ++i)
        fx_instance_free(&c->slots[i]);
    memset(c, 0, sizeof(*c));
}

 
static inline void fx_chain_process(FxChain* c, float* L, float* R) {
    if (!c) return;
    for (int i = 0; i < c->count && i < FX_MAX_SLOTS; ++i) {
        FxInstance* fx = &c->slots[i];
        if (fx->desc && fx->desc->process) fx->desc->process(fx, L, R);
    }
}

 
static inline void fx_chain_to_snapshot(FxChain* dst, const FxChain* src) {
    if (!dst || !src || dst == src) return;
    fx_chain_clear(dst);
    dst->count = (src->count >= 0 && src->count <= FX_MAX_SLOTS) ? src->count : 0;
    for (int i = 0; i < dst->count; ++i) {
        FxInstance* d = &dst->slots[i];
        const FxInstance* s = &src->slots[i];
        d->desc = s->desc;
        memcpy(d->params, s->params, sizeof(d->params));
        d->state = NULL;
        fx_instance_init(d, SAMPLE_RATE);
    }
}

 
static inline void fx_chain_load(FxChain* c, int count, const int* types, const float* paramsFlat, int sr) {
    if (!c || !types || !paramsFlat) return;
    fx_chain_clear(c);
    if (count < 0) count = 0;
    if (count > FX_MAX_SLOTS) count = FX_MAX_SLOTS;
    for (int i = 0; i < count; ++i) {
        const FxDescriptor* d = fx_descriptor_by_type(types[i]);
        if (!d) continue;
        if (!fx_chain_insert(c, d, c->count, sr)) break;
        FxInstance* fx = &c->slots[c->count - 1];
        for (int p = 0; p < d->paramCount && p < FX_MAX_PARAMS; ++p) {
            const FxParamDef* pd = &d->params[p];
            float v = paramsFlat[i * FX_MAX_PARAMS + p];
            if (isnan(v)) v = pd->def;
            if (v < pd->min) v = pd->min;
            if (v > pd->max) v = pd->max;
            fx->params[p] = v;
        }
    }
}

 
static inline void fx_init_all(void) {
    for (int t = 0; t < MAX_TRACKS; ++t)
        fx_chain_clear(&g_TrackFx[t]);
}
