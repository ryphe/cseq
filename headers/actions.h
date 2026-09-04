#pragma once
#include "globals.h"
#include "granular.h"
#include "synth.h"
#include "dsp.h"
#include "fx.h"
#include "state.h"
#include "samplecache.h"
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

#include "ogg.h"

 
static inline int get_clip_under_mouse(int mx, int my);
static inline void deselect_all_clips(void);
static inline void update_scrollbar(HWND hwnd);
static inline void init_track_theme(int trackIdx);
static inline void change_bar_count(int delta);
static inline void change_bar_count_even4(int delta);

static inline ma_uint64 find_nearest_zero_crossing(const AudioSample *s, ma_uint64 targetFrame, ma_uint64 maxWindow) {
    if (!s || !s->pFrames || s->frameCount == 0) return targetFrame;
    if (targetFrame >= s->frameCount) targetFrame = s->frameCount - 1;

    ma_uint64 start = (targetFrame > maxWindow) ? (targetFrame - maxWindow) : 0;
    ma_uint64 end   = targetFrame + maxWindow;
    if (end >= s->frameCount) end = s->frameCount - 1;

    ma_uint64 bestFrame = targetFrame;
    float minAmp = 1000.0f;
    
    for (ma_uint64 f = start; f < end; ++f) {
        float sampleVal = fabsf(s->pFrames[f * NUM_CHANNELS]);
        if (sampleVal < minAmp) {
            minAmp = sampleVal;
            bestFrame = f;
            if (minAmp < 0.0001f) break;
        }
    }
    return bestFrame;
}

// --- Multi-resolution peak cache ---------------------------------------------
// Level 0 = one min/max pair per PEAK_BASE_BIN_FRAMES frames; each level up
// aggregates PEAK_LOD_RATIO:1 so the renderer can match its resolution to the
// current zoom. Files above PEAK_SYNC_THRESHOLD_FRAMES get only a strided
// coarse preview synchronously (the load must stay instant) while a worker
// thread builds the full pyramid in the background and publishes it.

// ~11 min at 44.1 kHz: one full PCM pass takes tens of ms, done inline.
#define PEAK_SYNC_THRESHOLD_FRAMES 30000000ull
// Work cap for the strided preview pass (~4M mono touches = a few ms).
#define PEAK_PREVIEW_MAX_TOUCHES 4000000ull

// Compute the level layout for a frame count (no allocation). Level 0 rounds
// UP to the bin so the file tail is always covered (the last bin is clamped
// to frameCount when built); upper levels round up likewise.
static inline void peak_plan_levels(ma_uint64 frameCount, int* outLevels,
                                    int outOffset[PEAK_MAX_LOD_LEVELS],
                                    int outEntries[PEAK_MAX_LOD_LEVELS]) {
    ma_uint64 baseEntries = (frameCount + PEAK_BASE_BIN_FRAMES - 1) / PEAK_BASE_BIN_FRAMES;
    if (baseEntries == 0) baseEntries = 1;
    if (baseEntries > (ma_uint64)PEAK_MAX_BASE_ENTRIES) baseEntries = PEAK_MAX_BASE_ENTRIES;

    int n = 0;
    ma_uint64 entries = baseEntries;
    ma_uint64 offset = 0;
    while (n < PEAK_MAX_LOD_LEVELS) {
        outOffset[n] = (int)offset;
        outEntries[n] = (int)entries;
        offset += entries;
        n++;
        if (entries <= 1) break;
        entries = (entries + PEAK_LOD_RATIO - 1) / PEAK_LOD_RATIO;
    }
    *outLevels = n;
}

static inline size_t peak_total_entries(const AudioSample* s) {
    int offsets[PEAK_MAX_LOD_LEVELS], entries[PEAK_MAX_LOD_LEVELS];
    int levels;
    peak_plan_levels(s->frameCount, &levels, offsets, entries);
    size_t total = 0;
    for (int i = 0; i < levels; ++i) total += (size_t)entries[i];
    return total;
}

// Scan PCM into one contiguous pyramid buffer using s's layout. Level 0 is a
// true min/max over each 512-frame bin (tail clamped to frameCount, which
// fixes the old flat-zero tail when frameCount % bin != 0); upper levels
// aggregate PEAK_LOD_RATIO children.
static inline void peak_fill_pyramid(AudioSample* s, Peak* dst) {
    int offsets[PEAK_MAX_LOD_LEVELS], entries[PEAK_MAX_LOD_LEVELS];
    int levels;
    peak_plan_levels(s->frameCount, &levels, offsets, entries);

    // Level 0.
    Peak* l0 = dst;
    const int n0 = entries[0];
    const ma_uint64 bin = PEAK_BASE_BIN_FRAMES;
    for (int i = 0; i < n0; ++i) {
        ma_uint64 start = (ma_uint64)i * bin;
        ma_uint64 end = start + bin;
        if (end > s->frameCount) end = s->frameCount;
        float minV = 0.0f, maxV = 0.0f;
        for (ma_uint64 f = start; f < end; ++f) {
            float mono = (s->pFrames[f * 2 + 0] + s->pFrames[f * 2 + 1]) * 0.5f;
            if (mono < minV) minV = mono;
            if (mono > maxV) maxV = mono;
        }
        l0[i].min = minV;
        l0[i].max = maxV;
    }

    // Upper levels: 4:1 min/max aggregation of the previous level.
    for (int l = 1; l < levels; ++l) {
        const Peak* prev = dst + offsets[l - 1];
        const int nPrev = entries[l - 1];
        Peak* cur = dst + offsets[l];
        const int nCur = entries[l];
        for (int i = 0; i < nCur; ++i) {
            int c0 = i * PEAK_LOD_RATIO;
            int c1 = c0 + PEAK_LOD_RATIO;
            if (c1 > nPrev) c1 = nPrev;
            float minV = prev[c0].min, maxV = prev[c0].max;
            for (int c = c0 + 1; c < c1; ++c) {
                if (prev[c].min < minV) minV = prev[c].min;
                if (prev[c].max > maxV) maxV = prev[c].max;
            }
            cur[i].min = minV;
            cur[i].max = maxV;
        }
    }
}

// Coarse overview used when a file is too big to scan synchronously: strided
// sampling across the whole file capped at PEAK_PREVIEW_MAX_TOUCHES. It fills
// ONLY the highest level(s) geometry trick: simpler to fill level 0 strided,
// then aggregate upward - the preview is soft but structurally identical.
static inline void peak_fill_preview(AudioSample* s, Peak* dst) {
    int offsets[PEAK_MAX_LOD_LEVELS], entries[PEAK_MAX_LOD_LEVELS];
    int levels;
    peak_plan_levels(s->frameCount, &levels, offsets, entries);

    const int n0 = entries[0];
    ma_uint64 totalFrames = s->frameCount;
    // Stride so that n0 probes tile the whole file (ceil: the last probe
    // starts inside the file and reaches its end).
    ma_uint64 stride = (totalFrames + (ma_uint64)n0 - 1) / (ma_uint64)n0;
    if (stride < 1) stride = 1;

    for (int i = 0; i < n0; ++i) {
        ma_uint64 start = (ma_uint64)i * stride;
        ma_uint64 end = start + stride;
        if (start >= totalFrames) { dst[i].min = 0.0f; dst[i].max = 0.0f; continue; }
        if (end > totalFrames) end = totalFrames;

        // Sub-stride so the per-probe work stays capped on huge files.
        ma_uint64 step = (stride > 64) ? (stride / 64) : 1;

        float minV = 0.0f, maxV = 0.0f;
        for (ma_uint64 f = start; f < end; f += step) {
            float mono = (s->pFrames[f * 2 + 0] + s->pFrames[f * 2 + 1]) * 0.5f;
            if (mono < minV) minV = mono;
            if (mono > maxV) maxV = mono;
        }
        dst[i].min = minV;
        dst[i].max = maxV;
    }

    for (int l = 1; l < levels; ++l) {
        const Peak* prev = dst + offsets[l - 1];
        const int nPrev = entries[l - 1];
        Peak* cur = dst + offsets[l];
        const int nCur = entries[l];
        for (int i = 0; i < nCur; ++i) {
            int c0 = i * PEAK_LOD_RATIO;
            int c1 = c0 + PEAK_LOD_RATIO;
            if (c1 > nPrev) c1 = nPrev;
            float minV = prev[c0].min, maxV = prev[c0].max;
            for (int c = c0 + 1; c < c1; ++c) {
                if (prev[c].min < minV) minV = prev[c].min;
                if (prev[c].max > maxV) maxV = prev[c].max;
            }
            cur[i].min = minV;
            cur[i].max = maxV;
        }
    }
}

// Build the full pyramid synchronously (small/typical files).
static inline void generate_peak_cache(AudioSample* s) {
    if (!s || !s->pFrames || s->frameCount == 0) return;

    size_t total = peak_total_entries(s);
    Peak* buf = (Peak*)calloc(total, sizeof(Peak));
    if (!buf) return;

    peak_fill_pyramid(s, buf);
    free_peak_cache(s);
    s->peaks = buf;
    s->peakTotal = (int)total;
    {
        int offsets[PEAK_MAX_LOD_LEVELS], entries[PEAK_MAX_LOD_LEVELS];
        int levels;
        peak_plan_levels(s->frameCount, &levels, offsets, entries);
        s->lodCount = levels;
        memcpy(s->lodOffset, offsets, sizeof(offsets));
        memcpy(s->lodEntries, entries, sizeof(entries));
    }
    InterlockedExchange(&s->peaksReady, 1);
}

// Background refinement for huge files: build a fresh pyramid off-thread and
// publish it under the seq lock. Touches only the sample's own PCM buffer,
// which is immutable once loaded, so no torn reads are possible.
typedef struct {
    int sampleIndex;
} PeakBuildJob;

