#include "eq.h"
#include <math.h>
#include <stdlib.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline float pin_param(float val) {
    if (val < 0.0f) return 0.0f;
    if (val > 1.0f) return 1.0f;
    return val;
}

static inline double calculate_gain(float param) {
    double g = ((double)param - 0.5) * 2.0;
    return 1.0 + (g * fabs(g) * fabs(g));
}

static void update_coefficients(SmoothEQ3* eq) {
    if (!eq->dirty_coeffs) return;

    eq->treble_gain = calculate_gain(eq->param_high);
    eq->mid_gain    = calculate_gain(eq->param_mid);
    eq->bass_gain   = calculate_gain(eq->param_bass);

    double sr = eq->sample_rate > 0.0 ? eq->sample_rate : 44100.0;

     
    double high_freq = 4000.0 / sr;
    if (high_freq > 0.4999) high_freq = 0.4999;

    double omega = 2.0 * M_PI * high_freq;
    double K_iir = 2.0 - cos(omega);
    eq->high_coef = -sqrt(K_iir * K_iir - 1.0) + K_iir;

    double K_biq = tan(M_PI * high_freq);
    double norm = 1.0 / (1.0 + K_biq + (K_biq * K_biq));
    eq->high_a0 = K_biq * K_biq * norm;
    eq->high_a1 = 2.0 * eq->high_a0;
    eq->high_a2 = eq->high_a0;
    eq->high_b1 = 2.0 * (K_biq * K_biq - 1.0) * norm;
    eq->high_b2 = (1.0 - K_biq + (K_biq * K_biq)) * norm;

     
    double low_freq = 200.0 / sr;
    if (low_freq > 0.4999) low_freq = 0.4999;

    omega = 2.0 * M_PI * low_freq;
    K_iir = 2.0 - cos(omega);
    eq->low_coef = -sqrt(K_iir * K_iir - 1.0) + K_iir;

    K_biq = tan(M_PI * low_freq);
    norm = 1.0 / (1.0 + K_biq + (K_biq * K_biq));
    eq->low_a0 = K_biq * K_biq * norm;
    eq->low_a1 = 2.0 * eq->low_a0;
    eq->low_a2 = eq->low_a0;
    eq->low_b1 = 2.0 * (K_biq * K_biq - 1.0) * norm;
    eq->low_b2 = (1.0 - K_biq + (K_biq * K_biq)) * norm;

    eq->dirty_coeffs = false;
}

void smooth_eq3_reset(SmoothEQ3* eq) {
    eq->high_sL1 = 0.0; eq->high_sL2 = 0.0;
    eq->high_sR1 = 0.0; eq->high_sR2 = 0.0;
    eq->low_sL1  = 0.0; eq->low_sL2  = 0.0;
    eq->low_sR1  = 0.0; eq->low_sR2  = 0.0;

    eq->high_fast_L_IIR = 0.0;
    eq->high_fast_R_IIR = 0.0;
    eq->low_fast_L_IIR  = 0.0;
    eq->low_fast_R_IIR  = 0.0;
}

void smooth_eq3_init(SmoothEQ3* eq, double sample_rate) {
    eq->sample_rate = (sample_rate > 0.0) ? sample_rate : 44100.0;
    eq->param_high = 0.5f;
    eq->param_mid  = 0.5f;
    eq->param_bass = 0.5f;

    eq->fpdL = 1; while (eq->fpdL < 16386) eq->fpdL = (uint32_t)rand() * 16387 + (uint32_t)rand();
    eq->fpdR = 1; while (eq->fpdR < 16386) eq->fpdR = (uint32_t)rand() * 16387 + (uint32_t)rand();

    smooth_eq3_reset(eq);
    eq->dirty_coeffs = true;
    update_coefficients(eq);
}

void smooth_eq3_set_sample_rate(SmoothEQ3* eq, double sample_rate) {
    if (eq->sample_rate != sample_rate) {
        eq->sample_rate = sample_rate;
        eq->dirty_coeffs = true;
    }
}

void smooth_eq3_set_params(SmoothEQ3* eq, float high, float mid, float bass) {
    eq->param_high = pin_param(high);
    eq->param_mid  = pin_param(mid);
    eq->param_bass = pin_param(bass);
    eq->dirty_coeffs = true;
}

