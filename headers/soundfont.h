#pragma once

// SoundFont (tsf.h) integration for MIDI clips.
//
// Instead of streaming the active preset through a shared, single-instance tsf
// synth in the real-time audio thread (which forced a single ADSR envelope
// across the whole mixed chunk and desynced on play/pause), the active preset
// is PRE-RENDERED once into a bank of static PCM buffers. Playback then treats
// each SoundFont note exactly like a regular sample file: it reads the
// pre-rendered PCM for the note's pitch and applies the per-voice clip ADSR in
// audio.h, so every note gets its own envelope and play/pause/seek/export are
// fully stateless. The tsf synth is only ever touched by the (offline) build
// thread; the real-time audio path never locks or calls into tsf.

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// Provided by globals.h / types.h; declared here because this header is
// included early in the translation unit (before globals.h's externs).
extern SequencerState g_Seq;
extern HWND g_hWnd;
extern HWND g_midiHwnd;

// Job plumbing from globals.h (defined later in the include chain; the
// declarations must exist before the SFont load thread references them).
static inline bool job_begin(int kind, const char* path);
static inline void job_end(const char* successMsg);
static inline void job_set_progress(int pct);

#define SFONT_MAX_PRESETS 512

// Largest single note we pre-render: 3 seconds at the project sample rate.
#define SFONT_CACHE_MAX_FRAMES (SAMPLE_RATE * 3)
// Build chunk size for the offline render loop (not the audio callback).
#define SFONT_CACHE_BUILD_CHUNK 1024
// Micro-fade length (samples) applied at the end of a truncated note so a
// long note cut off at the 3s cap doesn't click.
#define SFONT_CACHE_FADE_SAMPLES 256

typedef struct {
    tsf*       synth;                 // NULL = no soundfont loaded
    CRITICAL_SECTION lock;            // serializes cache publish / teardown
    bool       lockInit;
    bool       loading;               // worker thread is decoding the file
    char       path[MAX_PATH];
    char       name[64];
    int        presetCount;
    struct { int index; char name[64]; } presets[SFONT_MAX_PRESETS];
    int        activePreset;          // preset_index used for new notes
} SoundFontState;

static SoundFontState g_SFont = { 0 };

static inline void sfont_init_lock(void) {
    if (!g_SFont.lockInit) {
        // Spin count is advisory; failure to pre-allocate the spin event is
        // non-fatal, the lock still works.
        (void)InitializeCriticalSectionAndSpinCount(&g_SFont.lock, 4000);
        g_SFont.lockInit = true;
    }
}

static inline bool sfont_is_loaded(void) {
    return !g_SFont.loading && g_SFont.synth != NULL;
}

static inline bool sfont_is_loading(void) {
    return g_SFont.loading;
}

static inline const char* sfont_name(void) {
    return g_SFont.name[0] ? g_SFont.name : NULL;
}

static inline int sfont_preset_count(void) {
    return sfont_is_loaded() ? g_SFont.presetCount : 0;
}

static inline const char* sfont_preset_name(int i) {
    if (i < 0 || i >= g_SFont.presetCount) return "";
    return g_SFont.presets[i].name;
}

static inline int sfont_preset_index(int i) {
    if (i < 0 || i >= g_SFont.presetCount) return 0;
    return g_SFont.presets[i].index;
}

static inline int sfont_active_preset_slot(void) {
    return g_SFont.activePreset;
}

static void sfont_cache_build_preset(int presetSlot);
static void sfont_unload(void);

// ---- Pre-rendered sample bank --------------------------------------------
// The active preset's 128 MIDI notes are pre-rendered once into an in-memory
// bank of static PCM buffers. The audio thread reads these exactly like any
// other AudioSample (no tsf access, no locks on the hot path). Rebuilds happen
// on the loader thread (default preset) or a short-lived worker thread (preset
// switch); a rebuild builds the whole new bank into a temp copy and publishes
// it atomically under the lock, so the live bank is never seen half-built.

typedef struct {
    AudioSample notes[128];   // PCM buffers for MIDI keys 0..127
    int  activePresetSlot;
    bool ready;
} SFontSampleCache;

