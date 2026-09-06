#pragma once
#include <string.h>
#include <stdlib.h>
#include "scrollbar.h"

// ---------------------------------------------------------------------------
// Media Explorer — non-modal folder/file browser + audition previewer.
//
// Mirrors the FX-rack dialog pattern (lazy RegisterClassA + CreateWindowExA,
// returns to the main loop so it is fully modeless). The UI thread renders the
// panes; a directory-scan worker enumerates folders/files incrementally; a
// preview-decode worker decodes the selected file into the audition voice's
// inactive ping-pong buffer and atomically publishes it. Neither worker touches
// seq_lock or the audio thread, and neither does realtime work.
// ---------------------------------------------------------------------------

#define MEDIA_MAX_ENTRIES 8192
#define AUDITION_PEAKS   512

// Slightly larger Inter font for the Media Explorer so text reads better at
// the enlarged window size (the default UI small font is 10px bold).
static inline HFONT media_font(void) {
    static HFONT s_font = NULL;
    static int   s_lastPx = 0;
    int px = (int)(11.0f * scale_font(1.0f) + 0.5f);
    if (px < 8) px = 8;
    if (!s_font || px != s_lastPx) {
        if (s_font) DeleteObject(s_font);
        s_font = CreateFontA(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Inter");
        if (!s_font) {
            s_font = CreateFontA(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        }
        s_lastPx = px;
    }
    return s_font;
}

// Draw a UTF-8 string into a rect via DrawTextW (the app renders all user
// text as wide; DrawTextA would misinterpret non-ASCII bytes as ANSI).
static inline void media_draw_text_utf8(HDC hdc, const char* utf8, const RECT* rc, UINT flags) {
    if (!utf8 || !utf8[0]) return;
    wchar_t wbuf[MAX_PATH * 2];
    if (utf8_to_wide_buf(utf8, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))))
        DrawTextW(hdc, wbuf, -1, (RECT*)rc, flags);
}

// Custom messages posted from workers back to the panel (UI thread).
#define WM_APP_MEDIA_LIST    (WM_APP + 40)
#define WM_APP_MEDIA_PREVIEW (WM_APP + 41)

// One row in either pane. isDir selects the left folder pane; otherwise it is
// an audio file in the right pane.
typedef struct {
    char name[MAX_PATH];     // display name (UTF-8)
    char path[MAX_PATH];     // full path (UTF-8)
    bool isDir;
    bool isBookmark;         // quick-access bookmark (left pane header)
    // Audio metadata (right pane), populated on the worker thread.
    double durationSec;
    int    sampleRate;
    int    channels;
    char   format[8];
    bool   metaReady;
} MediaEntry;

// Forward declaration
static void media_scan_async(void);

static HWND             g_mediaHwnd      = NULL;
static CRITICAL_SECTION g_mediaListLock;
static MediaEntry       g_mediaEntries[MEDIA_MAX_ENTRIES];
static int              g_mediaCount     = 0;
static int              g_mediaDirCount  = 0;   // folders occupy [0, dirCount)
static int              g_mediaFileCount = 0;   // files occupy [dirCount, count)
static bool             g_mediaListInit  = false;

static char             g_mediaCurDir[MAX_PATH] = { 0 };
static int              g_mediaSelDir  = -1;    // selected folder row (left)
static int              g_mediaSelFile = -1;    // selected file row (right)
static int              g_mediaFileScroll = 0;  // right pane scroll offset
static int              g_mediaDirScroll  = 0;  // left pane scroll offset

// Right-pane column sort state (click a header to change field / direction).
typedef enum {
    MEDIA_SORT_NAME = 0,
    MEDIA_SORT_DURATION,
    MEDIA_SORT_RATE,
    MEDIA_SORT_CH,
    MEDIA_SORT_FMT
} MediaSortField;
static MediaSortField   g_mediaSortField = MEDIA_SORT_NAME;
static bool             g_mediaSortAsc    = true;

// Right-pane custom scrollbar (styled like scrollbar.h). This is a separate
// instance from the main timeline's g_sbState so the two never collide.
static RefractSbState   g_mediaSb = { 0 };

// Preview / audition state.
static volatile LONG    g_mediaPreviewGen  = 0; // bumped on each request
static volatile LONG    g_mediaPreviewBusy = 0; // single-flight worker gate
static float            g_mediaSpeed       = 1.0f;
static float            g_mediaVolume      = 0.75f;   // audition output level (default 75%)
static bool             g_mediaAutoPreview = true;
static bool             g_mediaRepeat      = false;
static bool             g_mediaPreviewReady = false;
static float            g_mediaPeaksMin[2][AUDITION_PEAKS];
static float            g_mediaPeaksMax[2][AUDITION_PEAKS];
static int              g_mediaPeaksLen[2]  = { 0, 0 };

// Drag-and-drop from the explorer onto the canvas.
static bool  g_mediaDragging = false;
static bool  g_mediaDragMoved = false;
static POINT g_mediaDragStart = { 0, 0 };
static POINT g_mediaDragLast  = { 0, 0 };   // last mouse pos during a knob drag
static int   g_mediaDragKnob  = 0;          // 0 = speed, 1 = volume

// Smooth playhead extrapolation: the audio thread publishes the position once
// per chunk (~23 ms); the UI repaints on a 33 ms timer. To avoid the scrub
// head jittering between reads, we advance a UI-side position at the playback
// speed and re-sync it to the published value on each read.
static double   g_mediaPlayPos     = 0.0;   // extrapolated position (frames)
static double   g_mediaPlayLastT   = 0.0;   // GetTickCount64 at last read
static bool     g_mediaPlayValid   = false;
static bool      g_mediaWaveDragging = false; // true while clicking/scrubbing waveform
static ULONGLONG g_mediaLastSeekT    = 0;     // timestamp of last seek (prevents rubber-banding)
// File drag (right pane → canvas): armed on a file click, promoted to an
// active drag once the mouse moves past the threshold.
static bool  g_mediaFileDragArmed = false;
static bool  g_mediaFileDragging  = false;
static int   g_mediaFileDragIdx   = -1;

// Set the current directory under the list lock so the scan worker never
// reads a half-written path.
static void media_set_cur_dir(const char* dir) {
    audition_stop();               // Mute preview when changing directories/drives
    g_mediaPlayPos = 0.0;
    g_mediaPlayValid = false;

    EnterCriticalSection(&g_mediaListLock);
    strncpy(g_mediaCurDir, dir, MAX_PATH - 1);
    g_mediaCurDir[MAX_PATH - 1] = '\0';
    LeaveCriticalSection(&g_mediaListLock);
}

// ---------------------------------------------------------------------------
// Supported audio extensions (spec section 6).
// ---------------------------------------------------------------------------
static inline bool media_ext_supported(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return false;
    static const char* kExts[] = { ".wav", ".aiff", ".aif", ".flac", ".mp3", ".ogg" };
    for (int i = 0; i < (int)(sizeof(kExts) / sizeof(kExts[0])); ++i) {
        if (_stricmp(dot, kExts[i]) == 0) return true;
    }
    return false;
}

// Format label for the Format column.
static inline const char* media_format_label(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "";
    if (_stricmp(dot, ".wav") == 0)  return "WAV";
    if (_stricmp(dot, ".aiff") == 0) return "AIFF";
    if (_stricmp(dot, ".aif") == 0)  return "AIFF";
    if (_stricmp(dot, ".flac") == 0) return "FLAC";
    if (_stricmp(dot, ".mp3") == 0)  return "MP3";
    if (_stricmp(dot, ".ogg") == 0)  return "OGG";
    return "";
}

// ---------------------------------------------------------------------------
// Navigate up one folder level. When at a drive root (e.g. "C:\"), backing up
// switches to the drives enumeration view ("").
// ---------------------------------------------------------------------------
static void media_navigate_up(void) {
    char up[MAX_PATH];
    EnterCriticalSection(&g_mediaListLock);
    strncpy(up, g_mediaCurDir, MAX_PATH - 1);
    up[MAX_PATH - 1] = '\0';
    LeaveCriticalSection(&g_mediaListLock);

    if (up[0] == '\0') return; // Already at Drives level

    // Normalize forward slashes
    for (char* p = up; *p; ++p) {
        if (*p == '/') *p = '\\';
    }

    size_t len = strlen(up);
    if (len > 3 && up[len - 1] == '\\') {
        up[len - 1] = '\0';
        len--;
    }

    // If at a drive root like "C:\" or "C:", back up to the Drives view ("")
    if (isalpha((unsigned char)up[0]) && up[1] == ':' && (len <= 3)) {
        up[0] = '\0';
    } else {
        char* slash = strrchr(up, '\\');
        if (slash) {
            if (slash == up + 2 && up[1] == ':') {
                *(slash + 1) = '\0'; // Retain "C:\" trailing slash
            } else if (slash == up) {
                *(slash + 1) = '\0';
            } else {
                *slash = '\0';
            }
        } else {
            up[0] = '\0';
        }
    }

    media_set_cur_dir(up);
    g_mediaSelDir = -1;
    g_mediaSelFile = -1;
    g_mediaFileScroll = 0;
    g_mediaDirScroll = 0;
    media_scan_async();
}