static DWORD WINAPI PeakBuildThreadProc(LPVOID lpParam) {
    PeakBuildJob job = *(PeakBuildJob*)lpParam;
    free(lpParam);

    seq_lock();
    if (job.sampleIndex < 0 || job.sampleIndex >= g_Seq.sampleCount ||
        !g_Seq.samples[job.sampleIndex].loaded || !g_Seq.samples[job.sampleIndex].pFrames) {
        seq_unlock();
        return 0;
    }
    AudioSample sCopy = g_Seq.samples[job.sampleIndex];   // header + pointers
    seq_unlock();

    size_t total = peak_total_entries(&sCopy);
    Peak* buf = (Peak*)calloc(total, sizeof(Peak));
    if (!buf) return 0;

    peak_fill_pyramid(&sCopy, buf);

    seq_lock();
    AudioSample* live = &g_Seq.samples[job.sampleIndex];
    // Still the same file and no newer build landed? Then swap in.
    if (live->loaded && live->pFrames == sCopy.pFrames) {
        free(live->peaks);
        live->peaks = buf;
        live->peakTotal = (int)total;
        live->lodCount = sCopy.lodCount;
        memcpy(live->lodOffset, sCopy.lodOffset, sizeof(live->lodOffset));
        memcpy(live->lodEntries, sCopy.lodEntries, sizeof(live->lodEntries));
        InterlockedExchange(&live->peaksReady, 1);
        seq_unlock();
        invalidate_timeline_cache();
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
        return 0;
    }
    seq_unlock();
    free(buf);
    return 0;
}

static inline void generate_peak_cache_async(int sampleIndex) {
    if (sampleIndex < 0 || sampleIndex >= g_Seq.sampleCount) return;
    PeakBuildJob* job = (PeakBuildJob*)malloc(sizeof(PeakBuildJob));
    if (!job) return;
    job->sampleIndex = sampleIndex;
    HANDLE h = CreateThread(NULL, 0, PeakBuildThreadProc, job, 0, NULL);
    if (h) CloseHandle(h);
    else free(job);
}

// Public autodetector entry used by every load path: small/typical files get
// the full pyramid inline; huge files get an instant strided preview plus a
// background refinement pass ("chunked" waveform generation).
static inline void generate_peak_cache_auto(AudioSample* s) {
    if (!s || !s->pFrames || s->frameCount == 0) return;

    if (s->frameCount <= PEAK_SYNC_THRESHOLD_FRAMES) {
        generate_peak_cache(s);
        return;
    }

    // Too long for an inline scan: preview now, refine on a worker thread.
    size_t total = peak_total_entries(s);
    Peak* buf = (Peak*)calloc(total, sizeof(Peak));
    if (!buf) return;
    peak_fill_preview(s, buf);
    free_peak_cache(s);
    s->peaks = buf;
    s->peakTotal = (int)total;
    {
        int offsets[PEAK_MAX_LOD_LEVELS], entries[PEAK_MAX_LOD_LEVELS];
        int levels;
        peak_plan_levels(s->frameCount, &levels, offsets, entries);
        s->lodCount = levels;
        memcpy(s->lodOffset, offsets, sizeof(offsets));
        memcpy(s->lodEntries, entries, sizeof(entries));
    }
    InterlockedExchange(&s->peaksReady, 0);
}

// Called with seq_lock held. `heapPcm` is a freshly decoded PCM buffer owned by
// the caller; this helper takes ownership. Appends a new sample slot backed by
// the disk cache, or returns an existing slot on dedup (same content). Returns
// the sample index to use, or -1 on failure (heapPcm freed).
static inline int sample_finalize_loaded(const char* filepath, float* heapPcm,
                                         ma_uint64 framesRead, const char* baseName) {
    if (g_Seq.sampleCount >= MAX_SAMPLES) { free(heapPcm); return -1; }

    // Dedup: cheap pre-filter by frame count + name, then confirm by content
    // hash so two different files that decode identically also collapse.
    uint64_t hash = sample_hash_pcm(heapPcm, (size_t)framesRead * sizeof(float) * 2u);
    for (int i = 0; i < g_Seq.sampleCount; ++i) {
        const AudioSample* ex = &g_Seq.samples[i];
        if (ex->loaded && ex->frameCount == framesRead &&
            ex->contentHash == hash &&
            strncmp(ex->name, baseName, sizeof(ex->name) - 1) == 0) {
            free(heapPcm);
            return i;
        }
    }

    int idx = g_Seq.sampleCount;
    AudioSample* sample = &g_Seq.samples[idx];
    memset(sample, 0, sizeof(AudioSample));
    strncpy(sample->filename, filepath, MAX_PATH - 1);
    sample->filename[MAX_PATH - 1] = '\0';
    strncpy(sample->name, baseName, sizeof(sample->name) - 1);
    sample->name[sizeof(sample->name) - 1] = '\0';
    sample->frameCount = framesRead;
    sample_install_cached(sample, heapPcm, framesRead, hash);
    generate_peak_cache_auto(sample);
    sample->loaded = true;
    g_Seq.sampleCount++;
    return idx;
}

 
static inline int load_audio_file(const char *filepath) {
    if (g_Seq.isBusy) return -1;
    seq_lock();
    if (g_Seq.sampleCount >= MAX_SAMPLES) {
        seq_unlock();
        return -1;
    }
    seq_unlock();

     
    WCHAR wpath[MAX_PATH * 2];
    bool haveWide = (utf8_to_wide_buf(filepath, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) > 0);

     
    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, NUM_CHANNELS, SAMPLE_RATE);
    ma_decoder decoder;
    ma_result result = haveWide
        ? ma_decoder_init_file_w(wpath, &decoderConfig, &decoder)
        : ma_decoder_init_file(filepath, &decoderConfig, &decoder);
    if (result == MA_SUCCESS) {
        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
        if (totalFrames == 0) {
            ma_decoder_uninit(&decoder);
            goto try_mf;
        }

        float *pFrames = (float *)malloc(sizeof(float) * totalFrames * NUM_CHANNELS);
        if (!pFrames) {
            ma_decoder_uninit(&decoder);
            return -1;
        }

        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, pFrames, totalFrames, &framesRead);
        ma_decoder_uninit(&decoder);

        if (framesRead == 0) {
            free(pFrames);
            goto try_mf;
        }

        seq_lock();
        const char *baseName = strrchr(filepath, '\\');
        baseName = baseName ? baseName + 1 : filepath;
        int idx = sample_finalize_loaded(filepath, pFrames, framesRead, baseName);
        seq_unlock();
        return idx;
    }

try_mf:
     
    {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
        BOOL needUninit = SUCCEEDED(hr);

         
        IMFSourceReader *pReader = NULL;
        hr = MFCreateSourceReaderFromURL(wpath, NULL, &pReader);
        if (FAILED(hr) || !pReader) {
            if (needUninit) CoUninitialize();
            goto try_ogg;
        }

         
        IMFMediaType *pType = NULL;
        hr = MFCreateMediaType(&pType);
        if (FAILED(hr) || !pType) {
            pReader->lpVtbl->Release(pReader);
            if (needUninit) CoUninitialize();
            goto try_ogg;
        }

        pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
        pType->lpVtbl->SetUINT32(pType, &MF_MT_AUDIO_NUM_CHANNELS, NUM_CHANNELS);
        pType->lpVtbl->SetUINT32(pType, &MF_MT_AUDIO_SAMPLES_PER_SECOND, SAMPLE_RATE);
        pType->lpVtbl->SetUINT32(pType, &MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        pType->lpVtbl->SetUINT32(pType, &MF_MT_AUDIO_BLOCK_ALIGNMENT, NUM_CHANNELS * sizeof(float));
        pType->lpVtbl->SetUINT32(pType, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                 SAMPLE_RATE * NUM_CHANNELS * sizeof(float));

        hr = pReader->lpVtbl->SetCurrentMediaType(pReader,
                                                  (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                                  NULL, pType);
        pType->lpVtbl->Release(pType);
        pType = NULL;

        if (FAILED(hr)) {
            pReader->lpVtbl->Release(pReader);
            if (needUninit) CoUninitialize();
            goto try_ogg;
        }

         
        size_t capacity = 0;
        size_t frames   = 0;
        float *pFrames  = NULL;
        const size_t chunkHint = 8192;

        for (;;) {
            DWORD streamIndex = 0, flags = 0;
            LONGLONG timestamp = 0;
            IMFSample *pSample = NULL;

            hr = pReader->lpVtbl->ReadSample(pReader,
                                             (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                             0, &streamIndex, &flags, &timestamp, &pSample);

            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                if (pSample) pSample->lpVtbl->Release(pSample);
                break;
            }
            if (!pSample) continue;

            IMFMediaBuffer *pBuffer = NULL;
            hr = pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer);
            if (SUCCEEDED(hr) && pBuffer) {
                BYTE *pData = NULL;
                DWORD cbMax = 0, cb = 0;
                hr = pBuffer->lpVtbl->Lock(pBuffer, &pData, &cbMax, &cb);
                if (SUCCEEDED(hr) && pData && cb >= sizeof(float) * NUM_CHANNELS) {
                    size_t thisFrames = cb / (sizeof(float) * NUM_CHANNELS);

                    if (frames + thisFrames > capacity) {
                        size_t newCap = (capacity == 0) ? thisFrames * 2 : capacity * 2;
                        if (newCap < frames + thisFrames)
                            newCap = frames + thisFrames + chunkHint;

                        float *n = (float *)realloc(pFrames, newCap * NUM_CHANNELS * sizeof(float));
                        if (!n) {
                            pBuffer->lpVtbl->Unlock(pBuffer);
                            pBuffer->lpVtbl->Release(pBuffer);
                            pSample->lpVtbl->Release(pSample);
                            free(pFrames);
                            pReader->lpVtbl->Release(pReader);
                            if (needUninit) CoUninitialize();
                            goto try_ogg;
                        }
                        pFrames  = n;
                        capacity = newCap;
                    }

                    memcpy(pFrames + frames * NUM_CHANNELS, pData,
                           thisFrames * NUM_CHANNELS * sizeof(float));
                    frames += thisFrames;

                    pBuffer->lpVtbl->Unlock(pBuffer);
                }
                pBuffer->lpVtbl->Release(pBuffer);
            }
            pSample->lpVtbl->Release(pSample);
        }

        pReader->lpVtbl->Release(pReader);
        if (needUninit) CoUninitialize();

        if (!pFrames || frames == 0) {
            if (pFrames) free(pFrames);
            goto try_ogg;
        }

         
        seq_lock();
        const char *baseName = strrchr(filepath, '\\');
        baseName = baseName ? baseName + 1 : filepath;
        int idx = sample_finalize_loaded(filepath, pFrames, frames, baseName);
        seq_unlock();
        return idx;
    }

try_ogg:
     
    {
        OggDecoder d;
        memset(&d, 0, sizeof(d));

        if (ogg_open(&d, filepath) && ogg_decode_all(&d)) {
            seq_lock();
            if (g_Seq.sampleCount >= MAX_SAMPLES) {
                seq_unlock();
                 
                if (d.pcm) {
                    free(d.pcm);
                    d.pcm = NULL;
                }
                ogg_close(&d);
                return -1;
            }

            const char *baseName = strrchr(filepath, '\\');
            baseName = baseName ? baseName + 1 : filepath;
            int idx = sample_finalize_loaded(filepath, d.pcm, d.frames, baseName);
            d.pcm = NULL;               
            seq_unlock();

            ogg_close(&d);
            return idx;
        }

        ogg_close(&d);
    }

    return -1;    
}

