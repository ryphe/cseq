#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <commdlg.h>

#define MAX_TRACKS 128

#include "config.h"
#include "types.h"
#include "globals.h"
#include "dsp.h"
#include "fx.h"
#include "visualizer.h"
#include "state.h"
#include "samplecache.h"
#include "ui.h"
#include "actions.h"

// Shims for globals defined in main.c
SequencerState g_Seq;
HWND g_hWnd = NULL;
HWND g_fxRackHwnd = NULL;
GranularEngine g_TrackGran[MAX_TRACKS];
GranularEngine g_ClipGran[MAX_CLIPS];
SynthHaloState g_ClipHalo[MAX_CLIPS];
SynthQuadrumState g_ClipQuadrum[MAX_CLIPS];
FxChain         g_TrackFx[MAX_TRACKS];
HFONT g_hFontUI = NULL;
float g_dpiScaleX = 1.0f, g_dpiScaleY = 1.0f;
SlicePreviewState g_slicePreview = { 0 };

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_failures++; } \
    else           { printf("ok:   %s\n", msg); } \
} while (0)

int main(void) {
    printf("=== Sample Clip Reversing Verification ===\n");

    InitializeCriticalSection(&g_Seq.lock);
    g_Seq.bpm = 120.0f;
    g_Seq.sampleCount = 0;
    g_Seq.clipCount = 0;
    float fpb = frames_per_beat(g_Seq.bpm); // 44100 * 60 / 120 = 22050.0

    // --- 1. Offset Remapping Math & Invertibility ---
    {
        AudioSample s;
        memset(&s, 0, sizeof(s));
        s.frameCount = 44100; // 2 seconds

        // Case A: Clip covers whole sample (offset 0, 2 beats at 120 bpm = 44100 frames)
        Clip cA;
        memset(&cA, 0, sizeof(cA));
        cA.startBeat = 0.0f;
        cA.lengthBeats = 2.0f;
        cA.sampleOffsetFrames = 0;
        cA.playbackRate = 1.0f;

        ma_uint64 revOffA = reverse_remap_clip_offset(&cA, &s, fpb);
        CHECK(revOffA == 0, "Full sample clip mirrors to offset 0");

        cA.sampleOffsetFrames = revOffA;
        ma_uint64 origOffA = reverse_remap_clip_offset(&cA, &s, fpb);
        CHECK(origOffA == 0, "Re-reversing full sample returns to offset 0");

        // Case B: Sliced region (e.g. frames 5000..15000, span 10000)
        Clip cB;
        memset(&cB, 0, sizeof(cB));
        cB.startBeat = 0.0f;
        cB.lengthBeats = 10000.0f / fpb; // exact length for 10000 frames
        cB.sampleOffsetFrames = 5000;
        cB.playbackRate = 1.0f;

        ma_uint64 revOffB = reverse_remap_clip_offset(&cB, &s, fpb);
        // Expected: 44100 - 5000 - 10000 = 29100
        CHECK(revOffB == 29100, "Sliced clip mirrors offset: 44100 - 5000 - 10000 = 29100");

        // Re-reverse sliced clip
        cB.sampleOffsetFrames = revOffB;
        ma_uint64 origOffB = reverse_remap_clip_offset(&cB, &s, fpb);
        CHECK(origOffB == 5000, "Re-reversing sliced clip returns exactly to original offset 5000");

        // Case C: PlaybackRate scaling (2.0x rate = twice as many frames per beat)
        Clip cC;
        memset(&cC, 0, sizeof(cC));
        cC.startBeat = 0.0f;
        cC.lengthBeats = 1.0f; // 1 beat
        cC.sampleOffsetFrames = 4000;
        cC.playbackRate = 2.0f;
        // 1 beat at 2.0x = 22050 * 2.0 = 44100 frames
        // But avail = 44100 - 4000 = 40100 frames, so clamped to 40100
        // newOffset = 44100 - 4000 - 40100 = 0
        ma_uint64 revOffC = reverse_remap_clip_offset(&cC, &s, fpb);
        CHECK(revOffC == 0, "Rate 2.0x clamping at sample end mirrors cleanly to 0");

        // Case D: Half-speed (0.5x rate = 11025 frames for 1 beat)
        Clip cD;
        memset(&cD, 0, sizeof(cD));
        cD.startBeat = 0.0f;
        cD.lengthBeats = 1.0f;
        cD.sampleOffsetFrames = 10000;
        cD.playbackRate = 0.5f; // span = 22050 * 0.5 = 11025 frames
        // newOffset = 44100 - 10000 - 11025 = 23075
        ma_uint64 revOffD = reverse_remap_clip_offset(&cD, &s, fpb);
        CHECK(revOffD == 23075, "Rate 0.5x mirrors correctly: 44100 - 10000 - 11025 = 23075");

        cD.sampleOffsetFrames = revOffD;
        ma_uint64 origOffD = reverse_remap_clip_offset(&cD, &s, fpb);
        CHECK(origOffD == 10000, "Rate 0.5x re-reverses exactly back to 10000");

        // Case E: Out-of-bounds offset >= frameCount
        Clip cE;
        memset(&cE, 0, sizeof(cE));
        cE.lengthBeats = 1.0f;
        cE.sampleOffsetFrames = 50000; // past 44100
        cE.playbackRate = 1.0f;
        ma_uint64 revOffE = reverse_remap_clip_offset(&cE, &s, fpb);
        CHECK(revOffE == 0, "Out of bounds offset safely returns 0 without underflow");
    }

    // --- 2. Buffer Reversal, Caching & Content Dedup ---
    {
        ma_uint64 frameCount = 1000;
        size_t totalFloats = (size_t)frameCount * 2u;
        float* pcm = (float*)malloc(totalFloats * sizeof(float));
        for (ma_uint64 i = 0; i < frameCount; ++i) {
            pcm[i * 2 + 0] = (float)i;                  // Left ramp up: 0..999
            pcm[i * 2 + 1] = (float)(frameCount - 1 - i); // Right ramp down: 999..0
        }

        uint64_t origHash = sample_hash_pcm(pcm, totalFloats * sizeof(float));
        int s0 = g_Seq.sampleCount;
        AudioSample* as0 = &g_Seq.samples[s0];
        memset(as0, 0, sizeof(AudioSample));
        strcpy(as0->name, "kick.wav");
        strcpy(as0->filename, "C:\\samples\\kick.wav");
        as0->frameCount = frameCount;
        sample_install_cached(as0, pcm, frameCount, origHash);
        as0->loaded = true;
        generate_peak_cache_auto(as0);
        g_Seq.sampleCount++;

        // First reverse: creates new reversed sample
        int revIdx1 = get_or_create_reversed_sample(s0);
        CHECK(revIdx1 == s0 + 1, "First reverse creates sample at index s0 + 1");
        CHECK(g_Seq.sampleCount == 2, "g_Seq.sampleCount incremented to 2");

        AudioSample* asRev = &g_Seq.samples[revIdx1];
        CHECK(strcmp(asRev->name, "kick.wav (reversed)") == 0, "Name suffixed with ' (reversed)'");
        CHECK(asRev->frameCount == frameCount, "Frame count matches original");

        // Verify reversed PCM contents
        bool pcmReversedOk = true;
        for (ma_uint64 i = 0; i < frameCount; ++i) {
            float expectedL = (float)(frameCount - 1 - i);
            float expectedR = (float)i;
            if (fabsf(asRev->pFrames[i * 2 + 0] - expectedL) > 1e-5f ||
                fabsf(asRev->pFrames[i * 2 + 1] - expectedR) > 1e-5f) {
                pcmReversedOk = false;
                break;
            }
        }
        CHECK(pcmReversedOk, "Reversed PCM values match exact end-to-start flip for stereo");

        // Calling get_or_create_reversed_sample on s0 again: must dedup to revIdx1!
        int revIdx1Dup = get_or_create_reversed_sample(s0);
        CHECK(revIdx1Dup == revIdx1, "Re-requesting reversed sample on s0 dedups to existing revIdx1");
        CHECK(g_Seq.sampleCount == 2, "sampleCount unchanged on dedup");

        // Reversing the reversed sample (revIdx1): must dedup to the original sample (s0)!
        int revBack = get_or_create_reversed_sample(revIdx1);
        CHECK(revBack == s0, "Reversing reversed sample returns original sample index s0 (lossless dedup)");
        CHECK(g_Seq.sampleCount == 2, "sampleCount still 2 - zero redundant samples added");
    }

    // --- 3. Batch Action & Multi-Clip Reversing ---
    {
        // Add 3 clips:
        // Clip 0: uses sample 0 (offset 100, length 200 frames)
        // Clip 1: uses sample 0 (offset 400, length 300 frames) - shared sample slice!
        // Clip 2: uses sample 1 (which is already reversed) (offset 500, length 200 frames)
        g_Seq.clipCount = 3;

        Clip* c0 = &g_Seq.clips[0];
        memset(c0, 0, sizeof(Clip));
        c0->sampleIndex = 0;
        c0->sampleOffsetFrames = 100;
        c0->lengthBeats = 200.0f / fpb;
        c0->playbackRate = 1.0f;
        c0->isSelected = true;

        Clip* c1 = &g_Seq.clips[1];
        memset(c1, 0, sizeof(Clip));
        c1->sampleIndex = 0;
        c1->sampleOffsetFrames = 400;
        c1->lengthBeats = 300.0f / fpb;
        c1->playbackRate = 1.0f;
        c1->isSelected = true;

        Clip* c2 = &g_Seq.clips[2];
        memset(c2, 0, sizeof(Clip));
        c2->sampleIndex = 1; // sample 1 is the reversed version of sample 0
        c2->sampleOffsetFrames = 500;
        c2->lengthBeats = 200.0f / fpb;
        c2->playbackRate = 1.0f;
        c2->isSelected = true;

        int targets[3] = { 0, 1, 2 };
        reverse_clips_action(targets, 3);

        // Clip 0 should now point to sample 1, offset = 1000 - 100 - 200 = 700
        CHECK(c0->sampleIndex == 1, "Batch reverse: Clip 0 remapped to sample 1");
        CHECK(c0->sampleOffsetFrames == 700, "Batch reverse: Clip 0 offset remapped to 700");

        // Clip 1 should also point to sample 1, offset = 1000 - 400 - 300 = 300
        CHECK(c1->sampleIndex == 1, "Batch reverse: Clip 1 remapped to sample 1 (shared sample dedup)");
        CHECK(c1->sampleOffsetFrames == 300, "Batch reverse: Clip 1 offset remapped to 300");

        // Clip 2 was pointing to sample 1 (reversed), so it should now point back to sample 0!
        // offset = 1000 - 500 - 200 = 300
        CHECK(c2->sampleIndex == 0, "Batch reverse: Clip 2 returned to sample 0");
        CHECK(c2->sampleOffsetFrames == 300, "Batch reverse: Clip 2 offset remapped to 300");

        // --- 4. Undo and Redo ---
        undo_last_action();
        CHECK(g_Seq.clips[0].sampleIndex == 0 && g_Seq.clips[0].sampleOffsetFrames == 100,
              "Undo restores Clip 0 sampleIndex=0 and offset=100");
        CHECK(g_Seq.clips[1].sampleIndex == 0 && g_Seq.clips[1].sampleOffsetFrames == 400,
              "Undo restores Clip 1 sampleIndex=0 and offset=400");
        CHECK(g_Seq.clips[2].sampleIndex == 1 && g_Seq.clips[2].sampleOffsetFrames == 500,
              "Undo restores Clip 2 sampleIndex=1 and offset=500");

        redo_last_action();
        CHECK(g_Seq.clips[0].sampleIndex == 1 && g_Seq.clips[0].sampleOffsetFrames == 700,
              "Redo reapplies Clip 0 sampleIndex=1 and offset=700");
        CHECK(g_Seq.clips[1].sampleIndex == 1 && g_Seq.clips[1].sampleOffsetFrames == 300,
              "Redo reapplies Clip 1 sampleIndex=1 and offset=300");
        CHECK(g_Seq.clips[2].sampleIndex == 0 && g_Seq.clips[2].sampleOffsetFrames == 300,
              "Redo reapplies Clip 2 sampleIndex=0 and offset=300");
    }

    DeleteCriticalSection(&g_Seq.lock);

    printf("\n=== RESULT: %s (%d failures) ===\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures;
}