// ---------------------------------------------------------------------------
// Directory-scan worker: enumerates the current folder on a background thread,
// publishing the entry list incrementally so the UI never blocks.
// ---------------------------------------------------------------------------
static DWORD WINAPI media_scan_thread_proc(LPVOID param) {
    (void)param;
    char dir[MAX_PATH];
    EnterCriticalSection(&g_mediaListLock);
    strncpy(dir, g_mediaCurDir, MAX_PATH - 1);
    dir[MAX_PATH - 1] = '\0';
    LeaveCriticalSection(&g_mediaListLock);

    MediaEntry* staged = (MediaEntry*)malloc(sizeof(MediaEntry) * MEDIA_MAX_ENTRIES);
    if (!staged) return 0;
    int n = 0, dirCount = 0;

    // --- Drives level (This PC) ---
    if (dir[0] == '\0') {
        wchar_t driveBuf[512];
        DWORD dlen = GetLogicalDriveStringsW(sizeof(driveBuf) / sizeof(wchar_t), driveBuf);
        if (dlen > 0 && dlen < (sizeof(driveBuf) / sizeof(wchar_t))) {
            const wchar_t* p = driveBuf;
            while (*p && n < MEDIA_MAX_ENTRIES) {
                MediaEntry* e = &staged[n++];
                memset(e, 0, sizeof(*e));
                wide_to_utf8_buf(p, e->path, MAX_PATH);

                UINT dt = GetDriveTypeW(p);
                const char* typeName = "Drive";
                if (dt == DRIVE_FIXED)          typeName = "Local Disk";
                else if (dt == DRIVE_REMOVABLE) typeName = "Removable";
                else if (dt == DRIVE_CDROM)     typeName = "CD-ROM";
                else if (dt == DRIVE_REMOTE)    typeName = "Network";
                else if (dt == DRIVE_RAMDISK)   typeName = "RAM Disk";

                snprintf(e->name, sizeof(e->name), "%s (%s)", e->path, typeName);
                e->isDir = true;
                dirCount++;
                p += wcslen(p) + 1;
            }
        }
        EnterCriticalSection(&g_mediaListLock);
        memcpy(g_mediaEntries, staged, sizeof(MediaEntry) * (size_t)n);
        g_mediaCount = n;
        g_mediaDirCount = dirCount;
        g_mediaFileCount = 0;
        LeaveCriticalSection(&g_mediaListLock);

        free(staged);
        PostMessageA(g_mediaHwnd, WM_APP_MEDIA_LIST, 0, 0);
        return 0;
    }

    // --- Standard folder search ---
    char pattern[MAX_PATH + 4];
    snprintf(pattern, sizeof(pattern), "%s%s*", dir, (dir[strlen(dir) - 1] == '\\') ? "" : "\\");

    WIN32_FIND_DATAW fd;
    wchar_t wpattern[MAX_PATH + 4];
    utf8_to_wide_buf(pattern, wpattern, MAX_PATH + 4);
    HANDLE hFind = FindFirstFileW(wpattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        EnterCriticalSection(&g_mediaListLock);
        g_mediaCount = 0; g_mediaDirCount = 0; g_mediaFileCount = 0;
        LeaveCriticalSection(&g_mediaListLock);
        free(staged);
        PostMessageA(g_mediaHwnd, WM_APP_MEDIA_LIST, 0, 0);
        return 0;
    }

    // First pass: folders.
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (n >= MEDIA_MAX_ENTRIES) break;
            MediaEntry* e = &staged[n++];
            memset(e, 0, sizeof(*e));
            wide_to_utf8_buf(fd.cFileName, e->name, MAX_PATH);
            snprintf(e->path, MAX_PATH, "%s%s%s", dir, (dir[strlen(dir) - 1] == '\\') ? "" : "\\", e->name);
            e->isDir = true;
            dirCount++;
        }
    } while (FindNextFileW(hFind, &fd) != 0);
    FindClose(hFind);

    // Second pass: audio files (with metadata).
    hFind = FindFirstFileW(wpattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (n >= MEDIA_MAX_ENTRIES) break;
            char nameUtf8[MAX_PATH];
            wide_to_utf8_buf(fd.cFileName, nameUtf8, MAX_PATH);
            if (!media_ext_supported(nameUtf8)) continue;
            MediaEntry* e = &staged[n++];
            memset(e, 0, sizeof(*e));
            strncpy(e->name, nameUtf8, MAX_PATH - 1);
            snprintf(e->path, MAX_PATH, "%s%s%s", dir, (dir[strlen(dir) - 1] == '\\') ? "" : "\\", nameUtf8);
            e->isDir = false;
            strncpy(e->format, media_format_label(nameUtf8), sizeof(e->format) - 1);

            wchar_t wpath[MAX_PATH];
            utf8_to_wide_buf(e->path, wpath, MAX_PATH);
            ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, SAMPLE_RATE);
            ma_decoder dec;
            if (ma_decoder_init_file_w(wpath, &cfg, &dec) == MA_SUCCESS) {
                ma_uint64 frames = 0;
                if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) == MA_SUCCESS) {
                    e->durationSec = (double)frames / (double)SAMPLE_RATE;
                }
                e->sampleRate = (int)dec.outputSampleRate;
                e->channels   = (int)dec.outputChannels;
                e->metaReady  = true;
                ma_decoder_uninit(&dec);
            }
        } while (FindNextFileW(hFind, &fd) != 0);
        FindClose(hFind);
    }

    EnterCriticalSection(&g_mediaListLock);
    memcpy(g_mediaEntries, staged, sizeof(MediaEntry) * (size_t)n);
    g_mediaCount = n;
    g_mediaDirCount = dirCount;
    g_mediaFileCount = n - dirCount;
    LeaveCriticalSection(&g_mediaListLock);

    free(staged);
    PostMessageA(g_mediaHwnd, WM_APP_MEDIA_LIST, 0, 0);
    return 0;
}

// Kick off a background directory scan of the current folder.
static void media_scan_async(void) {
    CreateThread(NULL, 0, media_scan_thread_proc, NULL, 0, NULL);
}

// ---------------------------------------------------------------------------
// Right-pane sorting. Only the file sub-range [dirCount, count) is reordered;
// folders occupy [0, dirCount) and are never touched. Runs on the UI thread
// under the list lock after a scan publishes, or on a header click.
// ---------------------------------------------------------------------------
static int media_cmp_files(const void* a, const void* b) {
    const MediaEntry* ea = (const MediaEntry*)a;
    const MediaEntry* eb = (const MediaEntry*)b;
    int r = 0;
    switch (g_mediaSortField) {
        case MEDIA_SORT_DURATION:
            r = (ea->durationSec < eb->durationSec) ? -1 : (ea->durationSec > eb->durationSec) ? 1 : 0;
            break;
        case MEDIA_SORT_RATE:
            r = (ea->sampleRate < eb->sampleRate) ? -1 : (ea->sampleRate > eb->sampleRate) ? 1 : 0;
            break;
        case MEDIA_SORT_CH:
            r = (ea->channels < eb->channels) ? -1 : (ea->channels > eb->channels) ? 1 : 0;
            break;
        case MEDIA_SORT_FMT:
            r = _stricmp(ea->format, eb->format);
            break;
        case MEDIA_SORT_NAME:
        default:
            r = _stricmp(ea->name, eb->name);
            break;
    }
    return g_mediaSortAsc ? r : -r;
}

static void media_sort_entries(void) {
    EnterCriticalSection(&g_mediaListLock);
    if (g_mediaFileCount > 1) {
        qsort(g_mediaEntries + g_mediaDirCount, (size_t)g_mediaFileCount,
              sizeof(MediaEntry), media_cmp_files);
    }
    LeaveCriticalSection(&g_mediaListLock);
}

// ---------------------------------------------------------------------------
// Preview-decode worker: decodes the selected file into the audition voice's
// inactive ping-pong buffer, builds the waveform peaks, and publishes it.
// Single-flight (g_mediaPreviewBusy) with a generation check so stale decodes
// are discarded. Never writes the currently-published buffer.
// ---------------------------------------------------------------------------
static DWORD WINAPI media_preview_thread_proc(LPVOID param) {
    char path[MAX_PATH];
    strncpy(path, (const char*)param, MAX_PATH - 1);
    path[MAX_PATH - 1] = '\0';
    free(param);

    LONG gen = InterlockedCompareExchange(&g_mediaPreviewGen, 0, 0);

    wchar_t wpath[MAX_PATH];
    utf8_to_wide_buf(path, wpath, MAX_PATH);
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, SAMPLE_RATE);
    ma_decoder dec;
    if (ma_decoder_init_file_w(wpath, &cfg, &dec) != MA_SUCCESS) {
        InterlockedExchange(&g_mediaPreviewBusy, 0);
        return 0;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &totalFrames);
    if (totalFrames > AUDITION_MAX_FRAMES) totalFrames = AUDITION_MAX_FRAMES;

    // Choose the inactive ping-pong slot (the one the audio thread is not
    // currently reading).
    LONG active = InterlockedCompareExchange(&g_audState.activeIdx, 0, 0);
    int bufIdx = (active == 1) ? 0 : 1;
    float* buf = audition_get_write_buffer(bufIdx);

    ma_uint64 framesRead = 0;
    while (framesRead < totalFrames) {
        ma_uint32 want = (ma_uint32)(totalFrames - framesRead);
        if (want > 4096) want = 4096;
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&dec, buf + framesRead * 2, want, &got);
        if (got == 0) break;
        framesRead += got;
    }
    ma_decoder_uninit(&dec);
    if (framesRead == 0) {
        InterlockedExchange(&g_mediaPreviewBusy, 0);
        return 0;
    }

    // Build min/max peaks for the waveform strip.
    float* pmin = g_mediaPeaksMin[bufIdx];
    float* pmax = g_mediaPeaksMax[bufIdx];
    int peaks = AUDITION_PEAKS;
    for (int k = 0; k < peaks; ++k) {
        ma_uint64 s = (ma_uint64)framesRead * (ma_uint64)k / (ma_uint64)peaks;
        ma_uint64 e = (ma_uint64)framesRead * (ma_uint64)(k + 1) / (ma_uint64)peaks;
        if (e <= s) e = s + 1;
        if (e > framesRead) e = framesRead;
        float mn = 1.0f, mx = -1.0f;
        for (ma_uint64 f = s; f < e; ++f) {
            float mono = (buf[f * 2 + 0] + buf[f * 2 + 1]) * 0.5f;
            if (mono < mn) mn = mono;
            if (mono > mx) mx = mono;
        }
        pmin[k] = mn; pmax[k] = mx;
    }
    g_mediaPeaksLen[bufIdx] = peaks;

    // Discard if a newer request superseded this one.
    LONG curGen = InterlockedCompareExchange(&g_mediaPreviewGen, 0, 0);
    if (curGen == gen) {
        audition_play(bufIdx, (LONG)framesRead, gen);
        InterlockedExchange(&g_mediaPreviewBusy, 0);
        PostMessageA(g_mediaHwnd, WM_APP_MEDIA_PREVIEW, 0, 0);
    } else {
        InterlockedExchange(&g_mediaPreviewBusy, 0);
    }
    return 0;
}