// Live bank read by the audio thread; build bank is the offline render target.
static SFontSampleCache g_SFontCache = { 0 };
static SFontSampleCache g_SFontCacheBuild = { 0 };

// ---- tsf lifetime pinning (prevents use-after-free across threads) -------
// sfont_cache_build_preset drives a tsf* on a worker/loader thread. A
// concurrent sfont_unload()/sfont_load_file_internal() swaps g_SFont.synth and
// would otherwise tsf_close() the very instance being rendered. The build pins
// the tsf it holds (bumps g_sfontBuildRef); a close encountered while a build
// is active is deferred into g_sfontDeferredClose and flushed when the last
// build releases its pin. Deferred closes are guarded by g_SFont.lock.
//
// The offline render target g_SFontCacheBuild is only ever touched by the
// build worker and by sfont_cache_clear() (on the UI/load thread); the audio
// thread never reads it. Access to it is serialized with a dedicated build
// lock so an unload can't free the bank a build is mid-write into, without
// stalling the audio thread's sfont_get_sample reads.
static volatile LONG g_sfontBuildRef = 0;
static tsf* g_sfontDeferredClose[4] = { NULL, NULL, NULL, NULL };
static CRITICAL_SECTION g_SFontBuildLock;
static int g_SFontBuildLockInit = 0;

static inline void sfont_build_lock_init(void) {
    if (!g_SFontBuildLockInit) {
        InitializeCriticalSection(&g_SFontBuildLock);
        g_SFontBuildLockInit = 1;
    }
}
static inline void sfont_build_lock(void)   { sfont_build_lock_init(); EnterCriticalSection(&g_SFontBuildLock); }
static inline void sfont_build_unlock(void) { LeaveCriticalSection(&g_SFontBuildLock); }

static inline tsf* sfont_synth_pin(void) {
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    tsf* s = g_SFont.synth;
    if (s) InterlockedIncrement(&g_sfontBuildRef);
    LeaveCriticalSection(&g_SFont.lock);
    return s;
}

// Close a tsf now, or defer it if a build still holds a reference.
static inline void sfont_close_or_defer(tsf* old) {
    if (!old) return;
    bool defer = false;
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    if (InterlockedCompareExchange(&g_sfontBuildRef, 0, 0) > 0) {
        // Stash for later flush; a full deferred list means we drop the
        // oldest — that tsf is no longer referenced by any build.
        int i;
        for (i = 0; i < 4; ++i) {
            if (!g_sfontDeferredClose[i]) { g_sfontDeferredClose[i] = old; break; }
        }
        if (i == 4) {
            tsf* drop = g_sfontDeferredClose[0];
            g_sfontDeferredClose[0] = old;
            defer = false;
            LeaveCriticalSection(&g_SFont.lock);
            if (drop) tsf_close(drop);
            return;
        }
        defer = true;
    }
    LeaveCriticalSection(&g_SFont.lock);
    if (!defer) tsf_close(old);
}

// Release a build's pin and flush any deferred closes now that it is safe.
static inline void sfont_synth_unpin(tsf* s) {
    if (!s) return;
    InterlockedDecrement(&g_sfontBuildRef);
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    if (InterlockedCompareExchange(&g_sfontBuildRef, 0, 0) == 0) {
        for (int i = 0; i < 4; ++i) {
            if (g_sfontDeferredClose[i]) {
                tsf* old = g_sfontDeferredClose[i];
                g_sfontDeferredClose[i] = NULL;
                LeaveCriticalSection(&g_SFont.lock);
                tsf_close(old);
                EnterCriticalSection(&g_SFont.lock);
            }
        }
    }
    LeaveCriticalSection(&g_SFont.lock);
}

// Free every PCM buffer in a bank (used to tear a bank down).
static inline void sfont_cache_free_bank(SFontSampleCache* b) {
    for (int i = 0; i < 128; ++i) {
        if (b->notes[i].pFrames) {
            free(b->notes[i].pFrames);
            b->notes[i].pFrames = NULL;
        }
        b->notes[i].frameCount = 0;
        b->notes[i].loaded = false;
    }
    b->ready = false;
    b->activePresetSlot = 0;
}