static inline int add_clip(int sampleIndex, int track, float startBeat) {
    if (g_Seq.isBusy) return -1;
    if (sampleIndex < 0 || sampleIndex >= g_Seq.sampleCount) return -1;
    if (track < 0 || track >= g_Seq.trackCount) track = 0;

    push_undo_state();

    AudioSample *s = &g_Seq.samples[sampleIndex];
    float fpb = frames_per_beat(g_Seq.bpm);
    float minLen = get_min_clip_length_beats();
    float lengthBeats = (float)s->frameCount / fpb;
    if (lengthBeats < minLen) lengthBeats = minLen;

     
    if (startBeat >= total_beats()) {
        return -1;
    }

     
    if (startBeat + lengthBeats > total_beats()) {
        float neededBeats = startBeat + lengthBeats;
        int neededBars = (int)ceilf(neededBeats / beats_per_bar());
        if (neededBars > MAX_BARS) neededBars = MAX_BARS;
        if (neededBars > g_Seq.visibleBarCount) {
            int grow = neededBars - g_Seq.visibleBarCount;
            change_bar_count(grow);           
        }
         
        if (startBeat + lengthBeats > total_beats()) {
            lengthBeats = total_beats() - startBeat;
            if (lengthBeats < minLen) lengthBeats = minLen;
        }
    }

    Clip newClip;
    memset(&newClip, 0, sizeof(Clip));
    newClip.nextClipInBar = 0xFFFF;
    newClip.sampleIndex = sampleIndex;
    newClip.track = track;
    newClip.startBeat = startBeat;
    newClip.lengthBeats = lengthBeats;
    newClip.sampleOffsetFrames = find_nearest_zero_crossing(s, 0, 128);
    newClip.volume = 1.0f;
    newClip.playbackRate = 1.0f;
    newClip.fadeInBeats = 0.0f;
    newClip.fadeOutBeats = 0.0f;
    newClip.isSelected = true;

    seq_lock();
    if (g_Seq.clipCount >= MAX_CLIPS) {
        seq_unlock();
        return -1;
    }
    int idx = g_Seq.clipCount;
    g_Seq.clips[idx] = newClip;
    memset(&g_ClipGran[idx], 0, sizeof(GranularEngine));
    g_ClipGran[idx].clipIdx = idx;
    g_ClipGran[idx].trackIdx = track;
    g_ClipGran[idx].sampleIndex = sampleIndex;
    g_ClipGran[idx].volume = 0.85f;
    g_Seq.clipCount++;
    seq_unlock();

     
    cseq_clip_structure_changed();
    mark_clip_bars_dirty(&g_Seq.clips[idx]);

    return idx;
}

 
 
static inline bool split_single_clip_internal(int clipIdx, float splitBeat, float fpb, float maxTimelineBeats, float minLen) {
    if (clipIdx < 0 || clipIdx >= g_Seq.clipCount) return false;
    Clip *c = &g_Seq.clips[clipIdx];
    if (c->startBeat >= maxTimelineBeats) return false;
    if (!c->isMidi && (c->sampleIndex < 0 || c->sampleIndex >= g_Seq.sampleCount)) return false;

    float effectiveEnd = c->startBeat + c->lengthBeats;
    if (effectiveEnd > maxTimelineBeats) {
        c->lengthBeats = maxTimelineBeats - c->startBeat;
    }
    if (c->lengthBeats < minLen) {
        c->lengthBeats = minLen;
        return false;
    }

    const float halfMin = minLen * 0.5f;
    if (splitBeat <= c->startBeat + halfMin || splitBeat >= (c->startBeat + c->lengthBeats - halfMin)) {
        return false;
    }
    if (g_Seq.clipCount >= MAX_CLIPS) return false;

    float firstPartLen = splitBeat - c->startBeat;
    float secondPartLen = c->lengthBeats - firstPartLen;

         
        if (c->isMidi) {
            Clip n;
            memset(&n, 0, sizeof(Clip));
            n.nextClipInBar = 0xFFFF;
            n.isMidi = true;
            n.sampleIndex = c->sampleIndex;
            n.track = c->track;
            n.startBeat = splitBeat;
            n.lengthBeats = secondPartLen;
            if (n.lengthBeats < minLen) n.lengthBeats = minLen;
            n.volume = c->volume;
            n.playbackRate = 1.0f;
            n.isSelected = false;

            MidiNote tempNotes[MIDI_MAX_NOTES];
            int tempCount = c->midiNoteCount;
            if (tempCount > MIDI_MAX_NOTES) tempCount = MIDI_MAX_NOTES;
            memcpy(tempNotes, c->midiNotes, sizeof(MidiNote) * tempCount);

            c->midiNoteCount = 0;
            n.midiNoteCount = 0;

            for (int ni = 0; ni < tempCount; ++ni) {
                MidiNote *nt = &tempNotes[ni];
                if (!nt->active) continue;

                float ntStart = nt->startBeat;
                float ntEnd = nt->startBeat + nt->lengthBeats;

                if (ntEnd <= firstPartLen + 0.001f) {
                    if (c->midiNoteCount < MIDI_MAX_NOTES) {
                        c->midiNotes[c->midiNoteCount++] = *nt;
                    }
                } else if (ntStart >= firstPartLen - 0.001f) {
                    MidiNote newNt = *nt;
                    newNt.startBeat -= firstPartLen;
                    if (newNt.startBeat < 0.0f) newNt.startBeat = 0.0f;
                    if (n.midiNoteCount < MIDI_MAX_NOTES) {
                        n.midiNotes[n.midiNoteCount++] = newNt;
                    }
                } else {
                    
                    MidiNote leftNt = *nt;
                    leftNt.lengthBeats = firstPartLen - ntStart;
                    if (leftNt.lengthBeats > 0.01f && c->midiNoteCount < MIDI_MAX_NOTES) {
                        c->midiNotes[c->midiNoteCount++] = leftNt;
                    }

                    MidiNote rightNt = *nt;
                    rightNt.startBeat = 0.0f;
                    rightNt.lengthBeats = ntEnd - firstPartLen;
                    if (rightNt.lengthBeats > 0.01f && n.midiNoteCount < MIDI_MAX_NOTES) {
                        n.midiNotes[n.midiNoteCount++] = rightNt;
                    }
                }
            }

            c->lengthBeats = firstPartLen;
            c->isSelected = true;

            int newIdx = g_Seq.clipCount;
            g_Seq.clips[newIdx] = n;
            memset(&g_ClipGran[newIdx], 0, sizeof(GranularEngine));
            g_ClipGran[newIdx].clipIdx = newIdx;
            g_ClipGran[newIdx].trackIdx = n.track;
            g_ClipGran[newIdx].sampleIndex = n.sampleIndex;
            g_ClipGran[newIdx].volume = 0.85f;
            g_Seq.clipCount++;
            synth_state_init_clip(newIdx);
            g_timelineDirty = true;
            return true;
        }

         
        AudioSample *s = &g_Seq.samples[c->sampleIndex];

        
        bool isGran = c->isGranular;
        GranularEngine origEngine = {0};   
        if (isGran) {
            
            origEngine = g_ClipGran[clipIdx];
            
            for (int g = 0; g < GRAN_MAX_GRAINS; ++g)
                g_ClipGran[clipIdx].grains[g].active = false;
            g_ClipGran[clipIdx].spawnAcc = 0.0f;
        }

        
        float pRate = (c->playbackRate > 0.01f) ? c->playbackRate : 1.0f;
        ma_uint64 splitDeltaFrames = (ma_uint64)(firstPartLen * fpb * pRate);
        ma_uint64 rawSecondOffset = c->sampleOffsetFrames + splitDeltaFrames;
        if (rawSecondOffset >= s->frameCount) {
            return false;
        }
        ma_uint64 secondOffset = find_nearest_zero_crossing(s, rawSecondOffset, 256);
        float origFadeOut = c->fadeOutBeats;

        c->lengthBeats = firstPartLen;
        if (c->fadeInBeats > c->lengthBeats) c->fadeInBeats = c->lengthBeats;
        c->fadeOutBeats = 0.0f;
        c->isSelected = true;

        
        Clip n;
        memset(&n, 0, sizeof(Clip));
        n.nextClipInBar = 0xFFFF;
        n.sampleIndex = c->sampleIndex;
        n.track = c->track;
        n.startBeat = splitBeat;
        n.lengthBeats = secondPartLen;

        if (n.lengthBeats < minLen) n.lengthBeats = minLen;

        n.sampleOffsetFrames = secondOffset;
        n.volume = c->volume;
        n.playbackRate = c->playbackRate;
        n.fadeInBeats = 0.0f;
        n.fadeOutBeats = (origFadeOut > n.lengthBeats) ? n.lengthBeats : origFadeOut;
         
        n.fadeInType = c->fadeInType;
        n.fadeOutType = c->fadeOutType;
        n.isSelected = false;

        int newIdx = g_Seq.clipCount;
        g_Seq.clips[newIdx] = n;
        g_Seq.clipCount++;
        synth_state_init_clip(newIdx);

        
        if (isGran) {
            GranularEngine *origEng = &g_ClipGran[clipIdx];
            GranularEngine *newEng = &g_ClipGran[newIdx];

            
            newEng->enabled        = origEng->enabled;
            newEng->grainSizeMs    = origEng->grainSizeMs;
            newEng->density        = origEng->density;
            newEng->position       = origEng->position;
            newEng->posJitter      = origEng->posJitter;
            newEng->pitch          = origEng->pitch;
            newEng->pitchJitter    = origEng->pitchJitter;
            newEng->panSpread      = origEng->panSpread;
            newEng->attack         = origEng->attack;
            newEng->release        = origEng->release;
            newEng->volume         = origEng->volume;
            newEng->freeze         = origEng->freeze;
            newEng->droneMode      = origEng->droneMode;
            newEng->octaveShift    = origEng->octaveShift;
            newEng->sampleIndex    = c->sampleIndex;       
            newEng->trackIdx       = c->track;
            newEng->clipIdx        = newIdx;
            newEng->ownLoaded      = false;                
            newEng->ownFrames      = NULL;
            newEng->ownFrameCount  = 0;
            newEng->spawnAcc       = 0.0f;
            memset(newEng->grains, 0, sizeof(newEng->grains));

            
            GranNote tempNotes[GRAN_MAX_NOTES];
            int tempCount = 0;
            for (int ni = 0; ni < origEng->noteCount; ++ni) {
                tempNotes[tempCount++] = origEng->notes[ni];
            }

            origEng->noteCount = 0;
            newEng->noteCount = 0;

            for (int ni = 0; ni < tempCount; ++ni) {
                GranNote *note = &tempNotes[ni];
                if (!note->active) continue;

                float noteStart = note->startBeat;
                float noteEnd = note->startBeat + note->lengthBeats;

                if (noteEnd <= firstPartLen + 0.001f) {
                    if (origEng->noteCount < GRAN_MAX_NOTES) {
                        origEng->notes[origEng->noteCount++] = *note;
                    }
                } else if (noteStart >= firstPartLen - 0.001f) {
                    GranNote newNote = *note;
                    newNote.startBeat -= firstPartLen;
                    if (newNote.startBeat < 0.0f) newNote.startBeat = 0.0f;
                    if (newEng->noteCount < GRAN_MAX_NOTES) {
                        newEng->notes[newEng->noteCount++] = newNote;
                    }
                } else {
                    GranNote firstPart = *note;
                    firstPart.lengthBeats = firstPartLen - noteStart;
                    if (firstPart.lengthBeats > 0.0f && origEng->noteCount < GRAN_MAX_NOTES) {
                        origEng->notes[origEng->noteCount++] = firstPart;
                    }
                    GranNote secondPart = *note;
                    secondPart.startBeat = 0.0f;
                    secondPart.lengthBeats = noteEnd - firstPartLen;
                    if (secondPart.lengthBeats > 0.0f && newEng->noteCount < GRAN_MAX_NOTES) {
                        newEng->notes[newEng->noteCount++] = secondPart;
                    }
                }
            }

            g_Seq.clips[clipIdx].isGranular = true;
            g_Seq.clips[newIdx].isGranular = true;
            origEng->enabled = true;
            newEng->enabled = true;
            memset(origEng->grains, 0, sizeof(origEng->grains));
            memset(newEng->grains, 0, sizeof(newEng->grains));
            origEng->spawnAcc = 0.0f;
            newEng->spawnAcc = 0.0f;
        }

    g_timelineDirty = true;
    return true;
}