// Queue a preview decode of `path`. Single-flight: if a decode is already
// running, just bump the generation so the in-flight result is discarded.
static void media_preview_async(const char* path) {
    LONG gen = InterlockedIncrement(&g_mediaPreviewGen);
    (void)gen;

    // Reset position state for the newly selected preview file
    g_mediaPlayPos = 0.0;
    g_mediaPlayValid = false;
    g_mediaLastSeekT = 0;

    // Keep the audition engine's repeat flag in sync with the UI toggle so a
    // preview started from any path (selection, Play, Space) honors Repeat.
    audition_set_repeat(g_mediaRepeat);
    if (InterlockedCompareExchange(&g_mediaPreviewBusy, 1, 0) != 0) {
        return; // worker in flight; its result will be discarded by gen bump
    }
    char* p = (char*)malloc(MAX_PATH);
    if (!p) { InterlockedExchange(&g_mediaPreviewBusy, 0); return; }
    strncpy(p, path, MAX_PATH - 1);
    p[MAX_PATH - 1] = '\0';
    CreateThread(NULL, 0, media_preview_thread_proc, p, 0, NULL);
}

// ---------------------------------------------------------------------------
// Import the selected file onto the canvas at the edit cursor (Enter / Add to
// Canvas). Uses the existing, tested load + add_clip path (content-address
// cache + dedup), then applies the current preview speed to the clip's rate.
// ---------------------------------------------------------------------------
static void media_import_to_canvas(void) {
    if (g_mediaSelFile < 0 || g_mediaSelFile >= g_mediaCount) return;
    if (g_mediaSelFile < g_mediaDirCount) return; // folders can't be imported
    MediaEntry* e = &g_mediaEntries[g_mediaSelFile];
    if (g_Seq.isBusy) return;

    // Place on the most recently clicked timeline track (defaults to track 0).
    int track = g_Seq.lastClickedTrack;
    if (track < 0 || track >= g_Seq.trackCount) track = 0;

    LONG pf = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
    float startBeat = frame_to_beat((ma_uint64)(pf < 0 ? 0 : pf), g_Seq.bpm, g_Seq.swing);
    float bpb = beats_per_bar();
    startBeat = (float)floor(startBeat / bpb + 0.5f) * bpb;   // snap to bar

    int sampleIdx = load_audio_file(e->path);
    if (sampleIdx < 0) return;

    // Length of the clip about to be added, so we can snap it past any clip it
    // would overlap on the target track. This mirrors the paste-on-clip logic:
    // repeated imports land at the rightmost extremity instead of stacking on
    // top of each other.
    AudioSample *s = &g_Seq.samples[sampleIdx];
    float fpb = frames_per_beat(g_Seq.bpm);
    float minLen = get_min_clip_length_beats();
    float lengthBeats = (float)s->frameCount / fpb;
    if (lengthBeats < minLen) lengthBeats = minLen;

    float targetBeat = startBeat;
    bool adjusted = true;
    int iter = 0;
    while (adjusted && iter++ < 64) {
        adjusted = false;
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            Clip *ex = &g_Seq.clips[i];
            if (ex->track != track) continue;
            float exStart = ex->startBeat;
            float exEnd = ex->startBeat + ex->lengthBeats;
            float pasteEnd = targetBeat + lengthBeats;
            if (targetBeat < exEnd - 0.001f && pasteEnd > exStart + 0.001f) {
                // Snap flush against the overlapping clip's end. Deliberately
                // NOT quantized: rounding up to the 16th-note grid would leave a
                // visible gap between stacked imports, so keep them contiguous.
                float nextBeat = exEnd;
                if (nextBeat > targetBeat) {
                    targetBeat = nextBeat;
                    adjusted = true;
                    break;
                }
            }
        }
    }

    int idx = add_clip(sampleIdx, track, targetBeat);
    if (idx >= 0) {
        seq_lock();
        g_Seq.clips[idx].playbackRate = g_mediaSpeed;
        seq_unlock();
    }
    // Stop the looping audition copy so it doesn't keep playing the same file
    // underneath the newly added clip — that overlap made the clip's fade
    // envelope sound broken (the unfaded audition bled over the fade).
    audition_stop();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