// Reset a bank to all-NULL without freeing (ownership of the buffers has been
// moved elsewhere, e.g. from the build bank into the live bank on publish).
static inline void sfont_cache_zero_bank(SFontSampleCache* b) {
    memset(b->notes, 0, sizeof(b->notes));
    b->ready = false;
    b->activePresetSlot = 0;
}

// Purge the live cache. Safe because callers (unload/clear) tear the soundfont
// down with the transport/device stopped.
static inline void sfont_cache_clear(void) {
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    sfont_cache_free_bank(&g_SFontCache);
    LeaveCriticalSection(&g_SFont.lock);
    // The build bank is only touched by the build worker and here; serialize
    // so an in-flight build can't be writing into a bank we free.
    sfont_build_lock();
    sfont_cache_free_bank(&g_SFontCacheBuild);
    sfont_build_unlock();
}

// Returns the pre-rendered PCM for a MIDI pitch, or NULL if the pitch is out
// of range or has no region in the active preset. Locked only to fetch the
// pointer (a rebuild may publish a new bank); the returned buffer is immutable
// while it is in use.
static inline const AudioSample* sfont_get_sample(int pitch) {
    if (pitch < 0 || pitch >= 128) return NULL;
    const AudioSample* s = NULL;
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    if (g_SFontCache.ready && g_SFontCache.notes[pitch].loaded)
        s = &g_SFontCache.notes[pitch];
    LeaveCriticalSection(&g_SFont.lock);
    return s;
}

// True once a SoundFont is loaded and its active preset has been pre-rendered.
static inline bool sfont_is_ready(void) {
    return g_SFont.synth != NULL && g_SFontCache.ready;
}

// Returns the pre-rendered PCM for a MIDI pitch, or the nearest loaded pitch
// (with the found key written to *outFoundPitch) if the exact key has no
// region in the active preset — a gap in the SF2 key map. The caller
// pitch-shifts the returned sample from *outFoundPitch to the requested key.
// Returns NULL only if the bank has no loaded notes at all.
static inline const AudioSample* sfont_get_sample_nearest(int pitch, int* outFoundPitch) {
    if (pitch < 0) pitch = 0;
    if (pitch > 127) pitch = 127;
    const AudioSample* exact = sfont_get_sample(pitch);
    if (exact) { if (outFoundPitch) *outFoundPitch = pitch; return exact; }
    // Search outward from the requested key for the nearest loaded note.
    for (int d = 1; d <= 127; ++d) {
        int lo = pitch - d, hi = pitch + d;
        if (lo >= 0) {
            const AudioSample* s = sfont_get_sample(lo);
            if (s) { if (outFoundPitch) *outFoundPitch = lo; return s; }
        }
        if (hi < 128) {
            const AudioSample* s = sfont_get_sample(hi);
            if (s) { if (outFoundPitch) *outFoundPitch = hi; return s; }
        }
    }
    return NULL;
}

// ---- Offline pre-render --------------------------------------------------

