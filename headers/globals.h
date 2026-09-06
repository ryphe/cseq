#pragma once
#include <stdio.h>

#include "font.h"
#include "types.h"
#include "utf8.h"

extern HFONT g_hFontUI;

extern SequencerState g_Seq;
extern HWND g_hWnd;

// Hit-rect of the bottom-dock "Master" volume button, updated every frame by
// render_ui() so WM_LBUTTONDOWN can test the click without re-deriving layout.
extern RECT g_masterVolBtnRect;

extern GranularEngine g_TrackGran[MAX_TRACKS];
extern GranularEngine g_ClipGran[MAX_CLIPS];

// Posted to the main window by worker threads when they need a UI refresh;
// handled entirely on the UI thread (see cseq_main_wndproc).
#define WM_APP_FULL_REDRAW (WM_APP + 1)

 
extern HANDLE        g_hMainPacerThread;
extern volatile LONG g_mainPacerRunning;

 
extern volatile LONG g_timelineDynamicDirty;

// Set to 1 by WinMain during shutdown, before the device is uninitialized and
// the critical sections are deleted. Job workers check it and bail so a
// detached worker can never enter seq_lock()/midi_lock() after the lock has
// been destroyed (use-after-free on window close during a load/save/export).
extern volatile LONG g_shuttingDown;


 
#define VIS_MAX_FFT 8192

typedef struct {
    HWND  hwnd;
    int   mode;             
    int   fftSize;          
    float zoom;             
    float hue;              
    int   channels;         
    bool  isFrozen;
    int   mouseX, mouseY;
    bool  isDraggingHue;
    int   dragStartX;
    float dragStartHue;

     
    float specBinsL[VIS_MAX_FFT / 2];
    float specBinsR[VIS_MAX_FFT / 2];
    float specPeaks[VIS_MAX_FFT / 2];
    float peakDecay[VIS_MAX_FFT / 2];

     
    float freezeL[VIS_MAX_FFT];
    float freezeR[VIS_MAX_FFT];
} VisualizerState;

extern VisualizerState g_Vis;
extern HWND g_visHwnd;
extern volatile LONG g_visBadgeHover;    

static inline void seq_lock(void) {
    EnterCriticalSection(&g_Seq.lock);
}

static inline void seq_unlock(void) {
    LeaveCriticalSection(&g_Seq.lock);
}

 
static inline bool seq_is_playing(void) {
    return (InterlockedCompareExchange(&g_Seq.isPlaying, 0, 0) != 0);
}

static inline void seq_set_playing(bool play) {
    InterlockedExchange(&g_Seq.isPlaying, play ? 1 : 0);
}

 
static inline void midi_lock(void) {
    EnterCriticalSection(&g_midiLock);
}

static inline void midi_unlock(void) {
    LeaveCriticalSection(&g_midiLock);
}

 
static inline bool job_is_busy(void) {
    return InterlockedCompareExchange(&g_Seq.isBusy, 0, 0) != 0;
}

static inline bool job_begin(int kind, const char* path) {
    // Atomic test-and-set so two callers can't both observe "not busy" and
    // start two jobs concurrently.
    if (InterlockedCompareExchange(&g_Seq.isBusy, 1, 0) != 0) return false;
    InterlockedExchange(&g_Seq.jobProgress, 0);
    InterlockedExchange(&g_Seq.jobKind, kind);
    if (path) {
        strncpy(g_Seq.jobPath, path, MAX_PATH - 1);
        g_Seq.jobPath[MAX_PATH - 1] = '\0';
    } else {
        g_Seq.jobPath[0] = '\0';
    }
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    return true;
}

static inline void job_end(const char* successMsg) {
    InterlockedExchange(&g_Seq.isBusy, 0);
    InterlockedExchange(&g_Seq.jobProgress, 0);
    InterlockedExchange(&g_Seq.jobKind, 0);
    if (successMsg) {
        snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg), "%s", successMsg);
        g_Seq.exportMsgActive = true;
        g_Seq.exportMsgExpiry = GetTickCount64() + 4000;
    }
    if (g_hWnd) PostMessageA(g_hWnd, WM_APP_FULL_REDRAW, 0, 0);
}

static inline void job_set_progress(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    InterlockedExchange(&g_Seq.jobProgress, pct);
}

 

static inline int track_mask_test(const TrackMask128* m, int idx) {
    if (idx < 0 || idx >= 128) return 0;
    return (idx < 64) ? (int)((m->lo >> idx) & 1ULL)
                      : (int)((m->hi >> (idx - 64)) & 1ULL);
}