// Import a file at an explicit timeline point (drag-and-drop). Mirrors the
// WM_DROPFILES handler in events.h.
static void media_import_at_point(const char* path, int mx, int my) {
    if (g_Seq.isBusy) return;
    int track = (my - get_header_height() + g_Seq.scrollY) / get_track_height();
    if (track < 0) track = 0;
    if (track >= g_Seq.trackCount) track = g_Seq.trackCount - 1;
    float ppb = get_pixels_per_beat();
    float dropBeat = (float)(mx - get_track_header_width() + g_Seq.scrollX) / ppb;
    if (dropBeat < 0.0f) dropBeat = 0.0f;
    dropBeat = quantize_beat_16th(dropBeat);

    int sampleIdx = load_audio_file(path);
    if (sampleIdx < 0) return;
    int idx = add_clip(sampleIdx, track, dropBeat);
    if (idx >= 0) {
        seq_lock();
        g_Seq.clips[idx].playbackRate = g_mediaSpeed;
        seq_unlock();
    }
    // Stop the looping audition copy so it doesn't mask the clip's fades.
    audition_stop();
    if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Layout helpers.
// ---------------------------------------------------------------------------
typedef struct {
    RECT leftPane, rightPane, bottomPane;
    int  rowH;
    int  leftTop, leftVisible;
    int  rightTop, rightVisible;
    int  bottomTop;
    RECT playBtn, addBtn, autoChk;
    RECT repeatChk;
    RECT speedCell;
    RECT volCell;
    RECT waveform;
} MediaLayout;

static inline void media_compute_layout(const RECT* rc, MediaLayout* L) {
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    int headerH = scale_y(30);
    int margin = scale_x(8);
    int paneGap = scale_x(6);
    int leftW = (int)((w - margin * 2 - paneGap) * 0.36f);
    int rightX = margin + leftW + paneGap;
    int rightW = w - margin - rightX;
    int bottomH = scale_y(96);
    int panesBottom = h - headerH - bottomH - margin;

    L->rowH = scale_y(24);
    L->leftPane   = (RECT){ margin, headerH, margin + leftW, panesBottom };
    L->rightPane  = (RECT){ rightX, headerH, rightX + rightW, panesBottom };
    L->bottomPane = (RECT){ margin, panesBottom + scale_y(4), w - margin, h - margin };
    L->leftTop    = L->leftPane.top + 2;
    L->rightTop   = L->rightPane.top + 2;
    L->leftVisible  = (L->leftPane.bottom - L->leftPane.top - 4) / L->rowH;
    L->rightVisible = (L->rightPane.bottom - L->rightPane.top - 4) / L->rowH;
    L->bottomTop = L->bottomPane.top;

    // Bottom bar widgets.
    int bw = L->bottomPane.bottom - L->bottomPane.top;
    int by = L->bottomPane.top + scale_y(8);
    L->playBtn = (RECT){ L->bottomPane.left + scale_x(10), by,
                         L->bottomPane.left + scale_x(10) + scale_x(64), by + scale_y(24) };
    // Import button sits immediately right of Play, same size.
    L->addBtn = (RECT){ L->playBtn.right + scale_x(8), by,
                        L->playBtn.right + scale_x(8) + scale_x(64), by + scale_y(24) };
    L->speedCell = (RECT){ L->addBtn.right + scale_x(16), L->bottomPane.top,
                           L->addBtn.right + scale_x(16) + scale_x(56), L->bottomPane.bottom };
    L->volCell = (RECT){ L->speedCell.right + scale_x(8), L->bottomPane.top,
                         L->speedCell.right + scale_x(8) + scale_x(56), L->bottomPane.bottom };
    L->autoChk = (RECT){ L->volCell.right + scale_x(12), by,
                         L->volCell.right + scale_x(12) + scale_x(90), by + scale_y(22) };
    L->repeatChk = (RECT){ L->autoChk.right + scale_x(4), by,
                           L->autoChk.right + scale_x(4) + scale_x(85), by + scale_y(22) };
    L->waveform = (RECT){ L->bottomPane.left + scale_x(10), L->bottomPane.top + scale_y(38),
                          L->bottomPane.right - scale_x(10), L->bottomPane.bottom - scale_y(8) };
    (void)bw;
}

// ---------------------------------------------------------------------------
// Right-pane custom scrollbar. The scroll value lives in g_mediaFileScroll
// (in rows); the scrollbar works in pixels, so we convert on the way in/out.
// ---------------------------------------------------------------------------
static void media_update_sb(const RECT* rc) {
    MediaLayout L;
    media_compute_layout(rc, &L);
    int contentPx = g_mediaFileCount * L.rowH;
    int visiblePx = (L.rightPane.bottom - L.rightPane.top) - scale_y(20);
    if (visiblePx < 0) visiblePx = 0;
    g_mediaSb.totalContent = contentPx;
    g_mediaSb.visibleHeight = visiblePx;
    g_mediaSb.scrollPos = g_mediaFileScroll * L.rowH;
    g_mediaSb.visible = contentPx > visiblePx;
    int maxScroll = g_mediaFileCount - L.rightVisible;
    if (maxScroll < 0) maxScroll = 0;
    if (g_mediaFileScroll > maxScroll) g_mediaFileScroll = maxScroll;
    if (g_mediaFileScroll < 0) g_mediaFileScroll = 0;
}

// Pane-specific geometry: the scrollbar hugs the right edge of the right pane,
// spanning the file rows (below the header band).
static bool media_sb_get_geom(const RECT* rc, RefractSbGeom* geo) {
    memset(geo, 0, sizeof(*geo));
    MediaLayout L;
    media_compute_layout(rc, &L);
    int trackTop = L.rightPane.top + scale_y(20);
    int trackBottom = L.rightPane.bottom;
    int trackLen = trackBottom - trackTop;
    if (trackLen < cseq_sb_width()) return false;

    geo->visible = g_mediaSb.visible;
    geo->scrollMin = 0;
    geo->scrollRange = g_mediaSb.totalContent;
    geo->pagePx = g_mediaSb.visibleHeight;
    geo->left = L.rightPane.right - cseq_sb_width();
    geo->right = L.rightPane.right;
    geo->trackTop = trackTop;
    geo->trackBottom = trackBottom;

    int total = g_mediaSb.totalContent;
    int visible = g_mediaSb.visibleHeight;
    if (total <= 0) return false;

    int thumbLen = (total > visible)
                   ? (int)((float)trackLen * ((float)visible / (float)total))
                   : trackLen;
    if (thumbLen < scale_y(28)) thumbLen = scale_y(28);
    if (thumbLen > trackLen) thumbLen = trackLen;

    float frac = (total > visible)
                 ? (float)(g_mediaFileScroll * L.rowH) / (float)(total - visible)
                 : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    int thumbTop = trackTop + (int)(frac * (float)(trackLen - thumbLen));
    geo->thumbTop = thumbTop;
    geo->thumbBottom = thumbTop + thumbLen;
    return true;
}

static void media_sb_draw(HDC hdc, const RECT* rc) {
    if (!g_mediaSb.visible) return;
    RefractSbGeom geo;
    if (!media_sb_get_geom(rc, &geo) || !geo.visible) return;

    RECT trackRt = { geo.left, geo.trackTop, geo.right, geo.trackBottom };
    HBRUSH trackBrush = CreateSolidBrush(RGB(28, 33, 42));
    FillRect(hdc, &trackRt, trackBrush);
    DeleteObject(trackBrush);

    bool hot = (g_mediaSb.dragging || g_mediaSb.hoverThumb);
    COLORREF thumbCol = hot ? RGB(80, 100, 120) : RGB(60, 75, 90);
    int marginX = scale_x(2);
    int thumbX = geo.left + marginX;
    int thumbW = (geo.right - geo.left) - marginX * 2;
    int thumbY = geo.thumbTop;
    int thumbH = geo.thumbBottom - geo.thumbTop;
    if (thumbW > 0 && thumbH > 0) {
        float radius = (float)thumbW * 0.5f;
        cseq_sb_draw_aa_thumb(hdc, thumbX, thumbY, thumbW, thumbH, radius, thumbCol);
    }
}

static void media_sb_commit(HWND hwnd, const RECT* rc) {
    MediaLayout L;
    media_compute_layout(rc, &L);
    int maxScroll = g_mediaFileCount - L.rightVisible;
    if (maxScroll < 0) maxScroll = 0;
    if (g_mediaFileScroll > maxScroll) g_mediaFileScroll = maxScroll;
    if (g_mediaFileScroll < 0) g_mediaFileScroll = 0;
    InvalidateRect(hwnd, NULL, FALSE);
}

// ---------------------------------------------------------------------------
// Seek the audition voice to the frame under the mouse and latch the UI
// playhead there. Also records the seek timestamp so the draw pass won't
// immediately re-anchor to a stale audio-thread frame (rubber-banding).
// ---------------------------------------------------------------------------
static void media_seek_to_mouse(int mx, const RECT* waveRect) {
    LONG total = InterlockedCompareExchange(&g_audState.totalFrames, 0, 0);
    if (total <= 0) return;

    int ww = waveRect->right - waveRect->left;
    if (ww <= 0) return;

    int dx = mx - waveRect->left;
    if (dx < 0) dx = 0;
    if (dx > ww) dx = ww;

    LONG frame = (LONG)((double)dx * (double)total / (double)ww);
    if (frame >= total) frame = total - 1;
    audition_seek(frame);

    // Latch UI playhead and set seek timestamp
    g_mediaPlayPos = (double)frame;
    g_mediaPlayLastT = (double)GetTickCount64();
    g_mediaLastSeekT = GetTickCount64();
    g_mediaPlayValid = true;
}

// ---------------------------------------------------------------------------
// Waveform strip rendering (min/max peaks + scrub head).
// ---------------------------------------------------------------------------
static inline void media_draw_waveform(HDC memDC, const MediaLayout* L) {
    RECT W = L->waveform;
    int w = W.right - W.left, h = W.bottom - W.top;
    if (w < 4 || h < 4) return;
    int midY = (W.top + W.bottom) / 2;
    int amp = (h / 2) - 2;
    if (amp < 2) amp = 2;

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(28, 33, 42));
    HGDIOBJ oldP = SelectObject(memDC, gridPen);
    MoveToEx(memDC, W.left, midY, NULL);
    LineTo(memDC, W.right, midY);
    SelectObject(memDC, oldP);
    DeleteObject(gridPen);

    int active = InterlockedCompareExchange(&g_audState.activeIdx, 0, 0);
    int peaks = g_mediaPeaksLen[active & 1];
    if (peaks <= 0) {
        SetTextColor(memDC, RGB(95, 108, 126));
        HFONT oldF = (HFONT)SelectObject(memDC, media_font());
        RECT tr = W;
        DrawTextA(memDC, "Select a file to preview", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
        return;
    }

    const float* pmin = g_mediaPeaksMin[active & 1];
    const float* pmax = g_mediaPeaksMax[active & 1];

    // Playhead position: everything to its left is the "played" area and gets
    // a subtle green tint for position feedback; the rest stays muted grey.
    LONG total = InterlockedCompareExchange(&g_audState.totalFrames, 0, 0);
    double rf = 0.0;
    if (total > 0 && audition_is_playing()) {
        // Smoothly extrapolate the playhead between audio-thread publishes so
        // the scrub head advances continuously instead of jittering. The audio
        // thread publishes the position once per ~23 ms chunk; between reads we
        // advance at the playback speed from the last known anchor.
        ULONGLONG now = GetTickCount64();
        LONG pub = audition_get_read_frame();
        if (pub < 0) pub = 0;

        if (g_mediaPlayValid) {
            double dtMs = (double)(now - g_mediaPlayLastT);
            g_mediaPlayPos += g_mediaSpeed * (double)SAMPLE_RATE * (dtMs / 1000.0);

            // Re-anchor if the audio thread looped back to 0, if a new file
            // loaded, or if it drifted significantly in either direction.
            // A short grace period after a seek keeps a stale audio-thread
            // frame from snapping the playhead back to the pre-seek position.
            if (now - g_mediaLastSeekT > 120) {
                if (pub < (LONG)(g_mediaPlayPos - 2048.0) || pub > (LONG)(g_mediaPlayPos + 2048.0)) {
                    g_mediaPlayPos = (double)pub;
                }
            }
        } else {
            g_mediaPlayPos = (double)pub;
            g_mediaPlayValid = true;
        }
        g_mediaPlayLastT = (double)now;
        rf = g_mediaPlayPos;
        if (rf > (double)total) rf = (double)total;
        if (rf < 0.0) rf = 0.0;
    } else {
        // When stopped, keep playhead at the last seeked frame instead of resetting to 0
        g_mediaPlayValid = false;
        rf = g_mediaPlayPos;
        if (rf > (double)total) rf = (double)total;
        if (rf < 0.0) rf = 0.0;
    }

    int sx = W.left + (int)(rf * (double)w / (double)(total > 0 ? total : 1));
    if (sx < W.left) sx = W.left;
    if (sx > W.right - 1) sx = W.right - 1;

    HPEN wavePen = CreatePen(PS_SOLID, 1, RGB(92, 102, 116));          // unplayed
    HPEN playedPen = CreatePen(PS_SOLID, 1, RGB(48, 110, 84));         // low-opacity green (same hue as buttons)
    oldP = SelectObject(memDC, wavePen);
    int prevY0 = 0, prevY1 = 0;
    for (int x = 0; x < w; ++x) {
        int idx = (int)((long long)x * peaks / w);
        if (idx >= peaks) idx = peaks - 1;
        int y0 = midY - (int)(pmax[idx] * amp);
        int y1 = midY - (int)(pmin[idx] * amp);
        if (y0 < W.top) y0 = W.top;
        if (y1 > W.bottom) y1 = W.bottom;
        if (y1 < y0) y1 = y0;
        // Switch to the green pen for columns left of the playhead.
        int px = W.left + x;
        if (px <= sx && total > 0 && rf > 0.0) {
            if (GetCurrentObject(memDC, OBJ_PEN) != playedPen) {
                SelectObject(memDC, playedPen);
            }
        } else {
            if (GetCurrentObject(memDC, OBJ_PEN) != wavePen) {
                SelectObject(memDC, wavePen);
            }
        }
        if (x > 0 && abs(y0 - prevY0) > 1) { MoveToEx(memDC, W.left + x - 1, prevY0, NULL); LineTo(memDC, W.left + x, y0); }
        if (y1 > y0) { MoveToEx(memDC, W.left + x, y0, NULL); LineTo(memDC, W.left + x, y1); }
        prevY0 = y0; prevY1 = y1;
    }
    (void)prevY1;
    SelectObject(memDC, oldP);
    DeleteObject(wavePen);
    DeleteObject(playedPen);

    // Scrub head.
    if (total > 0) {
        HPEN scrubPen = CreatePen(PS_SOLID, 1, RGB(120, 235, 255));
        oldP = SelectObject(memDC, scrubPen);
        MoveToEx(memDC, sx, W.top, NULL);
        LineTo(memDC, sx, W.bottom);
        SelectObject(memDC, oldP);
        DeleteObject(scrubPen);
    }
}

// ---------------------------------------------------------------------------
// Panel painting.
// ---------------------------------------------------------------------------
// Draw a labelled checkbox (box + optional checkmark + label). The box is
// supersampled 3x into a DIB and HALFTONE-stretched down so its rounded
// corners and checkmark have no visible jaggies (same technique as the knobs).
static void media_draw_checkbox(HDC memDC, const RECT* rc, bool on, const char* label) {
    int boxPx = scale_y(16);
    int ss = 3;
    int ss_dim = boxPx * ss;

    HDC d = CreateCompatibleDC(memDC);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ss_dim;
    bmi.bmiHeader.biHeight = -ss_dim;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HBITMAP hBmp = CreateDIBSection(d, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
    if (hBmp) {
        HGDIOBJ oldBmp = SelectObject(d, hBmp);
        SetBkMode(d, TRANSPARENT);

        // Box fill + border at 3x resolution.
        HBRUSH fillBr = CreateSolidBrush(on ? RGB(25, 50, 45) : RGB(30, 32, 38));
        HPEN borderPen = CreatePen(PS_SOLID, ss, on ? RGB(80, 240, 180) : RGB(55, 60, 72));
        HGDIOBJ ob = SelectObject(d, fillBr);
        HGDIOBJ op = SelectObject(d, borderPen);
        RoundRect(d, 0, 0, ss_dim, ss_dim, 2 * ss, 2 * ss);
        SelectObject(d, op);
        SelectObject(d, ob);
        DeleteObject(borderPen);
        DeleteObject(fillBr);

        // Checkmark stroke at 3x resolution.
        if (on) {
            HPEN chkPen = CreatePen(PS_SOLID, ss, RGB(80, 240, 180));
            HGDIOBJ ocp = SelectObject(d, chkPen);
            MoveToEx(d, 3 * ss, 8 * ss, NULL);
            LineTo(d, 7 * ss, 12 * ss);
            LineTo(d, 13 * ss, 3 * ss);
            SelectObject(d, ocp);
            DeleteObject(chkPen);
        }

        // Stretch down with HALFTONE for smooth AA while the DIB is still
        // selected into d. Restoring the bitmap before StretchBlt would make
        // it read the 1x1 default bitmap and blank the checkbox.
        SetStretchBltMode(memDC, HALFTONE);
        SetBrushOrgEx(memDC, 0, 0, NULL);
        StretchBlt(memDC, rc->left, rc->top, boxPx, boxPx, d, 0, 0, ss_dim, ss_dim, SRCCOPY);
        SelectObject(d, oldBmp);
        DeleteObject(hBmp);
    }
    DeleteDC(d);

    SetTextColor(memDC, RGB(150, 165, 185));
    HFONT oldF = (HFONT)SelectObject(memDC, media_font());
    RECT lbl = { rc->left + boxPx + scale_x(6), rc->top, rc->right, rc->bottom };
    DrawTextA(memDC, label, -1, &lbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(memDC, oldF);
}

static void media_paint(HDC hdc, const RECT* rc) {
    int w = rc->right - rc->left, h = rc->bottom - rc->top;
    if (w <= 0 || h <= 0) return;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
    HFONT oldFont = SELECT_UI_FONT(memDC);
    HGDIOBJ origPen   = GetCurrentObject(memDC, OBJ_PEN);
    HGDIOBJ origBrush = GetCurrentObject(memDC, OBJ_BRUSH);

    HBRUSH bg = CreateSolidBrush(RGB(17, 20, 26));
    FillRect(memDC, rc, bg);
    DeleteObject(bg);
    SetBkMode(memDC, TRANSPARENT);

    MediaLayout L;
    media_compute_layout(rc, &L);

    // Header / title.
    SetTextColor(memDC, RGB(120, 135, 155));
    HFONT smallF = media_font();
    HFONT oldSmall = (HFONT)SelectObject(memDC, smallF);
    RECT titleRc = { scale_x(8), 0, w, scale_y(30) };
    DrawTextA(memDC, "Directory Browser", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    // Current path (breadcrumb) in the title band.
    SetTextColor(memDC, RGB(95, 108, 126));
    RECT pathRc = { scale_x(150), 0, w - scale_x(8), scale_y(30) };
    media_draw_text_utf8(memDC, g_mediaCurDir[0] ? g_mediaCurDir : "Quick Access", &pathRc,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Pane backgrounds + borders.
    HBRUSH paneBr = CreateSolidBrush(RGB(13, 16, 21));
    HPEN panePen = CreatePen(PS_SOLID, 1, RGB(40, 48, 60));
    SelectObject(memDC, paneBr);
    SelectObject(memDC, panePen);
    Rectangle(memDC, L.leftPane.left, L.leftPane.top, L.leftPane.right, L.leftPane.bottom);
    Rectangle(memDC, L.rightPane.left, L.rightPane.top, L.rightPane.right, L.rightPane.bottom);
    Rectangle(memDC, L.bottomPane.left, L.bottomPane.top, L.bottomPane.right, L.bottomPane.bottom);
    // Restore the original pen/brush before deleting the created ones, or the
    // DeleteObject fails and the object leaks on every repaint.
    SelectObject(memDC, origPen);
    SelectObject(memDC, origBrush);
    DeleteObject(panePen);
    DeleteObject(paneBr);

    // Column headers for the right pane.
    SetTextColor(memDC, RGB(120, 135, 155));
    int sbW = cseq_sb_width();
    int colName = L.rightPane.left + scale_x(10);
    int colDur  = L.rightPane.right - sbW - scale_x(220);
    int colRate = L.rightPane.right - sbW - scale_x(150);
    int colCh   = L.rightPane.right - sbW - scale_x(100);
    int colFmt  = L.rightPane.right - sbW - scale_x(44);
    RECT hdrRc = { colName, L.rightPane.top + 2, colDur, L.rightPane.top + scale_y(18) };
    DrawTextA(memDC, "NAME", -1, &hdrRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT h2 = { colDur, L.rightPane.top + 2, colRate, L.rightPane.top + scale_y(18) };
    DrawTextA(memDC, "DURATION", -1, &h2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT h3 = { colRate, L.rightPane.top + 2, colCh, L.rightPane.top + scale_y(18) };
    DrawTextA(memDC, "RATE", -1, &h3, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT h4 = { colCh, L.rightPane.top + 2, colFmt, L.rightPane.top + scale_y(18) };
    DrawTextA(memDC, "CH", -1, &h4, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT h5 = { colFmt, L.rightPane.top + 2, L.rightPane.right - sbW - scale_x(6), L.rightPane.top + scale_y(18) };
    DrawTextA(memDC, "FMT", -1, &h5, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Sort indicator on the active column: a small AA triangle (apex up for
    // ascending, down for descending) drawn on the side of the column away
    // from the header text so it never overlaps it.
    {
        int cx = 0;
        int topY = L.rightPane.top + scale_y(10);
        int halfW = scale_x(3);
        int height = scale_y(5);
        switch (g_mediaSortField) {
            case MEDIA_SORT_NAME:
                // Left-aligned text: put the caret at the column's right edge.
                cx = colDur - scale_x(6);
                break;
            case MEDIA_SORT_DURATION: cx = colDur + scale_x(8); break;
            case MEDIA_SORT_RATE:     cx = colRate + scale_x(8); break;
            case MEDIA_SORT_CH:       cx = colCh + scale_x(8); break;
            case MEDIA_SORT_FMT:      cx = colFmt - scale_x(12); break;
        }
        if (cx > 0) {
            draw_aa_triangle(memDC, cx, topY, halfW, height, RGB(80, 210, 240), g_mediaSortAsc);
        }
    }

    // Snapshot the entry list under the lock.
    int count, dirCount;
    EnterCriticalSection(&g_mediaListLock);
    count = g_mediaCount; dirCount = g_mediaDirCount;
    MediaEntry* entries = g_mediaEntries;
    LeaveCriticalSection(&g_mediaListLock);

    // Left pane: pure directory browser (parent row + folders).
    {
        int row = 0;
        bool hasUp = (g_mediaCurDir[0] != '\0');
        if (hasUp) {
            RECT upRc = { L.leftPane.left + 2, L.leftPane.top + 2 + row * L.rowH,
                          L.leftPane.right - 2, L.leftPane.top + 2 + (row + 1) * L.rowH };
            HBRUSH upBr = CreateSolidBrush(g_mediaSelDir == -2 ? RGB(26, 44, 54) : RGB(19, 24, 31));
            FillRect(memDC, &upRc, upBr);
            DeleteObject(upBr);
            SetTextColor(memDC, RGB(170, 190, 210));
            TextOutA(memDC, upRc.left + scale_x(8), upRc.top + scale_y(3), "[..] Up", 7);
        }
        int upOffset = hasUp ? 1 : 0;
        for (int i = 0; i < dirCount; ++i) {
            int visRow = i + upOffset - g_mediaDirScroll;
            if (visRow < upOffset) continue; // folders are only rendered at visRow >= 1, leaving visual row 0 visible as [..] Up
            if (visRow >= L.leftVisible) break;
            MediaEntry* e = &entries[i];
            RECT rRc = { L.leftPane.left + 2, L.leftPane.top + 2 + visRow * L.rowH,
                         L.leftPane.right - 2, L.leftPane.top + 2 + (visRow + 1) * L.rowH };
            bool sel = (i == g_mediaSelDir);
            HBRUSH rBr = CreateSolidBrush(sel ? RGB(26, 44, 54) : RGB(19, 24, 31));
            FillRect(memDC, &rRc, rBr);
            DeleteObject(rBr);
            SetTextColor(memDC, sel ? RGB(120, 235, 255) : RGB(170, 190, 210));
            char disp[MAX_PATH + 4];
            snprintf(disp, sizeof(disp), "> %s", e->name);
            RECT dRc = { rRc.left + scale_x(8), rRc.top, rRc.right - scale_x(8), rRc.bottom };
            media_draw_text_utf8(memDC, disp, &dRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    // Right pane: audio files.
    {
        for (int i = g_mediaDirCount; i < count; ++i) {
            int vis = i - g_mediaDirCount - g_mediaFileScroll;
            if (vis < 0) continue;
            if (vis >= L.rightVisible) break;
            MediaEntry* e = &entries[i];
            RECT rRc = { L.rightPane.left + 2, L.rightPane.top + scale_y(20) + vis * L.rowH,
                         L.rightPane.right - 2, L.rightPane.top + scale_y(20) + (vis + 1) * L.rowH };
            if (rRc.bottom > L.rightPane.bottom) break;
            bool sel = (i == g_mediaSelFile);
            HBRUSH rBr = CreateSolidBrush(sel ? RGB(26, 44, 54) : RGB(19, 24, 31));
            FillRect(memDC, &rRc, rBr);
            DeleteObject(rBr);
            HPEN rowPen = CreatePen(PS_SOLID, 1, sel ? RGB(80, 210, 240) : RGB(36, 44, 56));
            HPEN oldPen = (HPEN)SelectObject(memDC, rowPen);
            HBRUSH nullBr = (HBRUSH)GetStockObject(NULL_BRUSH);
            HBRUSH oldBr = (HBRUSH)SelectObject(memDC, nullBr);
            Rectangle(memDC, rRc.left, rRc.top, rRc.right, rRc.bottom);
            SelectObject(memDC, oldBr);
            DeleteObject(SelectObject(memDC, oldPen));

            SetTextColor(memDC, sel ? RGB(120, 235, 255) : RGB(180, 195, 215));
            RECT nmRc = { colName, rRc.top, colDur - scale_x(6), rRc.bottom };
            media_draw_text_utf8(memDC, e->name, &nmRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            char buf[64];
            SetTextColor(memDC, RGB(130, 145, 165));
            if (e->metaReady) {
                int mins = (int)(e->durationSec / 60.0);
                int secs = (int)e->durationSec % 60;
                snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
                RECT r1 = { colDur, rRc.top, colRate, rRc.bottom };
                DrawTextA(memDC, buf, -1, &r1, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                snprintf(buf, sizeof(buf), "%d", e->sampleRate);
                RECT r2 = { colRate, rRc.top, colCh, rRc.bottom };
                DrawTextA(memDC, buf, -1, &r2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                snprintf(buf, sizeof(buf), "%d", e->channels);
                RECT r3 = { colCh, rRc.top, colFmt, rRc.bottom };
                DrawTextA(memDC, buf, -1, &r3, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT r4 = { colFmt, rRc.top, L.rightPane.right - sbW - scale_x(6), rRc.bottom };
                DrawTextA(memDC, e->format, -1, &r4, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
        // Custom scrollbar on the right edge of the file pane.
        media_sb_draw(memDC, rc);
    }

    // Bottom bar: waveform + controls.
    media_draw_waveform(memDC, &L);

    // Play/Pause button.
    {
        bool playing = audition_is_playing();
        HBRUSH pb = CreateSolidBrush(playing ? RGB(25, 50, 45) : RGB(30, 32, 38));
        HPEN pp = CreatePen(PS_SOLID, 1, playing ? RGB(80, 240, 180) : RGB(55, 60, 72));
        SelectObject(memDC, pb);
        SelectObject(memDC, pp);
        RoundRect(memDC, L.playBtn.left, L.playBtn.top, L.playBtn.right, L.playBtn.bottom, 3, 3);
        DeleteObject(SelectObject(memDC, GetStockObject(NULL_BRUSH)));
        DeleteObject(SelectObject(memDC, GetStockObject(NULL_PEN)));
        SetTextColor(memDC, playing ? RGB(80, 240, 180) : RGB(130, 140, 155));
        HFONT oldF = (HFONT)SelectObject(memDC, smallF);
        DrawTextA(memDC, playing ? "PAUSE" : "PLAY", -1, &L.playBtn,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
    }

    // Speed knob.
    {
        SetTextColor(memDC, RGB(120, 135, 155));
        HFONT oldF = (HFONT)SelectObject(memDC, smallF);
        RECT lbl = { L.speedCell.left, L.bottomPane.top + scale_y(2), L.speedCell.right, L.bottomPane.top + scale_y(16) };
        char sbuf[16];
        snprintf(sbuf, sizeof(sbuf), "%.2fx", g_mediaSpeed);
        DrawTextA(memDC, sbuf, -1, &lbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        fx_draw_aa_knob(memDC, (L.speedCell.left + L.speedCell.right) / 2,
                        L.bottomPane.top + scale_y(30), (float)scale_y(14),
                        (g_mediaSpeed - 0.5f) / 1.5f);
        SelectObject(memDC, oldF);
    }

    // Output level knob (no "SPEED"/"VOL" caption text under the dials).
    {
        SetTextColor(memDC, RGB(120, 135, 155));
        HFONT oldF = (HFONT)SelectObject(memDC, smallF);
        RECT lbl = { L.volCell.left, L.bottomPane.top + scale_y(2), L.volCell.right, L.bottomPane.top + scale_y(16) };
        char vbuf[16];
        snprintf(vbuf, sizeof(vbuf), "%d%%", (int)(g_mediaVolume * 100.0f + 0.5f));
        DrawTextA(memDC, vbuf, -1, &lbl, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        fx_draw_aa_knob(memDC, (L.volCell.left + L.volCell.right) / 2,
                        L.bottomPane.top + scale_y(30), (float)scale_y(14),
                        g_mediaVolume);
        SelectObject(memDC, oldF);
    }

    // Preview + Repeat checkboxes.
    media_draw_checkbox(memDC, &L.autoChk, g_mediaAutoPreview, "Preview");
    media_draw_checkbox(memDC, &L.repeatChk, g_mediaRepeat, "Repeat");

    // Add to Canvas button.
    {
        HBRUSH ab = CreateSolidBrush(RGB(22, 90, 55));
        HPEN ap = CreatePen(PS_SOLID, 1, RGB(80, 240, 180));
        SelectObject(memDC, ab);
        SelectObject(memDC, ap);
        RoundRect(memDC, L.addBtn.left, L.addBtn.top, L.addBtn.right, L.addBtn.bottom, 4, 4);
        DeleteObject(SelectObject(memDC, GetStockObject(NULL_BRUSH)));
        DeleteObject(SelectObject(memDC, GetStockObject(NULL_PEN)));
        SetTextColor(memDC, RGB(160, 255, 205));
        HFONT oldF = (HFONT)SelectObject(memDC, smallF);
        DrawTextA(memDC, "IMPORT", -1, &L.addBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
    }

    SelectObject(memDC, oldSmall);
    SelectObject(memDC, oldFont);
    SelectObject(memDC, origBrush);
    SelectObject(memDC, origPen);

    // Composite the double-buffered panel to the window DC. Without this the
    // window would stay blank (everything was drawn into the memory DC only).
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

// ---------------------------------------------------------------------------
// Selection change → (re)queue preview.
// ---------------------------------------------------------------------------
static void media_on_selection_change(void) {
    if (g_mediaSelFile < g_mediaDirCount || g_mediaSelFile >= g_mediaCount) return;
    MediaEntry* e = &g_mediaEntries[g_mediaSelFile];
    media_preview_async(e->path);
    g_mediaPreviewReady = false;
}

// ---------------------------------------------------------------------------
// Window procedure.
// ---------------------------------------------------------------------------
static LRESULT CALLBACK MediaExplorerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        media_paint(hdc, &rc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        // Invalidate the entire client area so BitBlt is never clipped to a partial edge
        RECT rc; GetClientRect(hwnd, &rc);
        media_update_sb(&rc);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        // Prevent collapsing window into negative/unusable dimensions
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = scale_x(520);
        mmi->ptMinTrackSize.y = scale_y(320);
        return 0;
    }

    case WM_APP_MEDIA_LIST: {
        // A scan finished: apply the active sort to the new file list and
        // refresh the scrollbar range.
        media_sort_entries();
        RECT rc; GetClientRect(hwnd, &rc);
        media_update_sb(&rc);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_APP_MEDIA_PREVIEW: {
        g_mediaPreviewReady = true;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_TIMER: {
        // While the audition voice is playing, keep repainting so the scrub
        // head advances in the waveform strip.
        if (audition_is_playing()) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        SetFocus(hwnd);
        RECT rc; GetClientRect(hwnd, &rc);
        MediaLayout L; media_compute_layout(&rc, &L);

        // Right-pane custom scrollbar: thumb drag or track page scroll.
        {
            RefractSbGeom sg;
            if (media_sb_get_geom(&rc, &sg)) {
                RefractSbPart part = cseq_sb_hit_test(&sg, mx, my);
                if (part != CSEQ_SB_NONE) {
                    if (part == CSEQ_SB_THUMB) {
                        g_mediaSb.dragging = true;
                        g_mediaSb.grabOffsetPx = my - sg.thumbTop;
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                    } else {
                        // Page by the visible row count.
                        int rows = L.rightVisible;
                        if (rows < 1) rows = 1;
                        g_mediaFileScroll += (part == CSEQ_SB_TRACK_UP) ? -rows : rows;
                        media_sb_commit(hwnd, &rc);
                    }
                    return 0;
                }
            }
        }

        // Column headers: click to sort by that field.
        if (my >= L.rightPane.top && my <= L.rightPane.top + scale_y(20) &&
            mx >= L.rightPane.left && mx <= L.rightPane.right) {
            int sbW = cseq_sb_width();
            int colDur  = L.rightPane.right - sbW - scale_x(220);
            int colRate = L.rightPane.right - sbW - scale_x(150);
            int colCh   = L.rightPane.right - sbW - scale_x(100);
            int colFmt  = L.rightPane.right - sbW - scale_x(44);
            MediaSortField f;
            if (mx < colDur) f = MEDIA_SORT_NAME;
            else if (mx < colRate) f = MEDIA_SORT_DURATION;
            else if (mx < colCh) f = MEDIA_SORT_RATE;
            else if (mx < colFmt) f = MEDIA_SORT_CH;
            else f = MEDIA_SORT_FMT;
            if (f == g_mediaSortField) g_mediaSortAsc = !g_mediaSortAsc;
            else { g_mediaSortField = f; g_mediaSortAsc = true; }
            g_mediaFileScroll = 0;
            media_sort_entries();
            media_update_sb(&rc);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Waveform strip: click or begin scrub drag
        if (mx >= L.waveform.left && mx <= L.waveform.right &&
            my >= L.waveform.top && my <= L.waveform.bottom) {
            g_mediaWaveDragging = true;
            SetCapture(hwnd);
            media_seek_to_mouse(mx, &L.waveform);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Bottom bar controls.
        if (mx >= L.playBtn.left && mx <= L.playBtn.right && my >= L.playBtn.top && my <= L.playBtn.bottom) {
            if (g_mediaSelFile >= g_mediaDirCount && g_mediaSelFile < g_mediaCount) {
                if (audition_is_playing()) {
                    audition_stop();
                } else {
                    MediaEntry* e = &g_mediaEntries[g_mediaSelFile];
                    media_preview_async(e->path);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (mx >= L.addBtn.left && mx <= L.addBtn.right && my >= L.addBtn.top && my <= L.addBtn.bottom) {
            media_import_to_canvas();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (mx >= L.autoChk.left && mx <= L.autoChk.right && my >= L.autoChk.top && my <= L.autoChk.bottom) {
            g_mediaAutoPreview = !g_mediaAutoPreview;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (mx >= L.repeatChk.left && mx <= L.repeatChk.right && my >= L.repeatChk.top && my <= L.repeatChk.bottom) {
            g_mediaRepeat = !g_mediaRepeat;
            audition_set_repeat(g_mediaRepeat);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (mx >= L.speedCell.left && mx <= L.speedCell.right && my >= L.bottomPane.top && my <= L.bottomPane.bottom) {
            // Speed knob drag: track deltas per move for a smooth feel.
            g_mediaDragging = true;
            g_mediaDragMoved = false;
            g_mediaDragKnob = 0;
            g_mediaDragStart.x = mx; g_mediaDragStart.y = my;
            g_mediaDragLast.x = mx; g_mediaDragLast.y = my;
            SetCapture(hwnd);
            return 0;
        }
        if (mx >= L.volCell.left && mx <= L.volCell.right && my >= L.bottomPane.top && my <= L.bottomPane.bottom) {
            // Output level knob drag.
            g_mediaDragging = true;
            g_mediaDragMoved = false;
            g_mediaDragKnob = 1;
            g_mediaDragStart.x = mx; g_mediaDragStart.y = my;
            g_mediaDragLast.x = mx; g_mediaDragLast.y = my;
            SetCapture(hwnd);
            return 0;
        }

        // Left pane: directory browser.
        if (mx >= L.leftPane.left && mx <= L.leftPane.right && my >= L.leftPane.top && my <= L.leftPane.bottom) {
            int row = (my - L.leftPane.top - 2) / L.rowH;
            int visRow = row;
            if (visRow >= 0 && visRow < L.leftVisible) {
                bool hasUp = (g_mediaCurDir[0] != '\0');
                if (hasUp && visRow == 0) {
                    // Clicked "[..] Up" (always pinned to visual row 0)
                    media_navigate_up();
                } else {
                    int folderIdx = g_mediaDirScroll + (hasUp ? (visRow - 1) : visRow);
                    if (folderIdx >= 0 && folderIdx < g_mediaDirCount) {
                        MediaEntry* e = &g_mediaEntries[folderIdx];
                        media_set_cur_dir(e->path);
                        g_mediaSelDir = folderIdx; 
                        g_mediaSelFile = -1;
                        g_mediaFileScroll = 0; 
                        g_mediaDirScroll = 0;
                        media_scan_async();
                    }
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        // Right pane: select a file (and arm a possible drag to the canvas).
        if (mx >= L.rightPane.left && mx <= L.rightPane.right && my >= L.rightPane.top + scale_y(20) && my <= L.rightPane.bottom) {
            int vis = (my - L.rightPane.top - scale_y(20)) / L.rowH;
            int idx = g_mediaDirCount + g_mediaFileScroll + vis;
            if (idx >= g_mediaDirCount && idx < g_mediaCount) {
                g_mediaSelFile = idx;
                if (g_mediaAutoPreview) media_on_selection_change();
                g_mediaFileDragArmed = true;
                g_mediaFileDragIdx   = idx;
                g_mediaFileDragging  = false;
                g_mediaDragStart.x = mx; g_mediaDragStart.y = my;
                SetCapture(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);

        // Scrollbar thumb drag.
        if (g_mediaSb.dragging) {
            RECT rc; GetClientRect(hwnd, &rc);
            RefractSbGeom geo;
            if (!media_sb_get_geom(&rc, &geo)) {
                g_mediaSb.dragging = false;
                ReleaseCapture();
                return 0;
            }
            int trackLen = geo.trackBottom - geo.trackTop;
            int thumbLen = geo.thumbBottom - geo.thumbTop;
            int denom = trackLen - thumbLen;
            if (denom > 0) {
                float frac = (float)(my - g_mediaSb.grabOffsetPx - geo.trackTop) / (float)denom;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                int span = geo.scrollRange - geo.pagePx;
                if (span < 0) span = 0;
                MediaLayout L; media_compute_layout(&rc, &L);
                g_mediaFileScroll = (int)(frac * (float)span / (float)L.rowH + 0.5f);
                media_sb_commit(hwnd, &rc);
            }
            return 0;
        }

        // Smooth scrubbing across waveform
        if (g_mediaWaveDragging) {
            RECT rc; GetClientRect(hwnd, &rc);
            MediaLayout L; media_compute_layout(&rc, &L);
            media_seek_to_mouse(mx, &L.waveform);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (g_mediaDragging) {
            // Smooth knob drag: apply only the per-move delta, not the total
            // accumulated offset, so the speed tracks the mouse 1:1.
            int dx = mx - g_mediaDragLast.x;
            int dy = g_mediaDragLast.y - my;   // knob: up = increase
            g_mediaDragLast.x = mx; g_mediaDragLast.y = my;
            if (abs(mx - g_mediaDragStart.x) > 2 || abs(g_mediaDragStart.y - my) > 2) g_mediaDragMoved = true;
            if (g_mediaDragMoved && (dx != 0 || dy != 0)) {
                float delta = ((float)dy + (float)dx) / 200.0f;
                if (g_mediaDragKnob == 0) {
                    g_mediaSpeed += delta;
                    if (g_mediaSpeed < 0.5f) g_mediaSpeed = 0.5f;
                    if (g_mediaSpeed > 2.0f) g_mediaSpeed = 2.0f;
                    audition_set_speed(g_mediaSpeed);
                } else {
                    g_mediaVolume += delta;
                    if (g_mediaVolume < 0.0f) g_mediaVolume = 0.0f;
                    if (g_mediaVolume > 1.0f) g_mediaVolume = 1.0f;
                    audition_set_volume(g_mediaVolume);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        // Promote an armed file drag once the mouse moves past the threshold.
        if (g_mediaFileDragArmed) {
            int dx = mx - g_mediaDragStart.x;
            int dy = my - g_mediaDragStart.y;
            if (abs(dx) > get_drag_threshold() || abs(dy) > get_drag_threshold()) {
                g_mediaFileDragging = true;
                g_mediaFileDragArmed = false;
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_mediaSb.dragging) {
            g_mediaSb.dragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (g_mediaWaveDragging) {
            g_mediaWaveDragging = false;
            ReleaseCapture();
            return 0;
        }
        if (g_mediaDragging) {
            g_mediaDragging = false;
            ReleaseCapture();
            return 0;
        }
        if (g_mediaFileDragArmed || g_mediaFileDragging) {
            bool wasDragging = g_mediaFileDragging;
            g_mediaFileDragArmed = false;
            g_mediaFileDragging  = false;
            ReleaseCapture();
            if (wasDragging && g_mediaFileDragIdx >= g_mediaDirCount && g_mediaFileDragIdx < g_mediaCount) {
                // Drop onto the main timeline: convert to main-window client
                // coords and import at that point.
                POINT pt;
                GetCursorPos(&pt);
                if (g_hWnd) {
                    ScreenToClient(g_hWnd, &pt);
                    RECT cr;
                    GetClientRect(g_hWnd, &cr);
                    int clientH = cr.bottom - cr.top;
                    // Only drop if over the timeline viewport (not header/dock).
                    if (pt.y > get_header_height() && pt.y < clientH - get_bottom_dock_height()) {
                        media_import_at_point(g_mediaEntries[g_mediaFileDragIdx].path, pt.x, pt.y);
                    }
                }
            }
            return 0;
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_mediaSb.dragging) {
            g_mediaSb.dragging = false;
            g_mediaSb.grabOffsetPx = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_RBUTTONDOWN: {
        // Right-click on a knob resets it to the factory default.
        int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
        RECT rc; GetClientRect(hwnd, &rc);
        MediaLayout L; media_compute_layout(&rc, &L);
        if (mx >= L.speedCell.left && mx <= L.speedCell.right &&
            my >= L.bottomPane.top && my <= L.bottomPane.bottom) {
            g_mediaSpeed = AUDITION_SPEED_DEFAULT;
            audition_set_speed(g_mediaSpeed);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (mx >= L.volCell.left && mx <= L.volCell.right &&
            my >= L.bottomPane.top && my <= L.bottomPane.bottom) {
            g_mediaVolume = AUDITION_VOLUME_DEFAULT;
            audition_set_volume(g_mediaVolume);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        MediaLayout L; media_compute_layout(&rc, &L);
        // Wheel over the speed knob steps it in 0.05x increments.
        if (pt.x >= L.speedCell.left && pt.x <= L.speedCell.right &&
            pt.y >= L.bottomPane.top && pt.y <= L.bottomPane.bottom) {
            float step = (zDelta > 0) ? 0.05f : -0.05f;
            g_mediaSpeed += step;
            if (g_mediaSpeed < 0.5f) g_mediaSpeed = 0.5f;
            if (g_mediaSpeed > 2.0f) g_mediaSpeed = 2.0f;
            audition_set_speed(g_mediaSpeed);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        // Wheel over the volume knob steps it in 5% increments.
        if (pt.x >= L.volCell.left && pt.x <= L.volCell.right &&
            pt.y >= L.bottomPane.top && pt.y <= L.bottomPane.bottom) {
            float step = (zDelta > 0) ? 0.05f : -0.05f;
            g_mediaVolume += step;
            if (g_mediaVolume < 0.0f) g_mediaVolume = 0.0f;
            if (g_mediaVolume > 1.0f) g_mediaVolume = 1.0f;
            audition_set_volume(g_mediaVolume);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (pt.x >= L.rightPane.left && pt.x <= L.rightPane.right) {
            int step = (zDelta > 0) ? -1 : 1;
            g_mediaFileScroll += step;
            media_sb_commit(hwnd, &rc);
        } else if (pt.x >= L.leftPane.left && pt.x <= L.leftPane.right) {
            int step = (zDelta > 0) ? -1 : 1;
            g_mediaDirScroll += step;
            // Clamp maximum scroll
            if (g_mediaDirScroll > g_mediaDirCount - 1) g_mediaDirScroll = g_mediaDirCount - 1;
            if (g_mediaDirScroll < 0) g_mediaDirScroll = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        int vk = (int)wParam;
        if (vk == VK_ESCAPE) {
            audition_stop();   // mute the audition voice when the panel is hidden
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (vk == VK_OEM_2) {  // '/' toggles the panel closed when focused
            audition_stop();
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (vk == VK_DOWN) {
            int next = g_mediaSelFile + 1;
            if (next >= g_mediaDirCount && next < g_mediaCount) {
                g_mediaSelFile = next;
                int vis = g_mediaSelFile - g_mediaDirCount - g_mediaFileScroll;
                RECT rc; GetClientRect(hwnd, &rc);
                MediaLayout L; media_compute_layout(&rc, &L);
                if (vis >= L.rightVisible) g_mediaFileScroll++;
                if (g_mediaAutoPreview) media_on_selection_change();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (vk == VK_UP) {
            int next = g_mediaSelFile - 1;
            if (next >= g_mediaDirCount) {
                g_mediaSelFile = next;
                int vis = g_mediaSelFile - g_mediaDirCount - g_mediaFileScroll;
                if (vis < 0) g_mediaFileScroll--;
                if (g_mediaFileScroll < 0) g_mediaFileScroll = 0;
                if (g_mediaAutoPreview) media_on_selection_change();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (vk == VK_SPACE) {
            if (g_mediaSelFile >= g_mediaDirCount && g_mediaSelFile < g_mediaCount) {
                if (audition_is_playing()) {
                    audition_stop();
                } else {
                    MediaEntry* e = &g_mediaEntries[g_mediaSelFile];
                    media_preview_async(e->path);
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (vk == VK_RETURN) {
            media_import_to_canvas();
            return 0;
        }
        return 0;
    }

    case WM_CLOSE:
        // Hiding the panel must mute the audition voice immediately; otherwise
        // a preview started here keeps looping cached PCM in the master bus
        // while the user edits/auditions other clips (e.g. Quadrum notes).
        audition_stop();
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        audition_stop();
        g_mediaHwnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Launcher — mirrors open_fx_rack_dialog.
// ---------------------------------------------------------------------------
static inline void open_media_explorer(HWND parentHwnd) {
    if (!g_mediaListInit) {
        InitializeCriticalSection(&g_mediaListLock);
        g_mediaListInit = true;
    }

    if (!g_mediaHwnd) {
        static bool s_registered = false;
	if (!s_registered) {
            WNDCLASSA wc = { 0 };
            wc.style         = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc   = MediaExplorerWndProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = "RefractMediaExplorerClass";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            s_registered = true;
        }

        int rw = scale_x(820), rh = scale_y(540);
        RECT pr;
        GetWindowRect(parentHwnd, &pr);
        int scrW = GetSystemMetrics(SM_CXSCREEN), scrH = GetSystemMetrics(SM_CYSCREEN);
        int rx = pr.left + (pr.right - pr.left) / 2 - rw / 2;
        int ry = pr.top + (pr.bottom - pr.top) / 2 - rh / 2;
        if (rx < 0 || rx + rw > scrW) rx = (scrW - rw) / 2;
        if (ry < 0 || ry + rh > scrH) ry = (scrH - rh) / 2;

        g_mediaHwnd = CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            "RefractMediaExplorerClass",
            "Media Explorer",
            WS_POPUPWINDOW | WS_CAPTION | WS_VISIBLE | WS_SIZEBOX,
            rx, ry, rw, rh,
            parentHwnd, NULL, GetModuleHandle(NULL), NULL
        );

        // Start at the user's Documents folder (or home) on first open.
        if (g_mediaCurDir[0] == '\0') {
            const char* home = getenv("USERPROFILE");
            if (home) {
                char target[MAX_PATH];
                snprintf(target, sizeof(target), "%s\\Documents", home);
                if (GetFileAttributesA(target) == INVALID_FILE_ATTRIBUTES) {
                    strncpy(target, home, MAX_PATH - 1);
                }
                media_set_cur_dir(target);
            } else {
                media_set_cur_dir("C:\\");
            }
        }
    }

    // Always refresh the listing on show so files added in Windows Explorer
    // since the last open become visible.
    media_scan_async();

    // Keep the audition engine's output level in sync with the UI default.
    audition_set_volume(g_mediaVolume);
    audition_set_speed(g_mediaSpeed);

    // Refresh the right-pane scrollbar range for the current client size.
    {
        RECT cr; GetClientRect(g_mediaHwnd, &cr);
        media_update_sb(&cr);
    }

    // Repaint timer drives the waveform scrub head while a preview plays.
    SetTimer(g_mediaHwnd, 1, 33, NULL);

    ShowWindow(g_mediaHwnd, SW_SHOW);
    SetForegroundWindow(g_mediaHwnd);
    SetFocus(g_mediaHwnd);
    InvalidateRect(g_mediaHwnd, NULL, FALSE);
}
