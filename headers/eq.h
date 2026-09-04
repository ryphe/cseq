#ifndef SMOOTH_EQ3_H
#define SMOOTH_EQ3_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double high_a0, high_a1, high_a2, high_b1, high_b2;
    double low_a0,  low_a1,  low_a2,  low_b1,  low_b2;
    double high_coef, low_coef;
    double treble_gain, mid_gain, bass_gain;

    double high_sL1, high_sL2, high_sR1, high_sR2;
    double low_sL1,  low_sL2,  low_sR1,  low_sR2;

    double high_fast_L_IIR, high_fast_R_IIR;
    double low_fast_L_IIR,  low_fast_R_IIR;

    uint32_t fpdL, fpdR;
    float param_high, param_mid, param_bass;
    double sample_rate;
    bool dirty_coeffs;
} SmoothEQ3;

void smooth_eq3_init(SmoothEQ3* eq, double sample_rate);
void smooth_eq3_reset(SmoothEQ3* eq);
void smooth_eq3_set_sample_rate(SmoothEQ3* eq, double sample_rate);
void smooth_eq3_set_params(SmoothEQ3* eq, float high, float mid, float bass);

void smooth_eq3_process_float(SmoothEQ3* eq, const float* inL, const float* inR, float* outL, float* outR, uint32_t sample_frames);
void smooth_eq3_process_double(SmoothEQ3* eq, const double* inL, const double* inR, double* outL, double* outR, uint32_t sample_frames);

#ifdef __cplusplus
}
#endif
#endif  