static inline void split_clips_at_playhead(void) {
    if (g_Seq.isBusy) return;
    ma_uint64 currentFrame = (ma_uint64)InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
    float curBeat = frame_to_beat(currentFrame, g_Seq.bpm, g_Seq.swing);
    curBeat = quantize_beat_16th(curBeat);
    float fpb = frames_per_beat(g_Seq.bpm);
    float maxTimelineBeats = total_beats();

    if (curBeat >= maxTimelineBeats) return;

    push_undo_state();

    seq_lock();
    int originalCount = g_Seq.clipCount;
    bool hasSelection = false;
    for (int i = 0; i < originalCount; ++i) {
        if (g_Seq.clips[i].isSelected) {
            hasSelection = true;
            break;
        }
    }

    float minLen = get_min_clip_length_beats();
    for (int i = 0; i < originalCount; ++i) {
        if (hasSelection && !g_Seq.clips[i].isSelected) continue;
        split_single_clip_internal(i, curBeat, fpb, maxTimelineBeats, minLen);
    }
    seq_unlock();
    cseq_clip_structure_changed();
}

 
static inline void split_clip_at_mouse_or_playhead(void) {
    if (g_Seq.isBusy) return;

    int hovClip = -1;
    if (g_Seq.mouseY > get_header_height()) {
        hovClip = get_clip_under_mouse(g_Seq.mouseX, g_Seq.mouseY);
    }

    if (hovClip >= 0 && hovClip < g_Seq.clipCount) {
        float ppb = get_pixels_per_beat();
        float mouseBeat = (float)(g_Seq.mouseX - get_track_header_width() + g_Seq.scrollX) / ppb;
        if (g_Seq.quantizeEnabled) mouseBeat = quantize_beat_16th(mouseBeat);

        push_undo_state();
        seq_lock();
        float fpb = frames_per_beat(g_Seq.bpm);
        float maxTimelineBeats = total_beats();
        float minLen = get_min_clip_length_beats();
        split_single_clip_internal(hovClip, mouseBeat, fpb, maxTimelineBeats, minLen);
        seq_unlock();
        cseq_clip_structure_changed();
        if (g_hWnd) InvalidateRect(g_hWnd, NULL, FALSE);
    } else {
        split_clips_at_playhead();
    }
}

static inline void select_all_clips_on_track(int trackIdx) {
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        g_Seq.clips[i].isSelected = (g_Seq.clips[i].track == trackIdx);
    }
    seq_unlock();
     
    g_timelineDirty = true;
}

 
static inline void toggle_select_all_clips_on_track(int trackIdx) {
    seq_lock();
    bool allSelected = true;
    int countOnTrack = 0;

    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].track == trackIdx) {
            countOnTrack++;
            if (!g_Seq.clips[i].isSelected) {
                allSelected = false;
            }
        }
    }

    bool targetState = (countOnTrack > 0 && allSelected) ? false : true;
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].track == trackIdx) {
            g_Seq.clips[i].isSelected = targetState;
        }
    }
    seq_unlock();
     
    g_timelineDirty = true;
}

static inline void deselect_all_clips(void) {
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        g_Seq.clips[i].isSelected = false;
    }
    seq_unlock();
     
    g_timelineDirty = true;
}

static inline void delete_selected_clips(void) {
    if (g_Seq.isBusy) return;
    bool hasSelection = false;
    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].isSelected) {
            hasSelection = true;
            break;
        }
    }
    seq_unlock();

    if (!hasSelection && g_Seq.hoveredClip >= 0 && g_Seq.hoveredClip < g_Seq.clipCount) {
        push_undo_state();
        seq_lock();
        int del = g_Seq.hoveredClip;
        if (g_ClipGran[del].ownFrames) {
            free(g_ClipGran[del].ownFrames);
            g_ClipGran[del].ownFrames = NULL;
        }
        synth_state_shift_left(del, g_Seq.clipCount);
        for (int j = del; j < g_Seq.clipCount - 1; ++j) {
            g_Seq.clips[j] = g_Seq.clips[j + 1];
            g_ClipGran[j] = g_ClipGran[j + 1];
            g_ClipGran[j].clipIdx = j;
        }
        memset(&g_ClipGran[g_Seq.clipCount - 1], 0, sizeof(GranularEngine));
        g_ClipGran[g_Seq.clipCount - 1].clipIdx = g_Seq.clipCount - 1;
        g_ClipGran[g_Seq.clipCount - 1].sampleIndex = -1;
        g_Seq.clipCount--;
        g_Seq.hoveredClip = -1;
        seq_unlock();
        cseq_clip_structure_changed();
        return;
    }

    if (hasSelection) {
        push_undo_state();
        seq_lock();
        for (int i = 0; i < g_Seq.clipCount;) {
            if (g_Seq.clips[i].isSelected) {
                if (g_ClipGran[i].ownFrames) {
                    free(g_ClipGran[i].ownFrames);
                    g_ClipGran[i].ownFrames = NULL;
                }
                synth_state_shift_left(i, g_Seq.clipCount);
                for (int j = i; j < g_Seq.clipCount - 1; ++j) {
                    g_Seq.clips[j] = g_Seq.clips[j + 1];
                    g_ClipGran[j] = g_ClipGran[j + 1];
                    g_ClipGran[j].clipIdx = j;
                }
                memset(&g_ClipGran[g_Seq.clipCount - 1], 0, sizeof(GranularEngine));
                g_ClipGran[g_Seq.clipCount - 1].clipIdx = g_Seq.clipCount - 1;
                g_ClipGran[g_Seq.clipCount - 1].sampleIndex = -1;
                g_Seq.clipCount--;
            } else {
                i++;
            }
        }
        seq_unlock();
        cseq_clip_structure_changed();
    }
}

static inline void clear_clipboard(void) {
     
    seq_lock();
    for (int i = 0; i < g_Seq.clipboardCount; ++i) {
        if (g_Seq.clipboard[i].granSnapshot) {
             
            if (g_Seq.clipboard[i].granSnapshot->ownFrames) {
                free(g_Seq.clipboard[i].granSnapshot->ownFrames);
                g_Seq.clipboard[i].granSnapshot->ownFrames = NULL;
            }
            free(g_Seq.clipboard[i].granSnapshot);
            g_Seq.clipboard[i].granSnapshot = NULL;
        }
        if (g_Seq.clipboard[i].midiNotes) {
            free(g_Seq.clipboard[i].midiNotes);
            g_Seq.clipboard[i].midiNotes = NULL;
        }
    }
    g_Seq.clipboardCount = 0;
    seq_unlock();
}

 
#pragma pack(push, 1)
typedef struct {
    float   startBeat;
    float   lengthBeats;
    float   velocity;
    int32_t pitch;
    uint8_t active;
    uint8_t isSelected;
    uint8_t pad[2];
} CSQMidiNoteWire;                       

typedef struct {
    int32_t  trackOffset;
    float    beatOffset;
    float    lengthBeats;
    uint64_t sampleOffsetFrames;
    float    volume;
    float    playbackRate;
    float    fadeInBeats;
    float    fadeOutBeats;
    uint8_t  fadeInType;
    uint8_t  fadeOutType;
    uint8_t  isMuted;
    uint8_t  isMidi;
    uint8_t  reserved[2];
    float    adsrAttack;
    float    adsrDecay;
    float    adsrSustain;
    float    adsrRelease;
    int32_t  midiNoteCount;
    int32_t  sampleIndex;                
    char     sampleName[64];
} CSQClipItemWire;                       

typedef struct {
    uint32_t version;                    
    uint32_t itemCount;
} CSQClipboardWireHeader;                
#pragma pack(pop)

#define CSQ_CLIP_WIRE_VERSION 2u

 
static BOOL CALLBACK cseq_register_clip_fmt_cb(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)ctx;
    *(UINT *)param = RegisterClipboardFormatA("CSQ3_CLIPBOARD");
    return TRUE;
}

