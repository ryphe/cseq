#pragma once
#include "globals.h"
#include "granular.h"
#include "synth.h"
#include "fx.h"
#include <stdlib.h>
#include <string.h>

extern HWND g_fxRackHwnd;

// --- FX Rack Snapshot for Undo/Redo ---
typedef struct FxTrackSnapshot {
    int32_t count;
    int32_t types[FX_MAX_SLOTS];
    float   params[FX_MAX_SLOTS][FX_MAX_PARAMS];
} FxTrackSnapshot;

static inline void fx_snapshot_save(FxTrackSnapshot* dst) {
    if (!dst) return;
    for (int t = 0; t < MAX_TRACKS; ++t) {
        const FxChain* c = &g_TrackFx[t];
        dst[t].count = (c->count >= 0 && c->count <= FX_MAX_SLOTS) ? c->count : 0;
        for (int i = 0; i < FX_MAX_SLOTS; ++i) {
            if (i < dst[t].count && c->slots[i].desc) {
                dst[t].types[i] = c->slots[i].desc->type;
                memcpy(dst[t].params[i], c->slots[i].params, sizeof(float) * FX_MAX_PARAMS);
            } else {
                dst[t].types[i] = FX_TYPE_NONE;
                memset(dst[t].params[i], 0, sizeof(float) * FX_MAX_PARAMS);
            }
        }
    }
}

static inline void fx_snapshot_restore(const FxTrackSnapshot* src) {
    if (!src) return;
    for (int t = 0; t < MAX_TRACKS; ++t) {
        const FxTrackSnapshot* s = &src[t];
        int cnt = s->count;
        if (cnt < 0) cnt = 0;
        if (cnt > FX_MAX_SLOTS) cnt = FX_MAX_SLOTS;
        fx_chain_load(&g_TrackFx[t], cnt, s->types, &s->params[0][0], (int)SAMPLE_RATE);
    }
}

static inline void gran_engine_to_snapshot(GranClipSnapshot* dst, const GranularEngine* src) {
    dst->enabled = src->enabled;
    dst->grainSizeMs = src->grainSizeMs;
    dst->density = src->density;
    dst->position = src->position;
    dst->posJitter = src->posJitter;
    dst->pitch = src->pitch;
    dst->pitchJitter = src->pitchJitter;
    dst->panSpread = src->panSpread;
    dst->attack = src->attack;
    dst->release = src->release;
    dst->volume = src->volume;
    dst->freeze = src->freeze;
    dst->droneMode = src->droneMode;
    dst->sampleIndex = src->sampleIndex;
    dst->octaveShift = src->octaveShift;
    dst->noteCount = src->noteCount;
    if (src->noteCount > 0) {
        memcpy(dst->notes, src->notes, sizeof(GranNote) * src->noteCount);
    }
     
    dst->ownFrames = NULL;
    dst->ownFrameCount = 0;
    dst->ownLoaded = false;
    if (src->ownLoaded && src->ownFrames && src->ownFrameCount > 0) {
        size_t bytes = sizeof(float) * NUM_CHANNELS * (size_t)src->ownFrameCount;
        float* copy = (float*)malloc(bytes);
        if (copy) {
            memcpy(copy, src->ownFrames, bytes);
            dst->ownFrames = copy;
            dst->ownFrameCount = src->ownFrameCount;
            dst->ownLoaded = true;
        }
    }
}

static inline void gran_snapshot_to_engine(GranularEngine* dst, const GranClipSnapshot* src, int clipIdx, int trackIdx) {
    dst->enabled = src->enabled;
    dst->grainSizeMs = src->grainSizeMs;
    dst->density = src->density;
    dst->position = src->position;
    dst->posJitter = src->posJitter;
    dst->pitch = src->pitch;
    dst->pitchJitter = src->pitchJitter;
    dst->panSpread = src->panSpread;
    dst->attack = src->attack;
    dst->release = src->release;
    dst->volume = src->volume;
    dst->freeze = src->freeze;
    dst->droneMode = src->droneMode;
    dst->sampleIndex = src->sampleIndex;
    dst->octaveShift = src->octaveShift;
    dst->clipIdx = clipIdx;
    dst->trackIdx = trackIdx;
    dst->noteCount = src->noteCount;
    if (src->noteCount > 0) {
        memcpy(dst->notes, src->notes, sizeof(GranNote) * src->noteCount);
    }
     
    if (dst->ownFrames && dst->ownFrames != src->ownFrames) {
        free(dst->ownFrames);
    }
    dst->ownFrames = NULL;
    dst->ownFrameCount = 0;
    dst->ownLoaded = false;
    if (src->ownLoaded && src->ownFrames && src->ownFrameCount > 0) {
        size_t bytes = sizeof(float) * NUM_CHANNELS * (size_t)src->ownFrameCount;
        float* copy = (float*)malloc(bytes);
        if (copy) {
            memcpy(copy, src->ownFrames, bytes);
            dst->ownFrames = copy;
            dst->ownFrameCount = src->ownFrameCount;
            dst->ownLoaded = true;
        }
    }
    dst->sampleTable = NULL;
    dst->sampleTableCount = 0;
    memset(&dst->ownSample, 0, sizeof(AudioSample));
    dst->spawnAcc = 0.0f;
    memset(dst->grains, 0, sizeof(dst->grains));
    // Audition is transient UI state — never restored from a snapshot.
    dst->auditionNote = 0;
    dst->kbHeldCount = 0;
    dst->mouseNote = -1;
    dst->auditionNoteCount = 0;
    dst->auditionSpawnIdx = 0;
}

