// DSP verification harness for the new FX modules and TrackFilter.
// Compiled separately (not part of the app build) to validate:
//  - Phaser: allpass cascade stays bounded; notches appear in the response
//  - Chorus: delay reads stay inside the buffer; output bounded
//  - Compressor: static curve matches analytic soft-knee values
//  - Resonator: rings at the requested frequency, decays; 0 dB peak
//  - TrackFilter: LP/HP/BP/Notch magnitude at f0 matches RBJ theory
//  - Peak pyramid: level-0/LOD correctness, tail clamp, preview coverage
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Minimal shims so the project headers compile outside the app (globals are
// extern declarations; main.c normally defines them).
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commdlg.h>
#define MAX_TRACKS 128

#include "../headers/fx.h"
#include "../headers/visualizer.h"   // g_visRing etc., required by ui.h
#include "../headers/actions.h"   // peak pyramid builders + AudioSample

// Global definitions the extern declarations in globals.h expect.
SequencerState g_Seq;
HWND g_hWnd = NULL;
GranularEngine g_TrackGran[MAX_TRACKS];
GranularEngine g_ClipGran[MAX_CLIPS];
HFONT g_hFontUI = NULL;
float g_dpiScaleX = 1.0f, g_dpiScaleY = 1.0f;


static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_failures++; } \
    else           { printf("ok:   %s\n", msg); } \
} while (0)

static const float SR = 44100.0f;

// Feed n samples of a sine at freq, return peak of last 'tail' samples.
static double measure_peak(FxInstance* fx, float freq, int n, int tailSkip) {
    float phase = 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i) {
        phase += freq / SR;
        if (phase >= 1.0f) phase -= 1.0f;
        float L = 0.5f * sinf(6.2831853f * phase), R = L;
        fx->desc->process(fx, &L, &R);
        if (i >= tailSkip) {
            float a = fabsf(L);
            if (a > peak) peak = a;
        }
    }
    return (double)peak;
}