static inline UINT cseq_clipboard_format(void) {
    static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
    static UINT s_fmt = 0;
    InitOnceExecuteOnce(&s_once, cseq_register_clip_fmt_cb, &s_fmt, NULL);
    return s_fmt;
}

 
static inline void cseq_wire_copy_sample_name(char* dst, size_t dstCap, const char* src) {
    size_t srcCap = sizeof(g_Seq.samples[0].name);
    size_t n = (dstCap < srcCap) ? dstCap : srcCap;
    size_t i = 0;
    if (n > 0 && src) {
        while (i + 1 < n && src[i]) { dst[i] = src[i]; ++i; }
    }
    dst[i] = '\0';
    for (++i; i < dstCap; ++i) dst[i] = '\0';    
}

 
static HGLOBAL cseq_build_clipboard_hglobal(void) {
    if (g_Seq.clipboardCount <= 0) return NULL;

    int itemCount = g_Seq.clipboardCount;
    if (itemCount > MAX_CLIPS) itemCount = MAX_CLIPS;

    size_t total = sizeof(CSQClipboardWireHeader);
    for (int i = 0; i < itemCount; ++i) {
        const ClipboardItem* it = &g_Seq.clipboard[i];
        int cnt = it->midiNoteCount;
        if (cnt < 0) cnt = 0;
        if (cnt > MIDI_MAX_NOTES) cnt = MIDI_MAX_NOTES;
        total += sizeof(CSQClipItemWire) + (size_t)cnt * sizeof(CSQMidiNoteWire);
    }

    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
    if (!h) return NULL;

    BYTE* base = (BYTE*)GlobalLock(h);
    if (!base) { GlobalFree(h); return NULL; }

    CSQClipboardWireHeader* wh = (CSQClipboardWireHeader*)base;
    wh->version   = CSQ_CLIP_WIRE_VERSION;
    wh->itemCount = (uint32_t)itemCount;
    BYTE* p = base + sizeof(CSQClipboardWireHeader);

    for (int i = 0; i < itemCount; ++i) {
        const ClipboardItem* it = &g_Seq.clipboard[i];

        CSQClipItemWire* w = (CSQClipItemWire*)p;
        p += sizeof(CSQClipItemWire);
        memset(w, 0, sizeof(CSQClipItemWire));
        w->trackOffset        = it->trackOffset;
        w->beatOffset         = it->beatOffset;
        w->lengthBeats        = it->lengthBeats;
        w->sampleOffsetFrames = it->sampleOffsetFrames;
        w->volume             = it->volume;
        w->playbackRate       = it->playbackRate;
        w->fadeInBeats        = it->fadeInBeats;
        w->fadeOutBeats       = it->fadeOutBeats;
        w->fadeInType         = it->fadeInType;
        w->fadeOutType        = it->fadeOutType;
        w->isMuted            = it->isMuted ? 1 : 0;
        w->isMidi             = it->isMidi ? 1 : 0;
        w->adsrAttack         = it->adsrAttack;
        w->adsrDecay          = it->adsrDecay;
        w->adsrSustain        = it->adsrSustain;
        w->adsrRelease        = it->adsrRelease;
        w->midiNoteCount      = it->midiNoteCount;
        w->sampleIndex        = it->sampleIndex;

        if (it->sampleIndex >= 0 && it->sampleIndex < g_Seq.sampleCount) {
            cseq_wire_copy_sample_name(w->sampleName, sizeof(w->sampleName),
                                       g_Seq.samples[it->sampleIndex].name);
        }

        int cnt = it->midiNoteCount;
        if (cnt < 0) cnt = 0;
        if (cnt > MIDI_MAX_NOTES) cnt = MIDI_MAX_NOTES;
        for (int n = 0; n < cnt && it->midiNotes; ++n) {
            const MidiNote* src = &it->midiNotes[n];
            CSQMidiNoteWire* nw = (CSQMidiNoteWire*)p;
            p += sizeof(CSQMidiNoteWire);
            memset(nw, 0, sizeof(CSQMidiNoteWire));
            nw->startBeat   = src->startBeat;
            nw->lengthBeats = src->lengthBeats;
            nw->velocity    = src->velocity;
            nw->pitch       = (int32_t)src->pitch;
            nw->active      = src->active ? 1 : 0;
            nw->isSelected  = src->isSelected ? 1 : 0;
        }
    }

    GlobalUnlock(h);
    return h;
}

 
static inline void publish_clipboard_to_os(void) {
    UINT fmt = cseq_clipboard_format();
    if (fmt == 0) return;

    seq_lock();
    HGLOBAL hData = cseq_build_clipboard_hglobal();
    seq_unlock();
    if (!hData) return;

    if (!OpenClipboard(g_hWnd ? g_hWnd : NULL)) {
        GlobalFree(hData);
        return;
    }
    EmptyClipboard();
     
    if (SetClipboardData(fmt, hData) == NULL) {
        GlobalFree(hData);
    }
    CloseClipboard();
}

 
static inline bool import_clipboard_from_os(void) {
    UINT fmt = cseq_clipboard_format();
    if (fmt == 0 || !IsClipboardFormatAvailable(fmt)) return false;
    if (!OpenClipboard(g_hWnd ? g_hWnd : NULL)) return false;

    bool ok = false;
    HGLOBAL h = GetClipboardData(fmt);
    if (h) {
        SIZE_T sz = GlobalSize(h);
        const CSQClipboardWireHeader* wh = (const CSQClipboardWireHeader*)GlobalLock(h);
        if (wh && sz >= sizeof(CSQClipboardWireHeader) &&
            wh->version == CSQ_CLIP_WIRE_VERSION &&
            wh->itemCount > 0 && wh->itemCount <= (uint32_t)MAX_CLIPS) {

            seq_lock();
            clear_clipboard();    

            const BYTE* p   = (const BYTE*)wh + sizeof(CSQClipboardWireHeader);
            const BYTE* end = (const BYTE*)wh + sz;
            int imported = 0;

            for (uint32_t i = 0; i < wh->itemCount && imported < MAX_CLIPS; ++i) {
                if (p + sizeof(CSQClipItemWire) > end) break;
                const CSQClipItemWire* w = (const CSQClipItemWire*)p;
                p += sizeof(CSQClipItemWire);

                ClipboardItem* it = &g_Seq.clipboard[imported];
                memset(it, 0, sizeof(ClipboardItem));
                it->trackOffset        = w->trackOffset;
                it->beatOffset         = w->beatOffset;
                it->lengthBeats        = w->lengthBeats;
                it->sampleOffsetFrames = w->sampleOffsetFrames;
                it->volume             = w->volume;
                it->playbackRate       = w->playbackRate;
                it->fadeInBeats        = w->fadeInBeats;
                it->fadeOutBeats       = w->fadeOutBeats;
                it->fadeInType         = w->fadeInType;
                it->fadeOutType        = w->fadeOutType;
                it->isMuted            = w->isMuted != 0;
                it->isMidi             = w->isMidi != 0;
                it->adsrAttack         = w->adsrAttack;
                it->adsrDecay          = w->adsrDecay;
                it->adsrSustain        = w->adsrSustain;
                it->adsrRelease        = w->adsrRelease;
                it->isGranular         = false;    
                it->granSnapshot       = NULL;
                it->midiNotes          = NULL;
                it->midiNoteCount      = 0;

                 
                it->sampleIndex = -1;
                if (w->sampleName[0]) {
                    size_t cmp = sizeof(w->sampleName);
                    if (sizeof(g_Seq.samples[0].name) < cmp) cmp = sizeof(g_Seq.samples[0].name);
                    for (int s = 0; s < g_Seq.sampleCount; ++s) {
                        if (strncmp(g_Seq.samples[s].name, w->sampleName, cmp) == 0) {
                            it->sampleIndex = s;
                            break;
                        }
                    }
                }

                int cnt = w->midiNoteCount;
                if (cnt < 0) cnt = 0;
                if (cnt > MIDI_MAX_NOTES) cnt = MIDI_MAX_NOTES;
                if (cnt > 0 && p + (size_t)cnt * sizeof(CSQMidiNoteWire) <= end) {
                    it->midiNotes = (MidiNote*)malloc(sizeof(MidiNote) * cnt);
                    if (it->midiNotes) {
                        for (int n = 0; n < cnt; ++n) {
                            const CSQMidiNoteWire* nw = (const CSQMidiNoteWire*)p;
                            p += sizeof(CSQMidiNoteWire);
                            MidiNote* d = &it->midiNotes[n];
                            memset(d, 0, sizeof(MidiNote));
                            d->startBeat   = nw->startBeat;
                            d->lengthBeats = nw->lengthBeats;
                            d->velocity    = nw->velocity;
                            d->pitch       = (int)nw->pitch;    
                            d->active      = nw->active != 0;
                            d->isSelected  = nw->isSelected != 0;
                        }
                        it->midiNoteCount = cnt;
                    }
                } else {
                    cnt = 0;
                }

                imported++;
            }

            g_Seq.clipboardCount = imported;
            ok = (imported > 0);
            seq_unlock();
        }
        if (wh) GlobalUnlock(h);
    }
    CloseClipboard();
    return ok;
}