static inline void free_undo_snapshot(UndoSnapshot* s) {
    if (s->clips) {
        free(s->clips);
        s->clips = NULL;
    }
    if (s->clipGran) {
         
        for (int i = 0; i < s->clipCount; ++i) {
            if (s->clipGran[i].ownFrames) {
                free(s->clipGran[i].ownFrames);
                s->clipGran[i].ownFrames = NULL;
            }
            s->clipGran[i].ownLoaded = false;
        }
        free(s->clipGran);
        s->clipGran = NULL;
    }
    if (s->trackFx) {
        free(s->trackFx);
        s->trackFx = NULL;
    }
    s->clipCount = 0;
}

static inline void clear_undo_stack(void) {
    for (int i = 0; i < g_Seq.undoCount; ++i) {
        free_undo_snapshot(&g_Seq.undoStack[i]);
    }
    g_Seq.undoCount = 0;
}

static inline void clear_redo_stack(void) {
    for (int i = 0; i < g_Seq.redoCount; ++i) {
        free_undo_snapshot(&g_Seq.redoStack[i]);
    }
    g_Seq.redoCount = 0;
}

static inline void push_undo_state(void) {
    if (g_Seq.isBusy) return;
    seq_lock();
    clear_redo_stack();

     
    InterlockedExchange(&g_allChunksStale, 1);

    if (g_Seq.undoCount >= MAX_UNDO_STATES) {
        free_undo_snapshot(&g_Seq.undoStack[0]);
        memmove(&g_Seq.undoStack[0], &g_Seq.undoStack[1], sizeof(UndoSnapshot) * (MAX_UNDO_STATES - 1));
        g_Seq.undoCount = MAX_UNDO_STATES - 1;
    }

    UndoSnapshot* s = &g_Seq.undoStack[g_Seq.undoCount];
    s->clipCount = g_Seq.clipCount;
    s->trackFx = NULL;
    if (g_Seq.clipCount > 0) {
        s->clips = (Clip*)malloc(sizeof(Clip) * g_Seq.clipCount);
        s->clipGran = (GranClipSnapshot*)malloc(sizeof(GranClipSnapshot) * g_Seq.clipCount);
        if (s->clips && s->clipGran) {
            memcpy(s->clips, g_Seq.clips, sizeof(Clip) * g_Seq.clipCount);
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                gran_engine_to_snapshot(&s->clipGran[i], &g_ClipGran[i]);
            }
        }
        else {
            if (s->clips) { free(s->clips); s->clips = NULL; }
            if (s->clipGran) { free(s->clipGran); s->clipGran = NULL; }
            s->clipCount = 0;
        }
    }
    else {
        s->clips = NULL;
        s->clipGran = NULL;
    }

    // Capture FX state for all tracks
    s->trackFx = (FxTrackSnapshot*)malloc(sizeof(FxTrackSnapshot) * MAX_TRACKS);
    if (s->trackFx) {
        fx_snapshot_save(s->trackFx);
    }

    g_Seq.undoCount++;

     
    g_Seq.isModified = true;

    seq_unlock();
}