// Pre-render every MIDI note of the given preset slot into g_SFontCacheBuild,
// then publish it atomically into g_SFontCache. Runs on a non-audio thread
// (the loader thread, or the preset-switch worker); it is the ONLY code that
// calls into tsf at runtime.
static void sfont_cache_build_preset(int presetSlot) {
    // Pin the tsf we are about to render so a concurrent unload/load defers
    // its tsf_close() instead of freeing the instance mid-build.
    tsf* syn = sfont_synth_pin();
    if (!syn) return;
    int presetIdx = sfont_preset_index(presetSlot);
    if (presetIdx < 0 || presetIdx >= syn->presetNum) { sfont_synth_unpin(syn); return; }

    // Serialize access to the offline render bank (g_SFontCacheBuild) with the
    // UI/load thread's sfont_cache_clear(). The audio thread never touches this
    // bank, so the build lock is never on the realtime path.
    sfont_build_lock();

    // Render into the temp bank so the live bank the audio thread reads is
    // never observed in a half-built state.
    sfont_cache_free_bank(&g_SFontCacheBuild);
    g_SFontCacheBuild.activePresetSlot = presetSlot;

    float scratch[SFONT_CACHE_BUILD_CHUNK * 2];
    const struct tsf_preset* p = &syn->presets[presetIdx];

    for (int k = 0; k < 128; ++k) {
        // Does this preset have any region covering key k at full velocity?
        // (tsf_note_on with vel=1.0 -> midiVelocity 127, so the note only
        // sounds if 127 is inside the region's velocity range.)
        bool covered = false;
        for (int r = 0; r < p->regionNum; ++r) {
            const struct tsf_region* rg = &p->regions[r];
            if (k >= rg->lokey && k <= rg->hikey &&
                rg->lovel <= 127 && rg->hivel >= 127) { covered = true; break; }
        }
        if (!covered) continue;

        size_t maxBytes = (size_t)SFONT_CACHE_MAX_FRAMES * 2 * sizeof(float);
        float* accum = (float*)malloc(maxBytes);
        if (!accum) continue;

        tsf_reset(syn);
        tsf_note_on(syn, presetIdx, k, 1.0f);

        int rendered = 0;
        while (rendered < SFONT_CACHE_MAX_FRAMES) {
            int chunk = SFONT_CACHE_BUILD_CHUNK;
            if (rendered + chunk > SFONT_CACHE_MAX_FRAMES)
                chunk = SFONT_CACHE_MAX_FRAMES - rendered;
            tsf_render_float(syn, scratch, chunk, 0);
            memcpy(accum + (size_t)rendered * 2, scratch, (size_t)chunk * 2 * sizeof(float));
            rendered += chunk;
            // Note died naturally; stop early to save time and memory.
            if (tsf_active_voice_count(syn) == 0) break;
        }

        // Micro-fade the tail to zero so a note still ringing at the 3s cap
        // truncates without a click.
        if (rendered > 0) {
            int fadeN = (rendered < SFONT_CACHE_FADE_SAMPLES)
                        ? rendered : SFONT_CACHE_FADE_SAMPLES;
            for (int i = 0; i < fadeN; ++i) {
                float g = (float)i / (float)fadeN;
                accum[(size_t)(rendered - fadeN + i) * 2 + 0] *= g;
                accum[(size_t)(rendered - fadeN + i) * 2 + 1] *= g;
            }
        }

        // Shrink to the actual rendered length to reclaim memory.
        float* final = (float*)realloc(accum, (size_t)rendered * 2 * sizeof(float));
        if (!final) final = accum;   // keep the full buffer if shrink fails

        g_SFontCacheBuild.notes[k].pFrames    = final;
        g_SFontCacheBuild.notes[k].frameCount = (ma_uint64)rendered;
        g_SFontCacheBuild.notes[k].loaded     = (rendered > 0);
    }

    // Publish the finished bank and retire the previous one atomically.
    g_SFontCacheBuild.ready = true;
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    sfont_cache_free_bank(&g_SFontCache);
    g_SFontCache = g_SFontCacheBuild;      // buffers now owned by the live bank
    sfont_cache_zero_bank(&g_SFontCacheBuild);
    LeaveCriticalSection(&g_SFont.lock);
    sfont_build_unlock();

    // Release our pin; flush any deferred close now that it is safe.
    sfont_synth_unpin(syn);
}

// ---- Note control --------------------------------------------------------
// With pre-rendered samples, note on/off no longer drive the live synth. These
// remain as no-ops so legacy UI callers (the instrument-selector preview blip)
// compile; the actual audition/playback audio comes from the sample bank via
// the unified voice engine in audio.h.

static inline void sfont_note_on(int presetIndex, int key, float vel) { (void)presetIndex; (void)key; (void)vel; }
static inline void sfont_note_off(int presetIndex, int key) { (void)presetIndex; (void)key; }
static inline void sfont_note_off_all(void) { }

// Background worker that re-pre-renders the active preset after a preset
// switch. Keeps the (potentially slow) build off the UI thread.
static volatile LONG g_SFontCacheBuildBusy = 0;   // 0/1 (Interlocked)
static volatile LONG g_sfontBuildShutdown = 0;    // 1 = app shutting down
static HANDLE        g_SFontCacheBuildThread = NULL;
static int           g_SFontPendingPresetSlot = 0;

