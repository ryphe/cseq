#pragma once
// Disk-backed PCM cache for the shared sample pool.
//
// Decoded float PCM is stored once in a content-addressed cache file and then
// memory-mapped, so the OS pages it in/out under memory pressure instead of the
// app holding the full buffer resident. The realtime audio thread keeps
// dereferencing pFrames[i] as plain memory reads; only the load thread touches
// disk. PCM is immutable once loaded, so the cache is write-once / read-many.
//
// Cache file layout:  [ma_uint64 frameCount][float interleaved-stereo PCM]
// The file is named by the FNV-1a hash of the PCM bytes: <hash>.pcm

#include "types.h"
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Cap on total on-disk cache size; evict oldest files (by last-write time)
// until the cache fits under this on startup.
#define SAMPLE_CACHE_MAX_BYTES (4ull * 1024 * 1024 * 1024)

// FNV-1a 64 over raw PCM bytes. Iterated over unsigned char* so there is no
// strict-aliasing concern with the float source.
static inline uint64_t sample_hash_pcm(const float* pcm, size_t bytes) {
    const unsigned char* p = (const unsigned char*)pcm;
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Resolves the cache directory (%LOCALAPPDATA%\cseq\cache\) and creates it if
// missing. Returns a pointer to a static buffer, or NULL on failure.
static inline const char* sample_cache_dir(void) {
    static char dir[MAX_PATH];
    static int  resolved = 0;
    if (resolved) return dir;

    WCHAR appData[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        if (!GetTempPathW(MAX_PATH, appData)) { resolved = 1; return NULL; }
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, appData, -1, dir, MAX_PATH, NULL, NULL);
    if (n <= 0) { resolved = 1; return NULL; }
    // Trim trailing backslash if present.
    size_t dl = strlen(dir);
    if (dl > 0 && dir[dl - 1] == '\\') dir[dl - 1] = '\0';
    if (dl + 12 >= MAX_PATH) { resolved = 1; return NULL; }
    strcat(dir, "\\cseq\\cache");
    // CreateDirectory only makes one level, so ensure the parent exists first.
    {
        char parent[MAX_PATH];
        strncpy(parent, dir, sizeof(parent) - 1);
        parent[sizeof(parent) - 1] = '\0';
        char* slash = strrchr(parent, '\\');
        if (slash) { *slash = '\0'; CreateDirectoryA(parent, NULL); }
        CreateDirectoryA(dir, NULL);    // ignore failure: may already exist
    }
    resolved = 1;
    return dir;
}

// Builds the absolute path to a cache file for a given hash into out (outCap
// bytes). Returns the path or NULL if the dir is unavailable.
static inline const char* sample_cache_path(uint64_t hash, char* out, size_t outCap) {
    const char* dir = sample_cache_dir();
    if (!dir) return NULL;
    if (snprintf(out, outCap, "%s\\%016llx.pcm", dir, (unsigned long long)hash) < 0)
        return NULL;
    return out;
}

// Maps the cache file for `hash`, returning the mapped base (float PCM) and the
// stored frameCount. Returns false if the file doesn't exist or can't be mapped.
static inline bool sample_cache_open(uint64_t hash, ma_uint64* outFrameCount,
                                     float** outMap, HANDLE* outMapHandle) {
    char path[MAX_PATH];
    if (!sample_cache_path(hash, path, sizeof(path))) return false;

    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile);
    if (!hMap) return false;

    void* base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); return false; }

    ma_uint64 fc;
    memcpy(&fc, base, sizeof(fc));
    *outFrameCount = fc;
    *outMap = (float*)((unsigned char*)base + sizeof(fc));
    *outMapHandle = hMap;
    return true;
}

// Writes `frameCount` interleaved float PCM to the cache file for `hash`.
// Write-once: if the file already exists it is left untouched. Returns true on
// success (or if the file already exists).
static inline bool sample_cache_store(uint64_t hash, const float* pcm, ma_uint64 frameCount) {
    char path[MAX_PATH];
    if (!sample_cache_path(hash, path, sizeof(path))) return false;

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // Already exists (or dir problem). If it exists, treat as success.
        return GetLastError() == ERROR_FILE_EXISTS;
    }

    ma_uint64 fc = frameCount;
    size_t pcmBytes = (size_t)frameCount * sizeof(float) * 2u;
    DWORD written = 0;
    bool ok = WriteFile(hFile, &fc, sizeof(fc), &written, NULL) && written == sizeof(fc);
    if (ok) ok = WriteFile(hFile, pcm, (DWORD)pcmBytes, &written, NULL) &&
                written == (DWORD)pcmBytes;
    CloseHandle(hFile);
    return ok;
}