static inline void copy_selected_clips(void) {
    if (g_Seq.isBusy) return;
    seq_lock();
    clear_clipboard();
    float minBeat = 1e9f;
    int minTrack = 9999;

    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].isSelected) {
            if (g_Seq.clips[i].startBeat < minBeat) minBeat = g_Seq.clips[i].startBeat;
            if (g_Seq.clips[i].track < minTrack) minTrack = g_Seq.clips[i].track;
        }
    }

    if (minTrack == 9999) {
        seq_unlock();
        return;
    }

    for (int i = 0; i < g_Seq.clipCount; ++i) {
        if (g_Seq.clips[i].isSelected && g_Seq.clipboardCount < MAX_CLIPS) {
            ClipboardItem *item = &g_Seq.clipboard[g_Seq.clipboardCount++];
            item->sampleIndex = g_Seq.clips[i].sampleIndex;
            item->trackOffset = g_Seq.clips[i].track - minTrack;
            item->beatOffset = g_Seq.clips[i].startBeat - minBeat;
            item->lengthBeats = g_Seq.clips[i].lengthBeats;
            item->sampleOffsetFrames = g_Seq.clips[i].sampleOffsetFrames;
            item->volume = g_Seq.clips[i].volume;
            item->playbackRate = g_Seq.clips[i].playbackRate;
            item->fadeInBeats = g_Seq.clips[i].fadeInBeats;
            item->fadeOutBeats = g_Seq.clips[i].fadeOutBeats;
            item->fadeInType = g_Seq.clips[i].fadeInType;
            item->fadeOutType = g_Seq.clips[i].fadeOutType;
            item->isMuted = g_Seq.clips[i].isMuted;
            item->isGranular = g_Seq.clips[i].isGranular;
            item->isMidi = g_Seq.clips[i].isMidi;
            item->clipKind = g_Seq.clips[i].clipKind;
            item->adsrAttack = g_Seq.clips[i].adsrAttack;
            item->adsrDecay = g_Seq.clips[i].adsrDecay;
            item->adsrSustain = g_Seq.clips[i].adsrSustain;
            item->adsrRelease = g_Seq.clips[i].adsrRelease;
            item->synthAttack = g_Seq.clips[i].synthAttack;
            item->synthDecay = g_Seq.clips[i].synthDecay;
            item->synthSustain = g_Seq.clips[i].synthSustain;
            item->synthRelease = g_Seq.clips[i].synthRelease;
            memcpy(item->quadrumParams, g_Seq.clips[i].quadrumParams, sizeof(item->quadrumParams));
            item->haloPatch = g_Seq.clips[i].haloPatch;
            item->midiNotes = NULL;
            item->midiNoteCount = 0;

             
            if (item->isMidi && g_Seq.clips[i].midiNoteCount > 0) {
                int cnt = g_Seq.clips[i].midiNoteCount;
                if (cnt > MIDI_MAX_NOTES) cnt = MIDI_MAX_NOTES;
                item->midiNotes = (MidiNote*)malloc(sizeof(MidiNote) * cnt);
                if (item->midiNotes) {
                    memcpy(item->midiNotes, g_Seq.clips[i].midiNotes, sizeof(MidiNote) * cnt);
                    item->midiNoteCount = cnt;
                } else {
                    item->isMidi = false;
                }
            }
            else if (item->isMidi) {
                item->isMidi = true;  
            }

             
            if (item->isGranular) {
                item->granSnapshot = (GranClipSnapshot*)malloc(sizeof(GranClipSnapshot));
                if (item->granSnapshot) {
                    gran_engine_to_snapshot(item->granSnapshot, &g_ClipGran[i]);
                } else {
                    item->isGranular = false;
                    item->granSnapshot = NULL;
                }
            } else {
                item->granSnapshot = NULL;
            }
        }
    }
    seq_unlock();

     
    publish_clipboard_to_os();
}

static inline void paste_clipboard_clips(void) {
    if (g_Seq.isBusy) return;

     
    if (g_Seq.clipboardCount == 0) {
        import_clipboard_from_os();
    }
    if (g_Seq.clipboardCount == 0) return;

    push_undo_state();

    float curBeat = frame_to_beat((ma_uint64)InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0), g_Seq.bpm, g_Seq.swing);
    float baseBeat = quantize_beat_16th(curBeat);

    int baseTrack = 0;
    if (g_Seq.mouseY >= get_header_height()) {
        baseTrack = (g_Seq.mouseY - get_header_height() + g_Seq.scrollY) / get_track_height();
    }
    if (baseTrack < 0) baseTrack = 0;
    if (baseTrack >= g_Seq.trackCount) baseTrack = g_Seq.trackCount - 1;

    seq_lock();
    for (int i = 0; i < g_Seq.clipCount; ++i) g_Seq.clips[i].isSelected = false;

     
    float maxNeeded = 0.0f;
    for (int k = 0; k < g_Seq.clipboardCount; ++k) {
        float end = baseBeat + g_Seq.clipboard[k].beatOffset + g_Seq.clipboard[k].lengthBeats;
        if (end > maxNeeded) maxNeeded = end;
    }
    if (maxNeeded > total_beats()) {
        int neededBars = (int)ceilf(maxNeeded / beats_per_bar());
        if (neededBars > MAX_BARS) neededBars = MAX_BARS;
        if (neededBars > g_Seq.visibleBarCount) {
            int grow = neededBars - g_Seq.visibleBarCount;
             
            seq_unlock();
            change_bar_count(grow);
            seq_lock();
        }
    }

    int dropped = 0;
    for (int k = 0; k < g_Seq.clipboardCount; ++k) {
        if (g_Seq.clipCount >= MAX_CLIPS) {
            dropped = g_Seq.clipboardCount - k;
            break;
        }
        ClipboardItem *item = &g_Seq.clipboard[k];
        if (!item->isMidi && (item->sampleIndex < 0 || item->sampleIndex >= g_Seq.sampleCount)) continue;

        int targetTrack = baseTrack + item->trackOffset;
        if (targetTrack < 0) targetTrack = 0;
        if (targetTrack >= g_Seq.trackCount) targetTrack = g_Seq.trackCount - 1;

        float targetBeat = quantize_beat_16th(baseBeat + item->beatOffset);
        if (targetBeat < 0.0f) targetBeat = 0.0f;

        float minLen = get_min_clip_length_beats();
        float fitLen = item->lengthBeats;
        if (fitLen < minLen) fitLen = minLen;

        bool adjusted = true;
        int iter = 0;
        while (adjusted && iter++ < 64) {
            adjusted = false;
            for (int i = 0; i < g_Seq.clipCount; ++i) {
                Clip *ex = &g_Seq.clips[i];
                if (ex->track != targetTrack) continue;
                float exStart = ex->startBeat;
                float exEnd = ex->startBeat + ex->lengthBeats;
                float pasteEnd = targetBeat + fitLen;

                if (targetBeat < exEnd - 0.001f && pasteEnd > exStart + 0.001f) {
                    float nextBeat = exEnd;
                    if (g_Seq.quantizeEnabled) nextBeat = quantize_beat_16th(nextBeat);
                     
                    if (nextBeat >= total_beats() - minLen) {
                        nextBeat = total_beats() - minLen;
                        if (nextBeat < targetBeat) nextBeat = targetBeat;
                    }
                    if (nextBeat > targetBeat) {
                        targetBeat = nextBeat;
                        adjusted = true;
                        break;
                    }
                }
            }
        }

        if (targetBeat >= total_beats() - 0.001f) {
             
            continue;
        }

        if (targetBeat + fitLen > total_beats()) {
            fitLen = total_beats() - targetBeat;
        }
        if (fitLen < minLen) fitLen = minLen;

        Clip c;
        memset(&c, 0, sizeof(Clip));
        c.nextClipInBar = 0xFFFF;
        c.sampleIndex = item->sampleIndex;
        c.track = targetTrack;
        c.startBeat = targetBeat;
        c.lengthBeats = fitLen;
        c.sampleOffsetFrames = item->sampleOffsetFrames;
        c.volume = item->volume;
        c.playbackRate = item->playbackRate;
        c.fadeInBeats = item->fadeInBeats;
        c.fadeOutBeats = item->fadeOutBeats;
        c.fadeInType = item->fadeInType;
        c.fadeOutType = item->fadeOutType;
        c.isMuted = item->isMuted;
        c.isSelected = true;
        c.isGranular = item->isGranular;
        c.isMidi = item->isMidi;
        c.clipKind = item->clipKind;
        c.adsrAttack = item->adsrAttack;
        c.adsrDecay = item->adsrDecay;
        c.adsrSustain = item->adsrSustain;
        c.adsrRelease = item->adsrRelease;
        c.synthAttack = item->synthAttack;
        c.synthDecay = item->synthDecay;
        c.synthSustain = item->synthSustain;
        c.synthRelease = item->synthRelease;
        memcpy(c.quadrumParams, item->quadrumParams, sizeof(c.quadrumParams));
        c.haloPatch = item->haloPatch;
        if (c.isMidi && item->midiNotes && item->midiNoteCount > 0) {
            int cnt = item->midiNoteCount;
            if (cnt > MIDI_MAX_NOTES) cnt = MIDI_MAX_NOTES;
            memcpy(c.midiNotes, item->midiNotes, sizeof(MidiNote) * cnt);
            c.midiNoteCount = cnt;
            for (int ni = 0; ni < cnt; ++ni) {
                c.midiNotes[ni].velocity = clamp(c.midiNotes[ni].velocity, 0.0f, 100.0f);
            }
        }

        int newIdx = g_Seq.clipCount;
        g_Seq.clips[newIdx] = c;
        if (c.isGranular && item->granSnapshot) {
            gran_snapshot_to_engine(&g_ClipGran[newIdx], item->granSnapshot, newIdx, targetTrack);
        } else {
            memset(&g_ClipGran[newIdx], 0, sizeof(GranularEngine));
            g_ClipGran[newIdx].clipIdx = newIdx;
            g_ClipGran[newIdx].trackIdx = targetTrack;
            g_ClipGran[newIdx].sampleIndex = c.sampleIndex;
            g_ClipGran[newIdx].volume = 0.85f;
        }
        g_Seq.clipCount++;
        synth_state_init_clip(newIdx);
    }
    seq_unlock();
    cseq_clip_structure_changed();

    if (dropped > 0) {
        snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg),
                 "Paste truncated: %d clip(s) dropped (MAX_CLIPS)", dropped);
        g_Seq.exportMsgActive = true;
        g_Seq.exportMsgExpiry = GetTickCount64() + 4000;
    }
}

 
static inline int get_clip_under_mouse(int mx, int my) {
    if (my <= get_header_height() || mx < get_track_header_width() || g_Seq.clipCount <= 0) return -1;

    RECT rcClient;
    if (g_hWnd) {
        GetClientRect(g_hWnd, &rcClient);
        if (my >= (rcClient.bottom - rcClient.top - get_bottom_dock_height())) return -1;
    }

    float ppb = get_pixels_per_beat();
    seq_lock();
    for (int i = g_Seq.clipCount - 1; i >= 0; --i) {
        Clip *c = &g_Seq.clips[i];
        if (c->track >= g_Seq.trackCount || c->startBeat >= total_beats()) continue;

        float vLen = c->lengthBeats;
        if (c->startBeat + vLen > total_beats()) {
            vLen = total_beats() - c->startBeat;
        }
        if (vLen <= 0.0f) continue;

        int x1 = get_track_header_width() - g_Seq.scrollX + (int)(c->startBeat * ppb);
        int x2 = x1 + (int)(vLen * ppb);
        int y1 = get_header_height() - g_Seq.scrollY + c->track * get_track_height();
        int y2 = y1 + get_track_height();

        if (mx >= x1 && mx <= x2 && my >= y1 && my <= y2) {
            seq_unlock();
            return i;
        }
    }
    seq_unlock();
    return -1;
}