static DWORD WINAPI SFontCacheBuildThreadProc(LPVOID lpParam) {
    (void)lpParam;
    int slot = g_SFontPendingPresetSlot;
    if (!InterlockedCompareExchange(&g_sfontBuildShutdown, 0, 0))
        sfont_cache_build_preset(slot);
    InterlockedExchange(&g_SFontCacheBuildBusy, 0);
    HANDLE h = (HANDLE)InterlockedExchangePointer((void* volatile*)&g_SFontCacheBuildThread, NULL);
    if (h) CloseHandle(h);
    return 0;
}

// Switch the active preset and pre-render its note bank on a worker thread.
// The live bank stays playable until the new one is published.
static inline void sfont_set_active_preset_slot(int slot) {
    if (slot < 0 || slot >= g_SFont.presetCount) return;
    if (slot == g_SFontCache.activePresetSlot) return;
    g_SFont.activePreset = slot;
    if (!g_SFont.synth) return;
    if (InterlockedCompareExchange(&g_SFontCacheBuildBusy, 0, 0)) return; // rebuild in flight

    InterlockedExchange(&g_SFontCacheBuildBusy, 1);
    g_SFontPendingPresetSlot = slot;
    HANDLE hThread = CreateThread(NULL, 0, SFontCacheBuildThreadProc, NULL, 0, NULL);
    if (hThread) {
        InterlockedExchangePointer((void* volatile*)&g_SFontCacheBuildThread, hThread);
    } else {
        InterlockedExchange(&g_SFontCacheBuildBusy, 0);
        // Fall back to building synchronously so the preset still works.
        sfont_cache_build_preset(slot);
    }
}

// Wait for an in-flight preset-build worker to finish. Called at shutdown
// before the SoundFont critical section is deleted, so a worker can never
// enter a freed lock. Also honored by the worker via g_sfontBuildShutdown.
static inline void sfont_build_worker_wait(void) {
    InterlockedExchange(&g_sfontBuildShutdown, 1);
    for (int i = 0; i < 400 && InterlockedCompareExchange(&g_SFontCacheBuildBusy, 0, 0); ++i)
        Sleep(5);
    HANDLE h = (HANDLE)InterlockedExchangePointer((void* volatile*)&g_SFontCacheBuildThread, NULL);
    if (h) {
        WaitForSingleObject(h, 2000);
        CloseHandle(h);
    }
}

// Unload the soundfont entirely (the [X] button). Any loaded sample is
// untouched — that lives in g_Seq.
static inline void sfont_clear(void) {
    sfont_unload();
}

// ---- Loading -------------------------------------------------------------

static void sfont_unload(void) {
    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    tsf* old = g_SFont.synth;
    g_SFont.synth = NULL;
    g_SFont.presetCount = 0;
    g_SFont.activePreset = 0;
    g_SFont.path[0] = '\0';
    g_SFont.name[0] = '\0';
    LeaveCriticalSection(&g_SFont.lock);
    // Defer the close if a build worker still holds a pin on this instance.
    sfont_close_or_defer(old);
    sfont_cache_clear();
}