// Takes a heap-allocated PCM buffer (owned by the caller) and installs it as a
// memory-mapped cache-backed sample. Stores the PCM in the cache, maps it back,
// and frees the caller's heap buffer. On any failure the caller's buffer is
// kept as the resident pFrames (heap-backed) so loading still works. `hash` is
// the caller-computed content hash (from sample_hash_pcm).
static inline void sample_install_cached(AudioSample* s, float* heapPcm,
                                         ma_uint64 frameCount, uint64_t hash) {
    float* map = NULL;
    HANDLE hMap = NULL;
    ma_uint64 storedFrames = 0;

    if (sample_cache_store(hash, heapPcm, frameCount) &&
        sample_cache_open(hash, &storedFrames, &map, &hMap) &&
        storedFrames == frameCount) {
        // Cache path succeeded: swap in the mapping and release the heap copy.
        s->pFrames = map;
        s->hCacheMap = hMap;
        s->contentHash = hash;
        free(heapPcm);
    } else {
        // Cache unavailable (no dir / disk error): keep the heap buffer.
        s->pFrames = heapPcm;
        s->hCacheMap = NULL;
        s->contentHash = hash;
    }
}

// Releases a sample's PCM. If it was mapped from the cache (hCacheMap non-NULL)
// it is unmapped; otherwise it is treated as a heap buffer and freed. Always
// clears pFrames. Safe to call on any sample (mapped, heap, or empty).
static inline void sample_unmap(AudioSample* s) {
    if (!s) return;
    if (s->hCacheMap) {
        if (s->pFrames) UnmapViewOfFile((void*)s->pFrames);
        CloseHandle(s->hCacheMap);
        s->hCacheMap = NULL;
    } else if (s->pFrames) {
        free(s->pFrames);       // heap-backed
    }
    s->pFrames = NULL;
}

// Startup maintenance: if the cache directory exceeds SAMPLE_CACHE_MAX_BYTES,
// delete the oldest *.pcm files (by last-write time) until it fits.
static inline void sample_cache_evict_if_large(void) {
    const char* dir = sample_cache_dir();
    if (!dir) return;

    char pattern[MAX_PATH];
    if (snprintf(pattern, sizeof(pattern), "%s\\*.pcm", dir) < 0) return;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    typedef struct { char path[MAX_PATH]; FILETIME wt; uint64_t size; } CacheEntry;
    // ~140 KB would blow a worker-thread stack; keep it on the heap.
    CacheEntry* files = (CacheEntry*)malloc(sizeof(CacheEntry) * 512);
    if (!files) { FindClose(hFind); return; }
    int count = 0;
    uint64_t total = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        total += size;
        if (count < 512) {
            snprintf(files[count].path, sizeof(files[count].path), "%s\\%s", dir, fd.cFileName);
            files[count].wt = fd.ftLastWriteTime;
            files[count].size = size;
            count++;
        }
    } while (FindNextFileA(hFind, &fd) != 0);
    FindClose(hFind);

    if (total <= SAMPLE_CACHE_MAX_BYTES) { free(files); return; }

    // Selection sort by last-write time (oldest first), deleting until under
    // the cap. The file count is small (capped at 512) so O(n^2) is fine.
    for (int i = 0; i < count && total > SAMPLE_CACHE_MAX_BYTES; ++i) {
        int oldest = i;
        for (int j = i + 1; j < count; ++j)
            if (CompareFileTime(&files[j].wt, &files[oldest].wt) < 0) oldest = j;
        if (oldest != i) {
            CacheEntry tmp = files[i];
            files[i] = files[oldest];
            files[oldest] = tmp;
        }
        DeleteFileA(files[i].path);
        if (total >= files[i].size) total -= files[i].size;
        else total = 0;
    }
    free(files);
}