static inline void toggle_playback(void) {
    bool wasPlaying = seq_is_playing();
    if (!wasPlaying && g_Seq.playFromStartOnPlay) {
        InterlockedExchange(&g_Seq.playbackFrame, 0);
    }
    bool nowPlaying = !wasPlaying;
    seq_set_playing(nowPlaying);
    if (!nowPlaying) {
        granular_stop_all();
        synth_stop_all();
    }
}

static inline void open_sample_dialog(HWND hwnd, int track, float dropBeat) {
    if (g_Seq.isBusy) return;
     
    OPENFILENAMEW ofn;
    wchar_t szFileW[MAX_PATH] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"All Supported Audio (*.wav;*.mp3;*.flac;*.m4a;*.aac;*.wma;*.aiff;*.aif;*.ogg)\0"
        L"*.wav;*.mp3;*.flac;*.m4a;*.aac;*.wma;*.aiff;*.aif;*.ogg\0"
        L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileW;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        char szFile[MAX_PATH];
        if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) <= 0) return;
        int sampleIdx = load_audio_file(szFile);
        if (sampleIdx != -1) {
            add_clip(sampleIdx, track, dropBeat);
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

 
static inline void update_scrollbar(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int visibleH = rc.bottom - rc.top - get_header_height() - get_bottom_dock_height();
    int totalContentH = g_Seq.trackCount * get_track_height();

    
    g_sbState.totalContent = totalContentH;
    g_sbState.visibleHeight = visibleH;
    g_sbState.scrollPos = g_Seq.scrollY;

    
    int maxScroll = totalContentH - visibleH;
    if (maxScroll < 0) maxScroll = 0;
    if (g_Seq.scrollY > maxScroll) g_Seq.scrollY = maxScroll;
    if (g_Seq.scrollY < 0) g_Seq.scrollY = 0;

    
    
    g_sbState.visible = true;   

    
    float ppb = get_pixels_per_beat();
    int totalTimelineWidth = (int)(total_beats() * ppb);
    int visibleWidth = (rc.right - rc.left) - get_track_header_width();
    int maxScrollX = totalTimelineWidth - visibleWidth;
    if (maxScrollX < 0) maxScrollX = 0;
    if (g_Seq.scrollX > maxScrollX) g_Seq.scrollX = maxScrollX;
    if (g_Seq.scrollX < 0) g_Seq.scrollX = 0;
}

static inline void add_track_action(HWND hwnd) {
    seq_lock();
    if (g_Seq.trackCount < MAX_TRACKS) {
        int newIdx = g_Seq.trackCount;
        init_track_theme(newIdx);
        g_Seq.trackMuted[newIdx] = false;
        g_Seq.trackSolo[newIdx] = false;
        g_Seq.trackVolume[newIdx] = 1.0f;
        g_Seq.trackPan[newIdx] = 0.0f;
        g_Seq.trackWidth[newIdx] = 1.0f;
        g_Seq.trackTriggerProb[newIdx] = 1.0f;
        g_Seq.trackRngState[newIdx] = (uint32_t)(newIdx * 1337) + 1;

         
        memset(&g_TrackGran[newIdx], 0, sizeof(GranularEngine));
        g_TrackGran[newIdx].trackIdx = newIdx;
        g_TrackGran[newIdx].clipIdx = -1;
        g_TrackGran[newIdx].sampleIndex = -1;
        g_TrackGran[newIdx].volume = 0.85f;

        g_Seq.trackCount++;
        seq_unlock();
        seq_sync_track_masks();
        update_scrollbar(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        seq_unlock();
    }
}

 
static inline int insert_midi_clip(int track, float startBeat) {
    if (g_Seq.isBusy) return -1;
    if (track < 0 || track >= g_Seq.trackCount) track = 0;
    if (g_Seq.clipCount >= MAX_CLIPS) return -1;

    if (startBeat < 0.0f) startBeat = 0.0f;
    startBeat = quantize_beat_16th(startBeat);
    if (startBeat >= total_beats()) return -1;

     
    // A new MIDI clip spans a full bar; the grid division only sets the
    // snap, not the length (gridFrac*bpb made 1/16 snaps create 1-beat
    // clips that left one-beat gaps between bar-anchored clips).
    float gridFrac = grid_division_beat_fraction(g_Seq.gridDivision);
    float defaultLen = beats_per_bar();
    if (defaultLen < 0.25f) defaultLen = 0.25f;

    // Check if timeline needs expanding to accommodate the new clip
    if (startBeat + defaultLen > total_beats()) {
        float neededBeats = startBeat + defaultLen;
        int neededBars = (int)ceilf(neededBeats / beats_per_bar());
        if (neededBars > MAX_BARS) neededBars = MAX_BARS;
        if (neededBars > g_Seq.visibleBarCount) {
            change_bar_count(neededBars - g_Seq.visibleBarCount);
        }
    }

    // Clamp clip length so it never exceeds the timeline limit
    float fitLen = defaultLen;
    if (startBeat + fitLen > total_beats()) {
        fitLen = total_beats() - startBeat;
    }
    if (fitLen < gridFrac) {
        startBeat = total_beats() - gridFrac;
        if (startBeat < 0.0f) startBeat = 0.0f;
        fitLen = total_beats() - startBeat;
        if (fitLen <= 0.0f) return -1;
    }

    push_undo_state();

    seq_lock();
    if (g_Seq.clipCount >= MAX_CLIPS) {
        seq_unlock();
        return -1;
    }

    Clip newClip;
    memset(&newClip, 0, sizeof(Clip));
    newClip.nextClipInBar = 0xFFFF;
    newClip.isMidi = true;
    newClip.sampleIndex = -1;       
    newClip.track = track;
    newClip.startBeat = startBeat;
    newClip.lengthBeats = defaultLen;
    if (startBeat + newClip.lengthBeats > total_beats())
        newClip.lengthBeats = total_beats() - startBeat;
    if (newClip.lengthBeats < gridFrac) newClip.lengthBeats = gridFrac;
    newClip.volume = 1.0f;
    newClip.playbackRate = 1.0f;
    newClip.isSelected = true;
    newClip.adsrAttack = 5.0f;
    newClip.adsrDecay = 0.0f;
    newClip.adsrSustain = 1.0f;
    newClip.adsrRelease = 10.0f;
    // Synth-module envelope scaffolding defaults (engine-ready values).
    newClip.synthAttack = 5.0f;
    newClip.synthDecay = 0.0f;
    newClip.synthSustain = 1.0f;
    newClip.synthRelease = 10.0f;

    int idx = g_Seq.clipCount;
    g_Seq.clips[idx] = newClip;
    memset(&g_ClipGran[idx], 0, sizeof(GranularEngine));
    g_ClipGran[idx].clipIdx = idx;
    g_ClipGran[idx].trackIdx = track;
    g_ClipGran[idx].sampleIndex = -1;
    g_ClipGran[idx].volume = 0.85f;
    g_Seq.clipCount++;
    synth_state_init_clip(idx);
    seq_unlock();

    cseq_clip_structure_changed();
    mark_clip_bars_dirty(&g_Seq.clips[idx]);

    g_timelineDirty = true;
    return idx;
}

// Spawn a synth-module clip (Quadrum drum / Halo synth) at the playhead on
// the selected track (first selected, else track 0). The clip is MIDI-backed
// (isMidi = true) so notes ride the existing storage; clipKind marks it as a
// synth module. Returns the new clip index or -1.
static inline int spawn_synth_clip(int clipKind) {
    if (g_Seq.isBusy) return -1;
    if (clipKind != CLIP_KIND_QUADRUM && clipKind != CLIP_KIND_HALO) return -1;

    // Prefer the most recently clicked timeline track (falls back to the first
    // selected clip's track, then track 0).
    int track = g_Seq.lastClickedTrack;
    if (track < 0 || track >= g_Seq.trackCount) {
        track = 0;
        for (int c = 0; c < g_Seq.clipCount; ++c) {
            if (g_Seq.clips[c].isSelected &&
                g_Seq.clips[c].track >= 0 && g_Seq.clips[c].track < g_Seq.trackCount) {
                track = g_Seq.clips[c].track;
                break;
            }
        }
    }

    LONG pf = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
    float startBeat = frame_to_beat((ma_uint64)(pf < 0 ? 0 : pf), g_Seq.bpm, g_Seq.swing);
    float bpb = beats_per_bar();
    startBeat = (float)floor(startBeat / bpb + 0.5f) * bpb;   // snap to bar line
    float clipLen = bpb;

    // Auto-move the new clip to the extremity of whatever already occupies
    // the playhead on this track, mirroring the paste behavior: if the spawn
    // point overlaps an existing clip, slide it forward just past that clip's
    // end (quantized) so it never lands on top of another clip.
    seq_lock();
    bool adjusted = true;
    int iter = 0;
    while (adjusted && iter++ < 64) {
        adjusted = false;
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            Clip* ex = &g_Seq.clips[i];
            if (ex->track != track) continue;
            float exStart = ex->startBeat;
            float exEnd = ex->startBeat + ex->lengthBeats;
            float spawnEnd = startBeat + clipLen;
            if (startBeat < exEnd - 0.001f && spawnEnd > exStart + 0.001f) {
                float nextBeat = exEnd;
                if (g_Seq.quantizeEnabled) nextBeat = quantize_beat_16th(nextBeat);
                if (nextBeat > startBeat) {
                    startBeat = nextBeat;
                    adjusted = true;
                    break;
                }
            }
        }
    }
    seq_unlock();

    int idx = insert_midi_clip(track, startBeat);
    if (idx < 0) return -1;
    seq_lock();
    g_Seq.clips[idx].clipKind = (uint8_t)clipKind;
    // Initialize the engine patch(es) to factory defaults so a freshly spawned
    // clip is immediately playable. Also seed the piano-roll ADSR knobs.
    if (clipKind == CLIP_KIND_QUADRUM) {
        for (int v = 0; v < 8; ++v)
            quadrum_get_preset((VoiceType)v, &g_Seq.clips[idx].quadrumParams[v]);
        g_Seq.clips[idx].synthAttack  = 2.0f;
        g_Seq.clips[idx].synthDecay   = 40.0f;
        g_Seq.clips[idx].synthSustain = 1.0f;
        g_Seq.clips[idx].synthRelease = 20.0f;
    } else if (clipKind == CLIP_KIND_HALO) {
        halo_get_preset(0, &g_Seq.clips[idx].haloPatch);
        g_Seq.clips[idx].synthAttack  = 5.0f;
        g_Seq.clips[idx].synthDecay   = 0.0f;
        g_Seq.clips[idx].synthSustain = 1.0f;
        g_Seq.clips[idx].synthRelease = 10.0f;
    }
    synth_state_init_clip(idx);
    seq_unlock();
    return idx;
}

// Reorder a track: move track `from` so it lands at index `to`, shifting the
// tracks between them. Tracks stay contiguous (0..trackCount-1) to match the
// rest of the codebase. Every per-track array, the per-track FX/granular
// engines, and each clip's track index are moved together.
static inline void reorder_track(int from, int to) {
    if (from < 0 || from >= g_Seq.trackCount) return;
    if (to < 0 || to >= g_Seq.trackCount) return;
    if (from == to) return;

    seq_lock();

    // Build a remap table: oldIndex -> newIndex.
    int remap[MAX_TRACKS];
    for (int t = 0; t < MAX_TRACKS; ++t) remap[t] = t;
    if (to > from) {
        // Moving down: `from` lands at `to`; from+1..to shift up by one.
        for (int t = from + 1; t <= to; ++t) remap[t] = t - 1;
        remap[from] = to;
    } else {
        // Moving up: `from` lands at `to`; to..from-1 shift down by one.
        for (int t = to; t < from; ++t) remap[t] = t + 1;
        remap[from] = to;
    }

    // Swap helper macro for the per-track arrays: rotate [from..to] so `from`
    // lands at `to`.
#define SEQ_SWAP_TRACK_ARRAY(type, arr) \
    do { \
        type tmp = (arr)[from]; \
        if (to > from) { for (int t = from; t < to; ++t) (arr)[t] = (arr)[t + 1]; } \
        else           { for (int t = from; t > to; --t) (arr)[t] = (arr)[t - 1]; } \
        (arr)[to] = tmp; \
    } while (0)

    SEQ_SWAP_TRACK_ARRAY(bool, g_Seq.trackMuted);
    SEQ_SWAP_TRACK_ARRAY(bool, g_Seq.trackSolo);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackVolume);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackPan);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackWidth);
    SEQ_SWAP_TRACK_ARRAY(TrackTheme, g_Seq.trackThemes);

    SEQ_SWAP_TRACK_ARRAY(SmoothEQ3, g_Seq.trackEQ);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackEqLow);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackEqMid);
    SEQ_SWAP_TRACK_ARRAY(float, g_Seq.trackEqHigh);
    SEQ_SWAP_TRACK_ARRAY(bool, g_Seq.trackEqActive);

    // 2D per-track arrays: swap whole rows (3 PeakBiquads / 3 floats).
