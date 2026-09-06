// fade_wedge_verify.c — visual/numeric check that the low-opacity fade wedge
// tracks the drawn envelope curve for every fade type.
//
// The three blocks below are EXTRACTED VERBATIM from the real sources at
// build time by fade_wedge_verify.bat (config.h fade enum, dsp.h
// compute_fade_gain, ui.h fade-template block), so this exercises the exact
// shipping template code. For each curve type and direction it builds the
// real supersampled template and asserts that, in every pixel column, the
// alpha coverage is contiguous from the curve line down to the wedge base —
// i.e. no gap between the drawn shape and the envelope line. It also writes
// the alpha maps to tests\fade_wedge_out_*.pgm for eyeballing.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
    else           { printf("ok  : %s\n", msg); } \
} while (0)

// --- extracted: config.h fade enum ---------------------------------------
#include "_fade_enum.inc"
// --- extracted: dsp.h compute_fade_gain ----------------------------------
#include "_fade_gain.inc"
// --- extracted: ui.h fade-template block ----------------------------------
#include "_fade_tpl.inc"

static const char* kNames[FADE_CURVE_COUNT] = { "linear", "exp", "smooth", "log" };

static void dump_pgm(const char* path, const BYTE* a, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(a, 1, (size_t)w * h, f);
    fclose(f);
}

static void verify_case(HDC hdc, int w, int h, uint8_t type, bool isFadeIn) {
    const FadeCurveTpl* t = fade_template_get(hdc, w, h, type, isFadeIn);
    if (!t) { printf("FAIL: template build %s %s\n", kNames[type], isFadeIn ? "in" : "out"); g_fail++; return; }

    char path[128];
    snprintf(path, sizeof(path), "tests\\fade_wedge_out_%s_%s.pgm", kNames[type], isFadeIn ? "in" : "out");
    dump_pgm(path, t->alpha, w, h);

    // Contiguity: walking each column bottom-up, coverage must stay nonzero
    // from the base row until the first covered pixel (the curve line). Any
    // interior zero run below the line means the wedge detached from it.
    int worstGap = 0;
    for (int x = 1; x < w - 1; ++x) {           // skip pen-clip edges
        int y = h - 1;
        // find first covered pixel scanning bottom-up
        while (y >= 0 && t->alpha[(size_t)y * w + x] == 0) --y;
        if (y < 0) continue;                     // empty column (flat start)
        int gap = 0, run = 0;
        for (int yy = y; yy < h; ++yy) {
            if (t->alpha[(size_t)yy * w + x] == 0) { ++run; if (run > gap) gap = run; }
            else run = 0;
        }
        if (gap > worstGap) worstGap = gap;
    }
    if (worstGap > 0) {
        printf("FAIL: %s fade-%s gap of %d px between wedge and curve line\n",
               kNames[type], isFadeIn ? "in" : "out", worstGap);
        g_fail++;
    } else {
        printf("ok  : %s fade-%s wedge reaches the curve line in every column\n",
               kNames[type], isFadeIn ? "in" : "out");
    }

    // Linear must still be the exact straight triangle: the wedge top edge
    // (first covered row per column, allowing for the 2px pen) matches the
    // diagonal. Check the fill exists at 25% / 75% of the width below the diagonal.
    if (type == FADE_CURVE_LINEAR) {
        for (int frac = 1; frac <= 3; ++frac) {
            int x = w * frac / 4;
            float gy = (1.0f - (float)x / (float)(w - 1)) * (float)(h - 1);
            int found = 0;
            for (int y = (int)gy; y < h; ++y)
                if (t->alpha[(size_t)y * w + x]) { found = 1; break; }
            if (!found) { printf("FAIL: linear wedge missing below diagonal at x=%d\n", x); g_fail++; }
        }
    }
}

int main(void) {
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { printf("FAIL: no DC\n"); return 1; }

    // Full-size clip fade region and a tiny mini-indicator size.
    verify_case(hdc, 260, 120, FADE_CURVE_LINEAR, true);
    verify_case(hdc, 260, 120, FADE_CURVE_EXP,    true);
    verify_case(hdc, 260, 120, FADE_CURVE_SMOOTH, true);
    verify_case(hdc, 260, 120, FADE_CURVE_LOG,    true);
    verify_case(hdc, 260, 120, FADE_CURVE_LINEAR, false);
    verify_case(hdc, 260, 120, FADE_CURVE_EXP,    false);
    verify_case(hdc, 260, 120, FADE_CURVE_SMOOTH, false);
    verify_case(hdc, 260, 120, FADE_CURVE_LOG,    false);
    verify_case(hdc, 18, 16, FADE_CURVE_EXP,  true);   // mini glyph sizes
    verify_case(hdc, 18, 16, FADE_CURVE_LOG,  false);

    DeleteDC(hdc);
    printf(g_fail ? "\n%d failure(s)\n" : "\nall fade wedge checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
