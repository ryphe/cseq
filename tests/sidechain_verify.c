// Sidechain routing verification harness for the external sidechain feature.
// Compiled separately (not part of the app build) to validate that a slot-0
// compressor ducks a quiet target against a loud external feed when
// scActive/scFeedL/scFeedR are set, and that it leaves the target untouched
// when the external feed is inactive (internal self-detection).
#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commdlg.h>
#define MAX_TRACKS 128

#include "../headers/fx.h"
#include "../headers/visualizer.h"
#include "../headers/actions.h"

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

#define SR 44100.0f

int main(void) {
    FxDescriptor d = { FX_TYPE_COMPRESSOR, "Compressor", 6, kFxCompressorParams,
                       fx_compressor_init, fx_compressor_free, fx_compressor_process };

    // Case 1: External sidechain ducks a quiet target.
    {
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx_instance_init(&fx, (int)SR);
        // Sidechain ducking defaults: -20 dB threshold, 6:1 ratio, 2 ms attack,
        // 80 ms release, 4 dB knee, 0 dB makeup.
        fx.params[0] = -20.0f; fx.params[1] = 6.0f;
        fx.params[2] = 2.0f;   fx.params[3] = 80.0f;
        fx.params[4] = 4.0f;   fx.params[5] = 0.0f;

        // Quiet target (-40 dB), loud external feed (0 dB).
        const float targetAmp = 0.01f;
        const float feedAmp = 1.0f;
        float phase = 0.0f;
        int n = (int)SR; // 1 s steady state
        float peakOut = 0.0f;
        fx.scActive = true;
        for (int i = 0; i < n; ++i) {
            phase += 100.0f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = targetAmp * sinf(6.2831853f * phase);
            float R = L;
            // Loud constant feed -> detector sees 0 dB -> heavy ducking.
            fx.scFeedL = feedAmp * sinf(6.2831853f * phase);
            fx.scFeedR = fx.scFeedL;
            fx.desc->process(&fx, &L, &R);
            // Measure steady state only, past the 2 ms attack transient.
            if (i >= n - n / 4) {
                float a = fabsf(L);
                if (a > peakOut) peakOut = a;
            }
        }
        // Expect roughly (1/6-1)*(20dB) ~ -16.7 dB GR => output ~ target -16dB.
        float outDb = 20.0f * log10f(peakOut / targetAmp);
        CHECK(outDb < -8.0f && outDb > -22.0f,
              "Sidechain: loud feed ducks quiet target (measured GR window)");
        fx_instance_free(&fx);
    }

    // Case 2: Same target, no external feed -> internal self-detection on the
    // quiet signal leaves it essentially untouched (well below threshold).
    {
        FxInstance fx; fx.desc = &d;
        for (int p = 0; p < FX_MAX_PARAMS; ++p) fx.params[p] = 0.0f;
        fx_instance_init(&fx, (int)SR);
        fx.params[0] = -20.0f; fx.params[1] = 6.0f;
        fx.params[2] = 2.0f;   fx.params[3] = 80.0f;
        fx.params[4] = 4.0f;   fx.params[5] = 0.0f;

        const float targetAmp = 0.01f; // -40 dB
        float phase = 0.0f;
        int n = (int)SR;
        float peakOut = 0.0f;
        fx.scActive = false; // internal detection
        for (int i = 0; i < n; ++i) {
            phase += 100.0f / SR;
            if (phase >= 1.0f) phase -= 1.0f;
            float L = targetAmp * sinf(6.2831853f * phase);
            float R = L;
            fx.desc->process(&fx, &L, &R);
            float a = fabsf(L);
            if (a > peakOut) peakOut = a;
        }
        float outDb = 20.0f * log10f(peakOut / targetAmp);
        CHECK(outDb > -1.0f, "No feed: quiet target not ducked (internal detection)");
        fx_instance_free(&fx);
    }

    if (g_failures == 0) {
        printf("ALL SIDECHAIN CHECKS PASSED\n");
        return 0;
    }
    printf("SIDECHAIN FAILURES: %d\n", g_failures);
    return 1;
}