// Core loading logic, thread-neutral. Runs synchronously on the calling
// thread: either the async loader thread (UI-initiated loads) or the project
// load thread (sfont_load_sync). Publishes the new tsf instance + preset
// table atomically under g_SFont.lock, then pre-renders the target preset's
// note cache so notes are immediately playable.
static bool sfont_load_file_internal(const char* filepath, int targetPresetSlot) {
    if (!filepath || !filepath[0]) return false;

    tsf* f = tsf_load_filename(filepath);
    if (!f) return false;

    tsf_set_output(f, TSF_STEREO_INTERLEAVED, SAMPLE_RATE, 0.0f);
    tsf_set_volume(f, 0.8f);
    tsf_set_max_voices(f, 64);

    // Index the preset table OUTSIDE the lock: tsf_get_presetname + snprintf
    // per preset (up to 512) is non-trivial work that would stall the audio
    // thread if done while holding the lock. Build into a local temp table,
    // then swap it in atomically under the lock so any concurrent reader sees
    // either the old font or the new.
    int newCount = tsf_get_presetcount(f);
    if (newCount > SFONT_MAX_PRESETS) newCount = SFONT_MAX_PRESETS;
    struct { int index; char name[64]; } tempPresets[SFONT_MAX_PRESETS];
    for (int i = 0; i < newCount; ++i) {
        const char* nm = tsf_get_presetname(f, i);
        tempPresets[i].index = i;
        snprintf(tempPresets[i].name, sizeof(tempPresets[0].name), "%s",
                 (nm && nm[0]) ? nm : "Untitled");
    }

    if (targetPresetSlot < 0 || targetPresetSlot >= newCount) targetPresetSlot = 0;

    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    tsf* old = g_SFont.synth;
    g_SFont.presetCount = newCount;
    for (int i = 0; i < newCount; ++i) {
        // Anonymous structs are distinct types in C, so copy field-by-field.
        g_SFont.presets[i].index = tempPresets[i].index;
        strncpy(g_SFont.presets[i].name, tempPresets[i].name,
                sizeof(g_SFont.presets[i].name) - 1);
        g_SFont.presets[i].name[sizeof(g_SFont.presets[i].name) - 1] = '\0';
    }
    g_SFont.activePreset = targetPresetSlot;
    strncpy(g_SFont.path, filepath, MAX_PATH - 1);
    g_SFont.path[MAX_PATH - 1] = '\0';
    const char* base = strrchr(filepath, '\\');
    const char* fwd = strrchr(filepath, '/');
    if (fwd && (!base || fwd > base)) base = fwd;
    base = base ? base + 1 : filepath;
    strncpy(g_SFont.name, base, sizeof(g_SFont.name) - 1);
    g_SFont.name[sizeof(g_SFont.name) - 1] = '\0';
    g_SFont.synth = f;
    g_SFont.loading = false;
    LeaveCriticalSection(&g_SFont.lock);
    // Defer the close if a build worker still holds a pin on the old instance.
    sfont_close_or_defer(old);

    // Pre-render the active preset so the soundfont is immediately playable.
    // This runs on the loader thread (not the audio thread) and is the only
    // tsf work performed at load.
    sfont_cache_build_preset(targetPresetSlot);
    return true;
}

// Synchronous loader for LoadProjectThreadProc: the project loader already
// runs inside a job (job_kind 2), so job_begin() would fail; this runs the
// parse + note-cache build directly on the project load thread, finishing
// before the audio device is restarted.
static inline bool sfont_load_sync(const char* filepath, int targetPresetSlot) {
    const char* dot = strrchr(filepath, '.');
    if (!dot || _stricmp(dot, ".sf2") != 0) return false;
    return sfont_load_file_internal(filepath, targetPresetSlot);
}

static DWORD WINAPI SFontLoadThreadProc(LPVOID lpParam) {
    (void)lpParam;
    const char* path = g_Seq.jobPath;

    job_set_progress(10);
    bool ok = sfont_load_file_internal(path, 0);
    if (!ok) {
        g_SFont.loading = false;
        job_end(NULL);
        if (g_hWnd) {
            cseq_report_error(g_hWnd, "SoundFont Error", "Could not load the SoundFont (.sf2) file.");
        }
        return 1;
    }

    job_set_progress(100);
    job_end(NULL);
    // Worker thread: route the repaint request through the message queue.
    if (g_midiHwnd && IsWindow(g_midiHwnd)) InvalidateRect(g_midiHwnd, NULL, FALSE);
    return 0;
}

// Start an async .sf2 load on the shared job system. Returns false if a
// job is already running or the extension isn't .sf2.
static inline bool sfont_load_async(const char* filepath) {
    const char* dot = strrchr(filepath, '.');
    if (!dot || _stricmp(dot, ".sf2") != 0) return false;
    if (!job_begin(5, filepath)) return false;

    sfont_init_lock();
    EnterCriticalSection(&g_SFont.lock);
    g_SFont.loading = true;
    LeaveCriticalSection(&g_SFont.lock);

    HANDLE hThread = CreateThread(NULL, 0, SFontLoadThreadProc, NULL, 0, NULL);
    if (!hThread) {
        EnterCriticalSection(&g_SFont.lock);
        g_SFont.loading = false;
        LeaveCriticalSection(&g_SFont.lock);
        job_end(NULL);
        return false;
    }
    CloseHandle(hThread);
    return true;
}
