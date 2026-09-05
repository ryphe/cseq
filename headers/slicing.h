#pragma once
#include "globals.h"

// --- Interactive Transient Slicing ------------------------------------------
// Pure-C99 transient detection that converts sample clips into zero-copy
// slices at detected transient boundaries. Used by the "Slice..." / "Batch
// Slice..." context-menu dialog: sensitivity is scrubbed live to preview
// slice boundaries, and only a commit (ENTER / [APPLY]) writes real clips.

#define MAX_SLICES_PER_CLIP 64

typedef struct {
    size_t frame_indices[MAX_SLICES_PER_CLIP];
    size_t count;
} TransientSliceMap;

// Snap a candidate frame index to the nearest rising zero crossing within a
// 256-frame window so slice boundaries land on silent points (no click).
static inline size_t snap_to_zero_crossing(const float *samples, size_t channels, size_t idx, size_t max_idx) {
    size_t limit = idx + 256;
    if (limit > max_idx) limit = max_idx;

    for (size_t i = idx; i < limit - 1; ++i) {
        float s0 = samples[i * channels];
        float s1 = samples[(i + 1) * channels];
        if ((s0 <= 0.0f && s1 > 0.0f) || (s0 >= 0.0f && s1 < 0.0f)) {
            return i;
        }
    }
    return idx;
}

// Detect transient slice boundaries in a mono-mixed PCM region. The slice
// map's frame indices are relative to the start of `samples` (i.e. the caller
// passes the clip's own frame range). Slice 0 always starts at frame 0.
static inline TransientSliceMap detect_clip_transients(const float *samples, size_t total_frames,
                                                       size_t channels, uint32_t sample_rate,
                                                       float sensitivity_0_to_1) {
    TransientSliceMap map = { 0 };
    if (!samples || total_frames == 0) return map;

    // 1. Peak Normalization Scan
    float max_amp = 0.0f;
    for (size_t i = 0; i < total_frames * channels; ++i) {
        float val = fabsf(samples[i]);
        if (val > max_amp) max_amp = val;
    }
    if (max_amp < 0.0001f) return map;

    // Map sensitivity slider (0.01 - 1.0) inversely to detection threshold:
    // Higher sensitivity -> lower threshold -> more slices detected.
    float min_thresh = max_amp * 0.05f;
    float max_thresh = max_amp * 0.60f;
    float threshold  = max_thresh - (sensitivity_0_to_1 * (max_thresh - min_thresh));

    float decay = expf(-1.0f / (0.015f * (float)sample_rate)); // ~15ms decay window
    size_t min_distance = (size_t)(0.040f * (float)sample_rate); // ~40ms min distance

    float prev_env = 0.0f;
    size_t last_slice = 0;
    map.frame_indices[map.count++] = 0; // Slice 0 start

    for (size_t i = 0; i < total_frames; ++i) {
        float mono = 0.0f;
        for (size_t c = 0; c < channels; ++c) mono += fabsf(samples[i * channels + c]);
        mono /= (float)channels;

        float diff = mono - prev_env;
        // One-pole low-pass envelope (not a leaky integrator): the leaky
        // form prev_env = mono + prev_env*decay accumulates to ~1/(1-decay)
        // for a sustained tone, so diff is always negative and nothing is
        // ever detected. The proper one-pole form keeps prev_env ≈ mono.
        prev_env = prev_env * decay + mono * (1.0f - decay);

        if (diff > threshold && (i - last_slice) > min_distance) {
            size_t clean_idx = snap_to_zero_crossing(samples, channels, i, total_frames);
            map.frame_indices[map.count++] = clean_idx;
            // Guard on the loop index, not the snapped position: the snap can
            // land at (or before) the detection point, which would make the
            // distance check pass again on the very next frame and re-fire.
            last_slice = i;

            if (map.count >= MAX_SLICES_PER_CLIP) break;
        }
    }

    return map;
}

// --- Preview state (UI thread only) -----------------------------------------
// While the Slice dialog is open, the sensitivity slider recomputes the slice
// maps here (non-destructively) and the timeline overlay draws the resulting
// boundaries as dashed lines. On cancel the state is cleared; on commit the
// maps drive the clip-split pipeline.
typedef struct {
    bool active;
    float sensitivity;               // 0.01 .. 1.0 (default 0.5)
    int  clipCount;
    int  clipIdx[MAX_CLIPS];
    TransientSliceMap maps[MAX_CLIPS];
} SlicePreviewState;

extern SlicePreviewState g_slicePreview;