static inline void track_mask_set(TrackMask128* m, int idx) {
    if (idx < 0 || idx >= 128) return;
    if (idx < 64) m->lo |= (1ULL << idx);
    else          m->hi |= (1ULL << (idx - 64));
}

static inline void track_mask_clear(TrackMask128* m, int idx) {
    if (idx < 0 || idx >= 128) return;
    if (idx < 64) m->lo &= ~(1ULL << idx);
    else          m->hi &= ~(1ULL << (idx - 64));
}

static inline bool track_mask_is_empty(const TrackMask128* m) {
    return !(m->lo | m->hi);
}

 
static inline int ctz_u64(uint64_t v) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    if (!_BitScanForward64(&i, v)) return 64;
    return (int)i;
#elif defined(_MSC_VER)
    unsigned long i;
    uint32_t lo = (uint32_t)v;
    if (lo) { _BitScanForward(&i, lo); return (int)i; }
    uint32_t hi = (uint32_t)(v >> 32);
    if (hi) { _BitScanForward(&i, hi); return 32 + (int)i; }
    return 64;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(v);
#else
    int i = 0;
    while (i < 64 && !((v >> i) & 1ULL)) ++i;
    return i;
#endif
}

 
static inline bool track_mask_any_range(const TrackMask128* m, int t0, int count) {
    if (t0 < 0) return false;
    int t1 = t0 + count; if (t1 > 128) t1 = 128;
    for (int t = t0; t < t1; ++t) if (track_mask_test(m, t)) return true;
    return false;
}

static inline void bar_bit_set(BarBitfield* bf, int bar) {
    if (bar < 0 || bar >= MAX_BARS) return;
    InterlockedOr64((volatile LONG64*)&(*bf)[bar >> 6], (LONG64)(1ULL << (bar & 63)));
}

static inline void bar_bit_clear(BarBitfield* bf, int bar) {
    if (bar < 0 || bar >= MAX_BARS) return;
    InterlockedAnd64((volatile LONG64*)&(*bf)[bar >> 6], (LONG64)~(1ULL << (bar & 63)));
}

static inline bool bar_bit_test(const BarBitfield* bf, int bar) {
    if (bar < 0 || bar >= MAX_BARS) return false;
    return (((*bf)[bar >> 6] >> (bar & 63)) & 1ULL) != 0ULL;
}

static inline bool bar_bitfield_any(const BarBitfield* bf) {
    uint64_t acc = 0;
    for (int w = 0; w < BAR_BITFIELD_WORDS; ++w) acc |= (*bf)[w];
    return acc != 0ULL;
}

 
static inline TrackMask128 compute_active_mask(TrackMask128 solo, TrackMask128 mute, TrackMask128 hasAudio) {
    TrackMask128 r;
    if (!track_mask_is_empty(&solo)) {
        r.lo = hasAudio.lo & solo.lo & ~mute.lo;
        r.hi = hasAudio.hi & solo.hi & ~mute.hi;
    } else {
        r.lo = hasAudio.lo & ~mute.lo;
        r.hi = hasAudio.hi & ~mute.hi;
    }
    return r;
}

 
static inline TrackMask128 compute_playable_mask(TrackMask128 solo, TrackMask128 mute) {
    TrackMask128 r;
    if (!track_mask_is_empty(&solo)) {
        r.lo = solo.lo & ~mute.lo;
        r.hi = solo.hi & ~mute.hi;
    } else {
        r.lo = ~mute.lo;
        r.hi = ~mute.hi;
    }
    return r;
}

 
static volatile LONG g_clipMapStale   = 1;   
static volatile LONG g_allChunksStale = 1;   

static inline float beats_per_bar(void) {
    int n = (g_Seq.timeSigNum > 0) ? g_Seq.timeSigNum : 4;
    int d = (g_Seq.timeSigDen > 0) ? g_Seq.timeSigDen : 4;
    return (float)n * 4.0f / (float)d;
}

static inline void clip_bar_range(const Clip* c, int* b0, int* b1) {
     
    const float kBar = beats_per_bar();
    int s = (int)(c->startBeat / kBar);
    int e = (int)((c->startBeat + c->lengthBeats - 0.0001f) / kBar);
    if (s < 0) s = 0;
    if (e >= MAX_BARS) e = MAX_BARS - 1;
    if (e < s) e = s;
    *b0 = s;
    *b1 = e;
}

 
static inline void mark_clip_bars_dirty(const Clip* c) {
    if (!c) return;
    int b0, b1;
    clip_bar_range(c, &b0, &b1);
    for (int b = b0; b <= b1; ++b) bar_bit_set(&g_Seq.barDirty, b);
    InterlockedExchange(&g_allChunksStale, 1);
}