void smooth_eq3_process_float(SmoothEQ3* eq, const float* inL, const float* inR, float* outL, float* outR, uint32_t sample_frames) {
    update_coefficients(eq);

    const double trebleGain = eq->treble_gain, midGain = eq->mid_gain, bassGain = eq->bass_gain;
    const double h_a0 = eq->high_a0, h_a1 = eq->high_a1, h_a2 = eq->high_a2, h_b1 = eq->high_b1, h_b2 = eq->high_b2;
    const double l_a0 = eq->low_a0,  l_a1 = eq->low_a1,  l_a2 = eq->low_a2,  l_b1 = eq->low_b1,  l_b2 = eq->low_b2;
    const double highCoef = eq->high_coef, lowCoef = eq->low_coef;

    while (sample_frames-- > 0) {
        double inputSampleL = *inL++, inputSampleR = *inR++;
        // NaN/Inf must never enter the persistent IIR state: gate them to
        // silence up front (matches peak_biquad_process / track_filter).
        if (!_finite(inputSampleL)) inputSampleL = 0.0;
        if (!_finite(inputSampleR)) inputSampleR = 0.0;
        // Denormal dither only: replace a genuine denormal with a tiny normal
        // so it can't stall the FPU, but never turn a true 0/silence into
        // signal (that would raise the noise floor on quiet buses).
        if (fabs(inputSampleL) > 0.0 && fabs(inputSampleL) < 1.18e-23) inputSampleL = eq->fpdL * 1.18e-17;
        if (fabs(inputSampleR) > 0.0 && fabs(inputSampleR) < 1.18e-23) inputSampleR = eq->fpdR * 1.18e-17;

        double trebleFastL = inputSampleL;
        double outSample = (trebleFastL * h_a0) + eq->high_sL1;
        eq->high_sL1 = (trebleFastL * h_a1) - (outSample * h_b1) + eq->high_sL2;
        eq->high_sL2 = (trebleFastL * h_a2) - (outSample * h_b2);
        double midFastL = outSample; trebleFastL -= midFastL;

        outSample = (midFastL * l_a0) + eq->low_sL1;
        eq->low_sL1 = (midFastL * l_a1) - (outSample * l_b1) + eq->low_sL2;
        eq->low_sL2 = (midFastL * l_a2) - (outSample * l_b2);
        double bassFastL = outSample; midFastL -= bassFastL;

        trebleFastL = (bassFastL * bassGain) + (midFastL * midGain) + (trebleFastL * trebleGain);
        eq->high_fast_L_IIR = (eq->high_fast_L_IIR * highCoef) + (trebleFastL * (1.0 - highCoef));
        midFastL = eq->high_fast_L_IIR; trebleFastL -= midFastL;

        eq->low_fast_L_IIR = (eq->low_fast_L_IIR * lowCoef) + (midFastL * (1.0 - lowCoef));
        bassFastL = eq->low_fast_L_IIR; midFastL -= bassFastL;

        inputSampleL = (bassFastL * bassGain) + (midFastL * midGain) + (trebleFastL * trebleGain);

        double trebleFastR = inputSampleR;
        outSample = (trebleFastR * h_a0) + eq->high_sR1;
        eq->high_sR1 = (trebleFastR * h_a1) - (outSample * h_b1) + eq->high_sR2;
        eq->high_sR2 = (trebleFastR * h_a2) - (outSample * h_b2);
        double midFastR = outSample; trebleFastR -= midFastR;

        outSample = (midFastR * l_a0) + eq->low_sR1;
        eq->low_sR1 = (midFastR * l_a1) - (outSample * l_b1) + eq->low_sR2;
        eq->low_sR2 = (midFastR * l_a2) - (outSample * l_b2);
        double bassFastR = outSample; midFastR -= bassFastR;

        trebleFastR = (bassFastR * bassGain) + (midFastR * midGain) + (trebleFastR * trebleGain);
        eq->high_fast_R_IIR = (eq->high_fast_R_IIR * highCoef) + (trebleFastR * (1.0 - highCoef));
        midFastR = eq->high_fast_R_IIR; trebleFastR -= midFastR;

        eq->low_fast_R_IIR = (eq->low_fast_R_IIR * lowCoef) + (midFastR * (1.0 - lowCoef));
        bassFastR = eq->low_fast_R_IIR; midFastR -= bassFastR;

        inputSampleR = (bassFastR * bassGain) + (midFastR * midGain) + (trebleFastR * trebleGain);

        int expon; (void)frexpf((float)inputSampleL, &expon);
        eq->fpdL ^= eq->fpdL << 13; eq->fpdL ^= eq->fpdL >> 17; eq->fpdL ^= eq->fpdL << 5;
        inputSampleL += ((double)eq->fpdL - (double)0x7fffffff) * 5.5e-36 * ldexp(1.0, expon + 62);

        (void)frexpf((float)inputSampleR, &expon);
        eq->fpdR ^= eq->fpdR << 13; eq->fpdR ^= eq->fpdR >> 17; eq->fpdR ^= eq->fpdR << 5;
        inputSampleR += ((double)eq->fpdR - (double)0x7fffffff) * 5.5e-36 * ldexp(1.0, expon + 62);

        *outL++ = (float)inputSampleL; *outR++ = (float)inputSampleR;
    }
}