#define SEQ_SWAP_TRACK_ROWS(type, arr) \
    do { \
        type tmp[3]; \
        for (int k = 0; k < 3; ++k) tmp[k] = (arr)[from][k]; \
        if (to > from) { for (int t = from; t < to; ++t) for (int k = 0; k < 3; ++k) (arr)[t][k] = (arr)[t + 1][k]; } \
        else           { for (int t = from; t > to; --t) for (int k = 0; k < 3; ++k) (arr)[t][k] = (arr)[t - 1][k]; } \
        for (int k = 0; k < 3; ++k) (arr)[to][k] = tmp[k]; \
    } while (0)
    SEQ_SWAP_TRACK_ROWS(PeakBiquad, g_Seq.trackPeak);
    SEQ_SWAP_TRACK_ROWS(float, g_Seq.trackEqFreq);
    SEQ_SWAP_TRACK_ROWS(float, g_Seq.trackEqQ);
#undef SEQ_SWAP_TRACK_ROWS

    SEQ_SWAP_TRACK_ARRAY(TrackFilter, g_Seq.trackFilter);
    SEQ_SWAP_TRACK_ARRAY(TrackAudioHot, g_Seq.trackAudio);
    SEQ_SWAP_TRACK_ARRAY(TrackUICold, g_Seq.trackUI);
    SEQ_SWAP_TRACK_ARRAY(FxChain, g_TrackFx);

    // Per-track granular engines: swap whole structs, then fix trackIdx.
    {
        GranularEngine tmp = g_TrackGran[from];
        if (to > from) { for (int t = from; t < to; ++t) g_TrackGran[t] = g_TrackGran[t + 1]; }
        else           { for (int t = from; t > to; --t) g_TrackGran[t] = g_TrackGran[t - 1]; }
        g_TrackGran[to] = tmp;
        int lo = (from < to) ? from : to;
        int hi = (from < to) ? to : from;
        for (int t = lo; t <= hi; ++t) g_TrackGran[t].trackIdx = t;
    }

#undef SEQ_SWAP_TRACK_ARRAY

    // --- Renumber every clip's track and the per-clip granular trackIdx. ---
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        int old = g_Seq.clips[i].track;
        if (old >= 0 && old < MAX_TRACKS) {
            g_Seq.clips[i].track = remap[old];
        }
        g_ClipGran[i].trackIdx = g_Seq.clips[i].track;
    }

    seq_unlock();

    cseq_clip_structure_changed();
    g_timelineDirty = true;
}

static inline void remove_track_action(HWND hwnd) {
    seq_lock();
    if (g_Seq.trackCount > MIN_TRACKS) {
        int removedTrack = g_Seq.trackCount - 1;
        for (int i = 0; i < g_Seq.clipCount;) {
            if (g_Seq.clips[i].track == removedTrack) {
                if (g_ClipGran[i].ownFrames) {
                    free(g_ClipGran[i].ownFrames);
                    g_ClipGran[i].ownFrames = NULL;
                }
                for (int j = i; j < g_Seq.clipCount - 1; ++j) {
                    g_Seq.clips[j] = g_Seq.clips[j + 1];
                    g_ClipGran[j] = g_ClipGran[j + 1];
                    g_ClipGran[j].clipIdx = j;
                }
                memset(&g_ClipGran[g_Seq.clipCount - 1], 0, sizeof(GranularEngine));
                g_ClipGran[g_Seq.clipCount - 1].clipIdx = g_Seq.clipCount - 1;
                g_ClipGran[g_Seq.clipCount - 1].sampleIndex = -1;
                g_Seq.clipCount--;
            } else {
                i++;
            }
        }
        g_Seq.trackCount--;
         
        fx_chain_clear(&g_TrackFx[removedTrack]);
        seq_unlock();
        seq_sync_track_masks();
        cseq_clip_structure_changed();
        update_scrollbar(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        seq_unlock();
    }
}

static inline void change_bar_count(int delta) {
    int oldCount = g_Seq.visibleBarCount;
    long long newCount = (long long)oldCount + delta;

    if (newCount < MIN_BARS) newCount = MIN_BARS;
    else if (newCount > MAX_BARS) newCount = MAX_BARS;

    if ((int)newCount == oldCount) return;

     
    g_Seq.visibleBarCount = (int)newCount;

     
    if ((int)newCount > oldCount) {
        for (int b = oldCount; b < (int)newCount && b < MAX_BARS; ++b) {
            bar_bit_set(&g_Seq.barDirty, b);
            bar_bit_clear(&g_Seq.barValid, b);
        }
    } else {
        // Bar count reduced: trim or delete clips that now extend past the
        // new timeline boundary so no clip overhangs into the void beyond the
        // last bar. A clip fully past the boundary is removed entirely.
        float maxBeats = total_beats();
        seq_lock();
        for (int i = 0; i < g_Seq.clipCount; ) {
            Clip* c = &g_Seq.clips[i];
            if (c->startBeat >= maxBeats) {
                // Clip starts beyond the new boundary: delete it.
                if (g_ClipGran[i].ownFrames) {
                    free(g_ClipGran[i].ownFrames);
                    g_ClipGran[i].ownFrames = NULL;
                }
                for (int j = i; j < g_Seq.clipCount - 1; ++j) {
                    g_Seq.clips[j] = g_Seq.clips[j + 1];
                    g_ClipGran[j] = g_ClipGran[j + 1];
                    g_ClipGran[j].clipIdx = j;
                }
                g_Seq.clipCount--;
            } else if (c->startBeat + c->lengthBeats > maxBeats) {
                // Clip straddles the boundary: clamp its length.
                c->lengthBeats = maxBeats - c->startBeat;
                if (c->fadeInBeats > c->lengthBeats)  c->fadeInBeats = c->lengthBeats;
                if (c->fadeOutBeats > c->lengthBeats) c->fadeOutBeats = c->lengthBeats;
                ++i;
            } else {
                ++i;
            }
        }
        seq_unlock();
    }

     
    cseq_clip_structure_changed();
    invalidate_timeline_cache();

    double beats = total_beats();
    double fpb = frames_per_beat(g_Seq.bpm);
    if (beats > 0.0 && fpb > 0.0) {
        double total = beats * fpb;
        if (total > (double)UINT64_MAX) total = (double)UINT64_MAX;
        ma_uint64 loopTotalFrames = (ma_uint64)total;
        if (loopTotalFrames > 0) {
            LONG cur = InterlockedCompareExchange(&g_Seq.playbackFrame, 0, 0);
            InterlockedExchange(&g_Seq.playbackFrame, (LONG)((ma_uint64)cur % loopTotalFrames));
        }
    }

    if (g_hWnd) {
        update_scrollbar(g_hWnd);
        InvalidateRect(g_hWnd, NULL, FALSE);
    }
}

 
static inline void change_bar_count_even4(int delta) {
    float bpb = beats_per_bar();
    int barsPerStep = (bpb >= 1.0f) ? (int)(bpb + 0.5f) : 1;
    if (barsPerStep < 1) barsPerStep = 1;
    int aligned = (g_Seq.visibleBarCount / barsPerStep) * barsPerStep;
    if (delta >= 0) {
        change_bar_count((aligned + delta) - g_Seq.visibleBarCount);
    }
    else {
        if (aligned < barsPerStep) return;
        int target = aligned + delta;
        if (target < barsPerStep) target = barsPerStep;
        change_bar_count(target - g_Seq.visibleBarCount);
    }
}