static inline void mark_all_bars_dirty(void) {
    for (int w = 0; w < BAR_BITFIELD_WORDS; ++w)
        InterlockedExchange64((volatile LONG64*)&g_Seq.barDirty[w], (LONG64)-1);
    InterlockedExchange(&g_allChunksStale, 1);
}

 
static inline void rebuild_bar_presence(void) {
    for (int w = 0; w < BAR_BITFIELD_WORDS; ++w) g_Seq.barPresence[w] = 0ULL;
    g_Seq.hasAudioMask.lo = 0ULL;
    g_Seq.hasAudioMask.hi = 0ULL;
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        const Clip* c = &g_Seq.clips[i];
        if (c->track < 0 || c->track >= MAX_TRACKS) continue;
        track_mask_set(&g_Seq.hasAudioMask, c->track);
        int b0, b1;
        clip_bar_range(c, &b0, &b1);
        for (int b = b0; b <= b1; ++b) bar_bit_set(&g_Seq.barPresence, b);
    }
}

 
static inline void rebuild_bar_to_clip_grid(void) {
    for (int t = 0; t < MAX_TRACKS; ++t) {
        uint16_t* row = g_Seq.barToClip[t];
        for (int b = 0; b < MAX_BARS; ++b) row[b] = 0xFFFF;
    }
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        Clip* c = &g_Seq.clips[i];
        c->nextClipInBar = 0xFFFF;
    }
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        Clip* c = &g_Seq.clips[i];
        if (c->track < 0 || c->track >= MAX_TRACKS) continue;
        int b0, b1;
        clip_bar_range(c, &b0, &b1);
        uint16_t* row = g_Seq.barToClip[c->track];
        if (row[b0] == 0xFFFF) {
            row[b0] = (uint16_t)i;
        } else {
             
            uint16_t n = row[b0];
            int guard = 0;
            while (g_Seq.clips[n].nextClipInBar != 0xFFFF && guard++ < MAX_CLIPS_PER_BAR * 4) {
                n = g_Seq.clips[n].nextClipInBar;
            }
            g_Seq.clips[n].nextClipInBar = (uint16_t)i;
        }
        for (int b = b0 + 1; b <= b1; ++b) {
            if (row[b] == 0xFFFF) row[b] = (uint16_t)i;   
        }
    }
}

 
static inline void seq_sync_track_masks(void) {
    g_Seq.soloMask.lo = 0ULL; g_Seq.soloMask.hi = 0ULL;
    g_Seq.muteMask.lo = 0ULL; g_Seq.muteMask.hi = 0ULL;
    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (g_Seq.trackMuted[t]) track_mask_set(&g_Seq.muteMask, t);
        if (g_Seq.trackSolo[t])  track_mask_set(&g_Seq.soloMask, t);
    }
}

 
static inline void seq_set_track_mute(int t, bool muted) {
    if (t < 0 || t >= MAX_TRACKS) return;
    g_Seq.trackMuted[t] = muted;
    g_Seq.trackUI[t].muted = muted;
    if (muted) track_mask_set(&g_Seq.muteMask, t);
    else       track_mask_clear(&g_Seq.muteMask, t);
}

static inline void seq_set_track_solo(int t, bool solo) {
    if (t < 0 || t >= MAX_TRACKS) return;
    g_Seq.trackSolo[t] = solo;
    g_Seq.trackUI[t].solo = solo;
    if (solo) track_mask_set(&g_Seq.soloMask, t);
    else      track_mask_clear(&g_Seq.soloMask, t);
}

 
static inline void mark_selected_clips_bars_dirty(void) {
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        if (g_Seq.clips[i].isSelected) mark_clip_bars_dirty(&g_Seq.clips[i]);
    }
    seq_unlock();
}

 
static inline void cseq_clip_structure_changed(void) {
    InterlockedExchange(&g_clipMapStale, 0);
    seq_lock();
    seq_sync_track_masks();
    rebuild_bar_presence();
    rebuild_bar_to_clip_grid();
    mark_all_bars_dirty();
    seq_unlock();
    InterlockedExchange(&g_allChunksStale, 1);
}

 
static inline void cseq_rebuild_clip_maps_only(void) {
    InterlockedExchange(&g_clipMapStale, 0);
    seq_lock();
    seq_sync_track_masks();
    rebuild_bar_presence();
    rebuild_bar_to_clip_grid();
    seq_unlock();
}