void smooth_eq3_process_double(SmoothEQ3* eq, const double* inL, const double* inR, double* outL, double* outR, uint32_t sample_frames) {
    update_coefficients(eq);

    const double trebleGain = eq->treble_gain, midGain = eq->mid_gain, bassGain = eq->bass_gain;
    const double h_a0 = eq->high_a0, h_a1 = eq->high_a1, h_a2 = eq->high_a2, h_b1 = eq->high_b1, h_b2 = eq->high_b2;
    const double l_a0 = eq->low_a0,  l_a1 = eq->low_a1,  l_a2 = eq->low_a2,  l_b1 = eq->low_b1,  l_b2 = eq->low_b2;
    const double highCoef = eq->high_coef, lowCoef = eq->low_coef;

    while (sample_frames-- > 0) {
        double inputSampleL = *inL++, inputSampleR = *inR++;
        // NaN/Inf must never enter the persistent IIR state: gate them to
        // silence up front (matches peak_biquad_process / track_filter).
        if (!_finite(inputSampleL)) inputSampleL = 0.0;
        if (!_finite(inputSampleR)) inputSampleR = 0.0;
        // Denormal dither only: replace a genuine denormal with a tiny normal
        // so it can't stall the FPU, but never turn a true 0/silence into
        // signal (that would raise the noise floor on quiet buses).
        if (fabs(inputSampleL) > 0.0 && fabs(inputSampleL) < 1.18e-23) inputSampleL = eq->fpdL * 1.18e-17;
        if (fabs(inputSampleR) > 0.0 && fabs(inputSampleR) < 1.18e-23) inputSampleR = eq->fpdR * 1.18e-17;

        double trebleFastL = inputSampleL;
        double outSample = (trebleFastL * h_a0) + eq->high_sL1;
        eq->high_sL1 = (trebleFastL * h_a1) - (outSample * h_b1) + eq->high_sL2;
        eq->high_sL2 = (trebleFastL * h_a2) - (outSample * h_b2);
        double midFastL = outSample; trebleFastL -= midFastL;

        outSample = (midFastL * l_a0) + eq->low_sL1;
        eq->low_sL1 = (midFastL * l_a1) - (outSample * l_b1) + eq->low_sL2;
        eq->low_sL2 = (midFastL * l_a2) - (outSample * l_b2);
        double bassFastL = outSample; midFastL -= bassFastL;

        trebleFastL = (bassFastL * bassGain) + (midFastL * midGain) + (trebleFastL * trebleGain);
        eq->high_fast_L_IIR = (eq->high_fast_L_IIR * highCoef) + (trebleFastL * (1.0 - highCoef));
        midFastL = eq->high_fast_L_IIR; trebleFastL -= midFastL;

        eq->low_fast_L_IIR = (eq->low_fast_L_IIR * lowCoef) + (midFastL * (1.0 - lowCoef));
        bassFastL = eq->low_fast_L_IIR; midFastL -= bassFastL;

        inputSampleL = (bassFastL * bassGain) + (midFastL * midGain) + (trebleFastL * trebleGain);

        double trebleFastR = inputSampleR;
        outSample = (trebleFastR * h_a0) + eq->high_sR1;
        eq->high_sR1 = (trebleFastR * h_a1) - (outSample * h_b1) + eq->high_sR2;
        eq->high_sR2 = (trebleFastR * h_a2) - (outSample * h_b2);
        double midFastR = outSample; trebleFastR -= midFastR;

        outSample = (midFastR * l_a0) + eq->low_sR1;
        eq->low_sR1 = (midFastR * l_a1) - (outSample * l_b1) + eq->low_sR2;
        eq->low_sR2 = (midFastR * l_a2) - (outSample * l_b2);
        double bassFastR = outSample; midFastR -= bassFastR;

        trebleFastR = (bassFastR * bassGain) + (midFastR * midGain) + (trebleFastR * trebleGain);
        eq->high_fast_R_IIR = (eq->high_fast_R_IIR * highCoef) + (trebleFastR * (1.0 - highCoef));
        midFastR = eq->high_fast_R_IIR; trebleFastR -= midFastR;

        eq->low_fast_R_IIR = (eq->low_fast_R_IIR * lowCoef) + (midFastR * (1.0 - lowCoef));
        bassFastR = eq->low_fast_R_IIR; midFastR -= bassFastR;

        inputSampleR = (bassFastR * bassGain) + (midFastR * midGain) + (trebleFastR * trebleGain);

        *outL++ = inputSampleL; *outR++ = inputSampleR;
    }
}