static inline void undo_last_action(void) {
    if (g_Seq.isBusy) return;
    seq_lock();
    if (g_Seq.undoCount <= 0) {
        seq_unlock();
        return;
    }

    if (g_Seq.redoCount >= MAX_UNDO_STATES) {
        free_undo_snapshot(&g_Seq.redoStack[0]);
        memmove(&g_Seq.redoStack[0], &g_Seq.redoStack[1], sizeof(UndoSnapshot) * (MAX_UNDO_STATES - 1));
        g_Seq.redoCount = MAX_UNDO_STATES - 1;
    }

    UndoSnapshot* s = &g_Seq.undoStack[--g_Seq.undoCount];

    // Snapshot current state to redoStack before overwriting with s
    UndoSnapshot* r = &g_Seq.redoStack[g_Seq.redoCount++];
    r->clipCount = g_Seq.clipCount;
    r->trackFx = NULL;
    if (g_Seq.clipCount > 0) {
        r->clips = (Clip*)malloc(sizeof(Clip) * g_Seq.clipCount);
        r->clipGran = (GranClipSnapshot*)malloc(sizeof(GranClipSnapshot) * g_Seq.clipCount);
        if (r->clips && r->clipGran) {
            memcpy(r->clips, g_Seq.clips, sizeof(Clip) * g_Seq.clipCount);
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                gran_engine_to_snapshot(&r->clipGran[i], &g_ClipGran[i]);
            }
        } else {
            if (r->clips) { free(r->clips); r->clips = NULL; }
            if (r->clipGran) { free(r->clipGran); r->clipGran = NULL; }
            r->clipCount = 0;
        }
    } else {
        r->clips = NULL;
        r->clipGran = NULL;
    }

    // Capture current FX state into redoStack
    r->trackFx = (FxTrackSnapshot*)malloc(sizeof(FxTrackSnapshot) * MAX_TRACKS);
    if (r->trackFx) {
        fx_snapshot_save(r->trackFx);
    }

    // Restore clips and synths
    g_Seq.clipCount = s->clipCount;
    if (s->clipCount > 0 && s->clips && s->clipGran) {
        memcpy(g_Seq.clips, s->clips, sizeof(Clip) * s->clipCount);
        for (int i = 0; i < s->clipCount; ++i) {
            gran_snapshot_to_engine(&g_ClipGran[i], &s->clipGran[i], i, g_Seq.clips[i].track);
        }
    }

    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].clipKind == CLIP_KIND_QUADRUM) {
            synth_state_init_clip(i);
        }
    }

    // Restore FX rack state
    if (s->trackFx) {
        fx_snapshot_restore(s->trackFx);
    }

    free_undo_snapshot(s);

     
    g_Seq.isModified = true;

    seq_unlock();

     
    cseq_clip_structure_changed();
    mark_all_bars_dirty();

     
    g_timelineDirty = true;
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
    if (g_fxRackHwnd && IsWindow(g_fxRackHwnd)) InvalidateRect(g_fxRackHwnd, NULL, FALSE);
}

static inline void redo_last_action(void) {
    if (g_Seq.isBusy) return;
    seq_lock();
    if (g_Seq.redoCount <= 0) {
        seq_unlock();
        return;
    }

    if (g_Seq.undoCount >= MAX_UNDO_STATES) {
        free_undo_snapshot(&g_Seq.undoStack[0]);
        memmove(&g_Seq.undoStack[0], &g_Seq.undoStack[1], sizeof(UndoSnapshot) * (MAX_UNDO_STATES - 1));
        g_Seq.undoCount = MAX_UNDO_STATES - 1;
    }

    // Snapshot current state to undoStack before overwriting with r
    UndoSnapshot* u = &g_Seq.undoStack[g_Seq.undoCount];
    u->clipCount = g_Seq.clipCount;
    u->trackFx = NULL;
    if (g_Seq.clipCount > 0) {
        u->clips = (Clip*)malloc(sizeof(Clip) * g_Seq.clipCount);
        u->clipGran = (GranClipSnapshot*)malloc(sizeof(GranClipSnapshot) * g_Seq.clipCount);
        if (u->clips && u->clipGran) {
            memcpy(u->clips, g_Seq.clips, sizeof(Clip) * g_Seq.clipCount);
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                gran_engine_to_snapshot(&u->clipGran[i], &g_ClipGran[i]);
            }
        }
        else {
            if (u->clips) { free(u->clips); u->clips = NULL; }
            if (u->clipGran) { free(u->clipGran); u->clipGran = NULL; }
            u->clipCount = 0;
        }
    }
    else {
        u->clips = NULL;
        u->clipGran = NULL;
    }

    // Capture current FX state into undoStack
    u->trackFx = (FxTrackSnapshot*)malloc(sizeof(FxTrackSnapshot) * MAX_TRACKS);
    if (u->trackFx) {
        fx_snapshot_save(u->trackFx);
    }
    g_Seq.undoCount++;

    UndoSnapshot* r = &g_Seq.redoStack[--g_Seq.redoCount];
    g_Seq.clipCount = r->clipCount;
    if (r->clipCount > 0 && r->clips && r->clipGran) {
        memcpy(g_Seq.clips, r->clips, sizeof(Clip) * r->clipCount);
        for (int i = 0; i < r->clipCount; ++i) {
            gran_snapshot_to_engine(&g_ClipGran[i], &r->clipGran[i], i, g_Seq.clips[i].track);
        }
    }

    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].clipKind == CLIP_KIND_QUADRUM) {
            synth_state_init_clip(i);
        }
    }

    // Restore FX rack state
    if (r->trackFx) {
        fx_snapshot_restore(r->trackFx);
    }

    free_undo_snapshot(r);

     
    g_Seq.isModified = true;

    seq_unlock();

     
    cseq_clip_structure_changed();
    mark_all_bars_dirty();

     
    g_timelineDirty = true;
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
    if (g_fxRackHwnd && IsWindow(g_fxRackHwnd)) InvalidateRect(g_fxRackHwnd, NULL, FALSE);
}