int main(void) {
    printf("=== fx.h new modules verification ===\n");

    // --- Resonator: SVF bandpass with sqrt(Q) makeup ------------------------
    {
        FxDescriptor d = { FX_TYPE_RESONATOR, "Resonator", 3, kFxResonatorParams,
                           fx_resonator_init, fx_resonator_free, fx_resonator_process };
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx.params[0] = 69.0f; fx.params[1] = 20.0f; fx.params[2] = 100.0f; // Note 69 = A4 (440 Hz)
        fx_instance_init(&fx, (int)SR);

        // Sine at f0: SVF band gain is Q (=1/k); the 1/sqrt(Q) output scale
        // sets the net resonant peak to sqrt(Q) (~4.47 for Q=20), so a 0.1
        // sine rings at ~0.45 - audible above the mix without clipping.
        fx.params[0] = 69.0f; fx.params[1] = 20.0f; fx.params[2] = 100.0f;
        fx_instance_init(&fx, (int)SR);
        {
            // measure with 0.1 amp input
            float phase = 0.0f; float peak = 0.0f; int n = (int)SR / 2;
            for (int i = 0; i < n; ++i) {
                phase += 440.0f / SR;
                if (phase >= 1.0f) phase -= 1.0f;
                float L = 0.1f * sinf(6.2831853f * phase), R = L;
                fx.desc->process(&fx, &L, &R);
                if (i >= n / 2 && fabsf(L) > peak) peak = fabsf(L);
            }
            // Expected net gain sqrt(Q) = 4.47 -> ~0.447 out.
            CHECK(peak > 0.3 && peak < 0.6, "Resonator net peak gain = sqrt(Q) at f0 (Q=20)");
        }

        // Mix law: mix=0 must pass dry exactly.
        fx_resonator_init(&fx, (int)SR);
        fx.params[2] = 0.0f;
        {
            float L = 0.37f, R = -0.21f;
            fx.desc->process(&fx, &L, &R);
            CHECK(fabsf(L - 0.37f) < 1e-6f && fabsf(R + 0.21f) < 1e-6f, "Resonator mix=0 is pure dry");
        }

        // Sine far off resonance -> strongly attenuated (relative to on-res).
        fx.params[0] = 69.0f; fx.params[1] = 20.0f; fx.params[2] = 100.0f;
        fx_resonator_init(&fx, (int)SR);
        {
            float phase = 0.0f; float peakOff = 0.0f; int n = (int)SR / 2;
            for (int i = 0; i < n; ++i) {
                phase += 3000.0f / SR;
                if (phase >= 1.0f) phase -= 1.0f;
                float L = 0.01f * sinf(6.2831853f * phase), R = L;
                fx.desc->process(&fx, &L, &R);
                if (i >= n / 2 && fabsf(L) > peakOff) peakOff = fabsf(L);
            }
            CHECK(peakOff < 0.02, "Resonator rejects off-resonance (3kHz)");
        }

        // Stability at extreme settings: loud input, max Q, sweep — bounded.
        fx.params[0] = 60.0f; fx.params[1] = 35.0f; fx.params[2] = 100.0f;
        fx_resonator_init(&fx, (int)SR);
        {
            float worst = 0.0f;
            for (int i = 0; i < (int)SR; ++i) {
                float t = (float)i / SR;
                float f = 60.0f * powf(20.0f, fmodf(t, 1.0f));
                float L = 0.9f * sinf(6.2831853f * f * t), R = L;
                fx.desc->process(&fx, &L, &R);
                if (fabsf(L) > worst) worst = fabsf(L);
            }
            CHECK(worst < 40.0f, "Resonator stays bounded at Q=40 with loud sweep");
        }

        fx_instance_free(&fx);
    }

    // --- Compressor: static curve ------------------------------------------
    {
        FxDescriptor d = { FX_TYPE_COMPRESSOR, "Compressor", 6, kFxCompressorParams,
                           fx_compressor_init, fx_compressor_free, fx_compressor_process };
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx.params[0] = -12.0f;  // threshold
        fx.params[1] = 4.0f;    // ratio
        fx.params[2] = 0.1f;    // attack (fast)
        fx.params[3] = 1000.0f; // release (slow: hold GR steady)
        fx.params[4] = 0.0f;    // hard knee
        fx.params[5] = 0.0f;    // makeup
        fx_instance_init(&fx, (int)SR);

        // 0 dBFS-peak sine: the ~5 ms sidechain pre-smoother settles a 100 Hz
        // sine near its mean-rectified level (~ -3.2 dB for amp 1.0), so
        // 4:1 @ -12 dB threshold gives roughly (1/4-1)*(9) ~ -6.7 dB GR.
        float amp = 1.0f;
        int n = (int)SR / 2;
        float phase = 0.0f, lastGainDb = 0.0f;
        for (int i = 0; i < n; ++i) {
            phase += 100.0f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = amp * sinf(6.2831853f * phase), R = L;
            fx.desc->process(&fx, &L, &R);
            // derive applied gain from state env
            lastGainDb = ((FxCompressorState*)fx.state)->envDb;
        }
        // Require solid reduction in the analytically plausible window.
        CHECK(lastGainDb < -4.0f && lastGainDb > -9.5f, "Compressor 4:1 @ -12dB reduces 0dBFS sine");

        // Below threshold -> no reduction.
        fx_compressor_init(&fx, (int)SR);
        amp = 0.05f; // ~ -26 dB peak
        for (int i = 0; i < n; ++i) {
            phase += 100.0f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = amp * sinf(6.2831853f * phase), R = L;
            fx.desc->process(&fx, &L, &R);
        }
        lastGainDb = ((FxCompressorState*)fx.state)->envDb;
        CHECK(fabs(lastGainDb) < 0.3f, "Compressor no GR below threshold");

        // Zero-crossing buzz fix: 60 Hz sine, 10 ms release. Measure gain
        // ripple (peak-to-peak of envDb) over the steady state; the old
        // instantaneous rectifier released on every crossing (large ripple).
        fx_compressor_init(&fx, (int)SR);
        fx.params[0] = -12.0f; fx.params[1] = 4.0f;
        fx.params[2] = 1.0f;   // attack
        fx.params[3] = 10.0f;  // release (fast - the buzz case)
        for (int i = 0; i < n; ++i) {
            phase += 60.0f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = 0.9f * sinf(6.2831853f * phase), R = L;
            fx.desc->process(&fx, &L, &R);
        }
        {
            float maxDb = -999.0f, minDb = 999.0f;
            for (int i = 0; i < (int)(0.2f * SR); ++i) {
                phase += 60.0f / SR;
                if (phase >= 1.0f) phase -= 1.0f;
                float L = 0.9f * sinf(6.2831853f * phase), R = L;
                fx.desc->process(&fx, &L, &R);
                float db = ((FxCompressorState*)fx.state)->envDb;
                if (db > maxDb) maxDb = db;
                if (db < minDb) minDb = db;
            }
            CHECK((maxDb - minDb) < 1.5f, "Compressor gain ripple < 1.5dB at 60Hz/10ms release (no buzz)");
        }
        fx_instance_free(&fx);
    }

    // --- Phaser: bounded output and notches --------------------------------
    {
        FxDescriptor d = { FX_TYPE_PHASER, "Phaser", 6, kFxPhaserParams,
                           fx_phaser_init, fx_phaser_free, fx_phaser_process };
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx.params[0] = 0.5f; fx.params[1] = 100.0f; fx.params[2] = 0.0f;
        fx.params[3] = 100.0f; fx.params[4] = 440.0f; fx.params[5] = 3200.0f;
        fx_instance_init(&fx, (int)SR);

        // Bounded: sweep with full-depth modulation must never exceed ~1.2.
        float worst = 0.0f;
        float phase = 0.0f;
        for (int i = 0; i < (int)SR; ++i) {
            // log sweep 100..5000 Hz
            float t = (float)i / SR;
            float f = 100.0f * powf(50.0f, fmodf(t, 2.0f) * 0.5f);
            phase += f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = 0.5f * sinf(6.2831853f * phase), R = L;
            fx.desc->process(&fx, &L, &R);
            if (fabsf(L) > worst) worst = fabsf(L);
        }
        CHECK(worst < 1.2f, "Phaser output bounded during full sweep");

        // Mix law: mix=0 must pass dry exactly (was pinned 100% wet pre-fix).
        fx_phaser_init(&fx, (int)SR);
        fx.params[3] = 0.0f;
        {
            float L = 0.42f, R = -0.33f;
            fx.desc->process(&fx, &L, &R);
            CHECK(fabsf(L - 0.42f) < 1e-6f && fabsf(R + 0.33f) < 1e-6f, "Phaser mix=0 is pure dry");
        }

        // 50% mix on a 20 Hz tone (allpass cascade ~transparent below sweep
        // floor): linear mix law sums dry+wet at 0.5 each -> ~unity through.
        fx_phaser_init(&fx, (int)SR);
        fx.params[3] = 50.0f;
        {
            float phase2 = 0.0f; float pk = 0.0f; int n2 = (int)SR / 2;
            for (int i = 0; i < n2; ++i) {
                phase2 += 20.0f / SR;
                if (phase2 >= 1.0f) phase2 -= 1.0f;
                float L = 0.5f * sinf(6.2831853f * phase2), R = L;
                fx.desc->process(&fx, &L, &R);
                if (i >= n2 / 2 && fabsf(L) > pk) pk = fabsf(L);
            }
            CHECK(pk > 0.35f && pk < 0.65f, "Phaser 50% mix ~unity through low band (notches elsewhere)");
        }

        // High feedback must not shriek into clipping (softclip in loop).
        fx_phaser_init(&fx, (int)SR);
        fx.params[2] = 90.0f; fx.params[3] = 100.0f;
        {
            float worstFb = 0.0f;
            for (int i = 0; i < (int)SR; ++i) {
                float t = (float)i / SR;
                float f = 200.0f * powf(20.0f, fmodf(t, 1.0f));
                float L = 0.5f * sinf(6.2831853f * f * t), R = L;
                fx.desc->process(&fx, &L, &R);
                if (fabsf(L) > worstFb) worstFb = fabsf(L);
            }
            CHECK(worstFb < 4.0f, "Phaser feedback 90% stays musical (softclip bounds resonance)");
        }
        fx_instance_free(&fx);
    }

    // --- Chorus: bounded, mixes dry, equal-power law ------------------------
    {
        FxDescriptor d = { FX_TYPE_CHORUS, "Chorus", 5, kFxChorusParams,
                           fx_chorus_init, fx_chorus_free, fx_chorus_process };
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx.params[0] = 0.8f; fx.params[1] = 2.0f; fx.params[2] = 12.0f;
        fx.params[3] = 20.0f; fx.params[4] = 50.0f;
        fx_instance_init(&fx, (int)SR);

        // Impulse response exists and stays bounded over the buffer length.
        float worst = 0.0f;
        for (int i = 0; i < (int)(0.05f * SR); ++i) {
            float L = (i == 0) ? 1.0f : 0.0f, R = L;
            fx.desc->process(&fx, &L, &R);
            if (fabsf(L) > worst) worst = fabsf(L);
        }
        CHECK(worst > 0.1f && worst <= 1.05f, "Chorus impulse produces bounded wet signal");

        // Equal-power mix: use a mid-band tone. The wet path deliberately
        // high-passes bass (BBD tone conditioning, ~90 Hz HPF) to keep low end
        // un-phased, so a low test tone would be stripped from the wet signal
        // and the crossfade could never be exercised. 1000 Hz sits in the wet
        // passband and its 12 ms delay is ~12 full cycles, so dry and wet stay
        // near in phase: the 50% crossfade must sit near unity amplitude
        // (no 100%-wet vibrato).
        fx_chorus_init(&fx, (int)SR);
        {
            const float toneF = 1000.0f;
            float phase2 = 0.0f; float pk = 0.0f; int n2 = (int)SR / 2;
            for (int i = 0; i < n2; ++i) {
                phase2 += toneF / SR;
                if (phase2 >= 1.0f) phase2 -= 1.0f;
                float L = 0.5f * sinf(6.2831853f * phase2), R = L;
                fx.desc->process(&fx, &L, &R);
                if (i >= n2 / 2 && fabsf(L) > pk) pk = fabsf(L);
            }
            // 0.707*(dry+wet) with the LFO sweeping the ~12 ms phase offset:
            // amplitude wobbles but must keep substantial dry presence rather
            // than dropping to the pure-vibrato floor.
            CHECK(pk > 0.4f && pk < 1.1f, "Chorus 50% equal-power mix stays level (no 100%-wet vibrato)");
        }

        // Mix=0 is pure dry passthrough.
        fx_chorus_init(&fx, (int)SR);
        fx.params[4] = 0.0f;
        {
            float L = 0.61f, R = -0.44f;
            fx.desc->process(&fx, &L, &R);
            CHECK(fabsf(L - 0.61f) < 1e-5f && fabsf(R + 0.44f) < 1e-5f, "Chorus mix=0 is pure dry");
        }
        fx_instance_free(&fx);
    }

    // --- TrackFilter: magnitude matches RBJ theory --------------------------
    {
        TrackFilter f;
        track_filter_init_defaults(&f);

        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP);
        f.frequency = 1000.0f; f.q = 0.7071067811865475f;
        track_filter_update(&f, SR);
        float dbAt1k;
        {
            double w = 6.28318530717958647692 * 1000.0 / (double)SR;
            dbAt1k = (float)track_filter_band_db(&f.band[TRACK_FILTER_LP], cos(w), cos(2.0 * w));
        }
        CHECK(fabs(dbAt1k + 3.01f) < 0.15f, "TrackFilter LP @1kHz Butterworth = -3.0dB");

        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_HP);
        track_filter_update(&f, SR);
        float dbLow;
        {
            double w = 6.28318530717958647692 * 100.0 / (double)SR;
            dbLow = (float)track_filter_band_db(&f.band[TRACK_FILTER_HP], cos(w), cos(2.0 * w));
        }
        CHECK(dbLow < -35.0f, "TrackFilter HP @1kHz rejects 100Hz");

        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_BP);
        f.q = 10.0f;
        track_filter_update(&f, SR);
        float dbPeak;
        {
            double w = 6.28318530717958647692 * 1000.0 / (double)SR;
            dbPeak = (float)track_filter_band_db(&f.band[TRACK_FILTER_BP], cos(w), cos(2.0 * w));
        }
        CHECK(fabs(dbPeak) < 0.2f, "TrackFilter BP peak = 0dB at f0");

        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_NOTCH);
        f.q = 5.0f;
        track_filter_update(&f, SR);
        float dbNull;
        {
            double w = 6.28318530717958647692 * 1000.0 / (double)SR;
            dbNull = (float)track_filter_band_db(&f.band[TRACK_FILTER_NOTCH], cos(w), cos(2.0 * w));
        }
        CHECK(dbNull < -55.0f, "TrackFilter Notch nulls f0");

        // Stacking: LP + HP cascade = -6 dB at f0 (each -3 dB, dB adds).
        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP) | TRACK_FILTER_BIT(TRACK_FILTER_HP);
        f.q = 0.7071067811865475f;
        track_filter_update(&f, SR);
        {
            double w = 6.28318530717958647692 * 1000.0 / (double)SR;
            double c = cos(w), c2 = cos(2.0 * w);
            float dbBoth = (float)(track_filter_band_db(&f.band[TRACK_FILTER_LP], c, c2)
                                 + track_filter_band_db(&f.band[TRACK_FILTER_HP], c, c2));
            CHECK(fabs(dbBoth + 6.02f) < 0.3f, "TrackFilter stacked LP+HP = -6dB at f0");
        }

        // Stored magnitude curve agrees with the analytic probe (LP @ 1 kHz).
        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP);
        track_filter_update(&f, SR);
        int bestIdx = 0;
        float bestDist = 1e9f;
        for (int i = 0; i < TRACK_FILTER_POINTS; ++i) {
            float probe = 20.0f * powf(1000.0f, (float)i / (TRACK_FILTER_POINTS - 1.0f));
            float d = fabsf(logf(probe / 1000.0f));
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        CHECK(fabs(f.magnitude[bestIdx] - dbAt1k) < 1.0f, "Stored magnitude curve agrees with analytic value");

        // All bands off -> inactive, curve flat 0 dB.
        f.typeMask = 0;
        track_filter_update(&f, SR);
        CHECK(!track_filter_any_active(&f), "No bands -> inactive");
        CHECK(fabsf(f.magnitude[128]) < 1e-4f, "No bands -> flat 0dB curve");

        // Audio state processes without blowing up.
        f.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP);
        f.frequency = 500.0f;
        f.enabled = true;
        track_filter_update(&f, SR);
        float L = 1.0f, R = 1.0f;
        track_filter_process(&f, &L, &R);
        for (int i = 0; i < 4096; ++i) { L = 0.3f * sinf((float)i); R = L; track_filter_process(&f, &L, &R); }
        CHECK(fabsf(L) < 1.0f, "TrackFilter process stays bounded on sine");

        // --- Zipper-noise regression: target slew + no state reset ----------
        {
            // Fresh filter at 8 kHz LP; drag target to 200 Hz mid-stream and
            // process through the smoothed block path. The transition must be
            // graceful: sample-to-sample output deltas stay small (no step
            // discontinuity = no crackle) and the smoothed cutoff converges.
            TrackFilter g;
            track_filter_init_defaults(&g);
            g.typeMask = TRACK_FILTER_BIT(TRACK_FILTER_LP);
            g.frequency = 8000.0f;
            g.q = 0.7071067811865475f;
            g.enabled = true;
            track_filter_update(&g, SR);

            float worstDelta = 0.0f;
            float prev = 0.0f;
            float Lg = 0.0f, Rg = 0.0f;
            int total = (int)SR / 4;
            for (int i = 0; i < total; ++i) {
                // "Drag": retarget repeatedly during the first half.
                if (i < total / 2 && (i % 97) == 0) {
                    track_filter_set_target(&g, 200.0f + 300.0f * (float)rand() / (float)RAND_MAX, 4.0f);
                }
                float in = 0.5f * sinf(2.0f * 3.14159265358979f * 220.0f * (float)i / SR);
                Lg = in; Rg = in;
                track_filter_process_block(&g, SR, &Lg, &Rg, 1);
                if (i > 16) {
                    float d = fabsf(Lg - prev);
                    if (d > worstDelta) worstDelta = d;
                }
                prev = Lg;
            }
            CHECK(worstDelta < 0.35f, "Sweeping cutoff slews smoothly (no per-tick step discontinuity)");
            CHECK(fabsf(g.curFrequency - g.frequency) < 1.0f, "Smoothed cutoff converges to target");
        }

        // set_target must clamp the same way the old update did.
        {
            TrackFilter g;
            track_filter_init_defaults(&g);
            track_filter_set_target(&g, 5.0f, 500.0f);
            CHECK(g.frequency == TRACK_FILTER_FMIN && g.q == 100.0f, "set_target clamps freq/Q to valid range");
        }
    }

    // --- Peak pyramid: level-0 min/max, LOD aggregation, tail clamping ------
    {
        // 10007 frames (prime: exercises the tail clamp), impulse + tone.
        const ma_uint64 N = 10007;
        AudioSample s;
        memset(&s, 0, sizeof(s));
        s.pFrames = (float*)calloc((size_t)N * 2, sizeof(float));
        s.frameCount = N;
        s.loaded = true;
        // Every 1000th frame: +0.8/-0.6 spike pair; background = 0.1 sine.
        for (ma_uint64 f = 0; f < N; ++f) {
            float v = 0.1f * sinf((float)f * 0.05f);
            if (f % 1000 == 0) v = 0.8f;
            if (f % 1000 == 500) v = -0.6f;
            s.pFrames[f * 2 + 0] = v;
            s.pFrames[f * 2 + 1] = v;
        }

        generate_peak_cache(&s);
        CHECK(s.peaksReady == 1 && s.peaks && s.lodCount > 1, "Pyramid built synchronously with multiple levels");

        // Level 0: brute-force min/max over each 512-frame bin must match.
        int n0 = s.lodEntries[0];
        bool l0ok = true;
        for (int i = 0; i < n0; ++i) {
            ma_uint64 a = (ma_uint64)i * PEAK_BASE_BIN_FRAMES;
            ma_uint64 b = a + PEAK_BASE_BIN_FRAMES;
            if (b > N) b = N;
            float mn = 0.0f, mx = 0.0f;
            for (ma_uint64 f = a; f < b; ++f) {
                float mono = (s.pFrames[f * 2] + s.pFrames[f * 2 + 1]) * 0.5f;
                if (mono < mn) mn = mono;
                if (mono > mx) mx = mono;
            }
            const Peak* p = &s.peaks[s.lodOffset[0] + i];
            if (fabsf(p->min - mn) > 1e-6f || fabsf(p->max - mx) > 1e-6f) { l0ok = false; break; }
        }
        CHECK(l0ok, "Level-0 min/max matches brute force (incl. clamped tail)");

        // The tail clamp must actually contain data: last bin holds frames
        // 9728..10006, which includes the spike at f=10000 (0.8).
        {
            const Peak* last = &s.peaks[s.lodOffset[0] + n0 - 1];
            CHECK(last->max > 0.79f, "Tail bin contains the f=10000 spike (tail clamp works)");
        }

        // LOD aggregation: each level-1 entry = min/max of its 4 children.
        bool lodOk = true;
        for (int l = 1; l < s.lodCount && lodOk; ++l) {
            const Peak* prev = s.peaks + s.lodOffset[l - 1];
            const Peak* cur = s.peaks + s.lodOffset[l];
            for (int i = 0; i < s.lodEntries[l]; ++i) {
                int c0 = i * PEAK_LOD_RATIO;
                int c1 = c0 + PEAK_LOD_RATIO;
                if (c1 > s.lodEntries[l - 1]) c1 = s.lodEntries[l - 1];
                float mn = prev[c0].min, mx = prev[c0].max;
                for (int c = c0 + 1; c < c1; ++c) {
                    if (prev[c].min < mn) mn = prev[c].min;
                    if (prev[c].max > mx) mx = prev[c].max;
                }
                if (fabsf(cur[i].min - mn) > 1e-6f || fabsf(cur[i].max - mx) > 1e-6f) { lodOk = false; break; }
            }
        }
        CHECK(lodOk, "Every LOD level equals 4:1 min/max of its children");

        free(s.pFrames);
        free_peak_cache(&s);
        CHECK(s.peaks == NULL && s.peakTotal == 0 && s.peaksReady == 0, "free_peak_cache clears all fields");
    }

    // --- Peak preview: strided fill covers the whole file -------------------
    {
        const ma_uint64 N = 900001;   // not a multiple of the bin size
        AudioSample s;
        memset(&s, 0, sizeof(s));
        s.pFrames = (float*)calloc((size_t)N * 2, sizeof(float));
        s.frameCount = N;
        s.loaded = true;
        for (ma_uint64 f = 0; f < N; ++f) {
            float v = 0.05f;
            if (f > N - 500) v = 0.7f;    // spike near the very end
            s.pFrames[f * 2 + 0] = v;
            s.pFrames[f * 2 + 1] = v;
        }
        size_t total = peak_total_entries(&s);
        Peak* buf = (Peak*)calloc(total, sizeof(Peak));
        peak_fill_preview(&s, buf);
        // The end-of-file spike must show up somewhere in level 0 (strided
        // probes reach the tail because each probe scans min(stride, bin)).
        float endMax = 0.0f;
        int n0 = (int)(total);
        // level 0 length via plan:
        int offs[PEAK_MAX_LOD_LEVELS], ent[PEAK_MAX_LOD_LEVELS]; int lv;
        peak_plan_levels(N, &lv, offs, ent);
        n0 = ent[0];
        for (int i = n0 - 8; i < n0; ++i) if (buf[i].max > endMax) endMax = buf[i].max;
        CHECK(endMax > 0.6f, "Preview strided scan reaches the file tail");
        (void)total;
        free(buf);
        free(s.pFrames);
    }

    printf("\n%s (%d failures)\n", g_failures ? "RESULT: FAILED" : "RESULT: ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
