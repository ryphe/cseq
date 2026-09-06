#pragma once
#include "globals.h"
#include "codec.h"
#include "ui.h"
#include "state.h"
#include "actions.h"
#include "dialogs.h"
#include "samplecache.h"
#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

 
typedef struct {
    char magic[4];       
    int  version;        
    int  gridDivision;   
    int  masterMode;     
    int  timeSigNum;     
    int  timeSigDen;     
} CSQExtHeader;

#define CSQ_EXT_MAGIC   "CSQX"
#define CSQ_EXT_VERSION 2

// --- SoundFont path resolution helpers -------------------------------------

// Derive the directory portion of a project file path (with trailing slash;
// empty if the path has no separator).
static inline void cseq_project_dir_of(const char* projectFilePath, char* outDir, size_t outSize) {
    if (!outDir || outSize == 0) return;
    outDir[0] = '\0';
    if (!projectFilePath || !projectFilePath[0]) return;
    strncpy(outDir, projectFilePath, outSize - 1);
    outDir[outSize - 1] = '\0';
    char* lastSlash = strrchr(outDir, '\\');
    if (!lastSlash) lastSlash = strrchr(outDir, '/');
    if (lastSlash) *(lastSlash + 1) = '\0';
    else outDir[0] = '\0';
}

// Copy the filename portion of a path (handles both slash flavors).
static inline void cseq_path_basename(const char* p, char* outName, size_t outSize) {
    if (!outName || outSize == 0) return;
    outName[0] = '\0';
    if (!p || !p[0]) return;
    const char* base = strrchr(p, '\\');
    const char* fwd = strrchr(p, '/');
    if (fwd && (!base || fwd > base)) base = fwd;
    base = base ? base + 1 : p;
    strncpy(outName, base, outSize - 1);
    outName[outSize - 1] = '\0';
}

static inline bool cseq_file_exists(const char* p) {
    return p && p[0] && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

// Compute a path to targetPath relative to the project file's directory.
// Falls back to the bare filename if the volumes differ or the relative
// computation fails.
static inline void cseq_build_relative_path(const char* projectFilePath, const char* targetPath,
                                            char* outRel, size_t outSize) {
    if (!outRel || outSize == 0) return;
    outRel[0] = '\0';
    if (!projectFilePath || !targetPath || !targetPath[0]) return;

    char projDir[MAX_PATH];
    cseq_project_dir_of(projectFilePath, projDir, sizeof(projDir));

    if (projDir[0] &&
        PathRelativePathToA(outRel, projDir, FILE_ATTRIBUTE_DIRECTORY,
                            targetPath, FILE_ATTRIBUTE_NORMAL)) {
        return;
    }

    cseq_path_basename(targetPath, outRel, outSize);
}

// Resolve a saved SoundFont reference to an existing file on disk. Probe
// order: (1) relPath under the project directory, (2) absPath directly,
// (3) the target filename in the project directory, (4) the filename in a
// "soundfonts\" subdirectory, (5) the filename in an "instruments\"
// subdirectory. Returns true and fills outResolved on the first hit.
static inline bool cseq_resolve_soundfont_path(const char* projectFilePath,
                                               const char* relPath,
                                               const char* absPath,
                                               char* outResolved,
                                               size_t outSize) {
    if (!outResolved || outSize == 0) return false;
    outResolved[0] = '\0';

    char projDir[MAX_PATH];
    cseq_project_dir_of(projectFilePath, projDir, sizeof(projDir));

    // 1. Relative path from the project directory (survives folder moves)
    if (relPath && relPath[0] && projDir[0]) {
        char cand[MAX_PATH];
        if (PathCombineA(cand, projDir, relPath) && cseq_file_exists(cand)) {
            strncpy(outResolved, cand, outSize - 1);
            outResolved[outSize - 1] = '\0';
            return true;
        }
    }

    // 2. Absolute path as saved
    if (absPath && absPath[0] && cseq_file_exists(absPath)) {
        strncpy(outResolved, absPath, outSize - 1);
        outResolved[outSize - 1] = '\0';
        return true;
    }

    // 3-5. Filename-only fallbacks keyed off whichever reference we have
    const char* src = (relPath && relPath[0]) ? relPath : absPath;
    char baseName[MAX_PATH];
    cseq_path_basename(src, baseName, sizeof(baseName));

    if (baseName[0] && projDir[0]) {
        char dir[MAX_PATH];
        char cand[MAX_PATH];

        // 3. Same directory as the project file
        if (PathCombineA(cand, projDir, baseName) && cseq_file_exists(cand)) {
            strncpy(outResolved, cand, outSize - 1);
            outResolved[outSize - 1] = '\0';
            return true;
        }

        // 4. soundfonts\ subdirectory
        if (PathCombineA(dir, projDir, "soundfonts") &&
            PathCombineA(cand, dir, baseName) && cseq_file_exists(cand)) {
            strncpy(outResolved, cand, outSize - 1);
            outResolved[outSize - 1] = '\0';
            return true;
        }

        // 5. instruments\ subdirectory
        if (PathCombineA(dir, projDir, "instruments") &&
            PathCombineA(cand, dir, baseName) && cseq_file_exists(cand)) {
            strncpy(outResolved, cand, outSize - 1);
            outResolved[outSize - 1] = '\0';
            return true;
        }
    }

    return false;
}

 
static inline int cseq_sanitize_grid_division(int div) {
    switch (div) {
        case GRID_1_16:
        case GRID_1_16T:
        case GRID_1_32:
        case GRID_1_32T:
            return div;
        default:
            return GRID_1_16;
    }
}

 
static bool write_compressed_sample(FILE* fp, const AudioSample* sample, int sampleIndex, int totalSamples) {
    CSQSampleHeader shdr = { 0 };
    if (sample && sample->name[0]) {
        strncpy(shdr.name, sample->name, sizeof(shdr.name) - 1);
    }
    shdr.frameCount = (sample && sample->loaded && sample->pFrames) ? sample->frameCount : 0;
    size_t rawSize = (size_t)shdr.frameCount * sizeof(float) * NUM_CHANNELS;
    shdr.rawBytes = (DWORD)rawSize;

    if (rawSize > 0 && sample->pFrames) {
        size_t compSize = 0;
        unsigned char* compData = csq_compress_lz((const unsigned char*)sample->pFrames, rawSize, &compSize);
        if (compData && compSize > 0 && compSize < rawSize) {
            shdr.compBytes = (DWORD)compSize;
            fwrite(&shdr, sizeof(shdr), 1, fp);
            fwrite(compData, 1, compSize, fp);
            free(compData);
        }
        else {
            if (compData) free(compData);
            shdr.compBytes = (DWORD)rawSize;
            fwrite(&shdr, sizeof(shdr), 1, fp);
            fwrite(sample->pFrames, 1, rawSize, fp);
        }
    }
    else {
        shdr.rawBytes = 0;
        shdr.compBytes = 0;
        fwrite(&shdr, sizeof(shdr), 1, fp);
    }

    if (totalSamples > 0) {
        job_set_progress(10 + (int)(((float)(sampleIndex + 1) / (float)totalSamples) * 85.0f));
    }
    return true;
}

 
static void write_gran_engine(FILE* fp, const GranularEngine* e, int idx) {
    CSQGranEngineHeader geh = { 0 };
    geh.trackIdx = idx;
    geh.enabled = e->enabled ? 1 : 0;
    geh.grainSizeMs = e->grainSizeMs;
    geh.density = e->density;
    geh.position = e->position;
    geh.posJitter = e->posJitter;
    geh.pitch = e->pitch;
    geh.pitchJitter = e->pitchJitter;
    geh.panSpread = e->panSpread;
    geh.attack = e->attack;
    geh.release = e->release;
    geh.volume = e->volume;
    geh.freeze = e->freeze ? 1 : 0;
    geh.droneMode = e->droneMode ? 1 : 0;
    geh.sampleIndex = e->sampleIndex;
    geh.octaveShift = e->octaveShift;
    geh.noteCount = (e->noteCount < 0) ? 0 : (e->noteCount > GRAN_MAX_NOTES ? GRAN_MAX_NOTES : e->noteCount);
    geh.ownLoaded = (e->ownLoaded && e->ownFrames && e->ownFrameCount >= 256) ? 1 : 0;

    fwrite(&geh, sizeof(geh), 1, fp);

    for (int n = 0; n < geh.noteCount; ++n) {
        CSQGranNote gn = { 0 };
        gn.active = e->notes[n].active ? 1 : 0;
        gn.midiNote = e->notes[n].midiNote;
        gn.velocity = e->notes[n].velocity;
        gn.startBeat = e->notes[n].startBeat;
        gn.lengthBeats = e->notes[n].lengthBeats;
        fwrite(&gn, sizeof(gn), 1, fp);
    }

    if (geh.ownLoaded) {
        AudioSample tmp = { 0 };
        strncpy(tmp.name, e->ownSampleName, sizeof(tmp.name) - 1);
        tmp.pFrames = e->ownFrames;
        tmp.frameCount = e->ownFrameCount;
        tmp.loaded = true;
        write_compressed_sample(fp, &tmp, 0, 1);
    }
}

 
static bool read_gran_engine(FILE* fp, GranularEngine* e, const int* loadRemap) {
    CSQGranEngineHeader geh = { 0 };
    if (fread(&geh, sizeof(geh), 1, fp) != 1) return false;
    if (geh.noteCount < 0 || geh.noteCount > GRAN_MAX_NOTES) return false;

    e->enabled = geh.enabled != 0;
    e->grainSizeMs = geh.grainSizeMs;
    e->density = geh.density;
    e->position = geh.position;
    e->posJitter = geh.posJitter;
    e->pitch = geh.pitch;
    e->pitchJitter = geh.pitchJitter;
    e->panSpread = geh.panSpread;
    e->attack = geh.attack;
    e->release = geh.release;
    e->volume = geh.volume;
    e->freeze = geh.freeze != 0;
    e->droneMode = geh.droneMode != 0;
    // The file stores compacted sample indices; map through loadRemap so the
    // engine references the sample that was actually loaded (or none if dropped).
    e->sampleIndex = (geh.sampleIndex >= 0 && loadRemap && geh.sampleIndex < MAX_SAMPLES)
                     ? loadRemap[geh.sampleIndex] : -1;
    e->octaveShift = geh.octaveShift;
    e->noteCount = 0;
    e->ownLoaded = false;
    e->ownFrames = NULL;
    e->ownFrameCount = 0;
    e->spawnAcc = 0.0f;
    e->auditionNote = 0;
    memset(e->grains, 0, sizeof(e->grains));
    memset(&e->ownSample, 0, sizeof(AudioSample));

    for (int n = 0; n < geh.noteCount; ++n) {
        CSQGranNote gn = { 0 };
        if (fread(&gn, sizeof(gn), 1, fp) != 1) return false;
        if (e->noteCount >= GRAN_MAX_NOTES) continue;
        GranNote* dst = &e->notes[e->noteCount++];
        dst->active = gn.active != 0;
        dst->midiNote = gn.midiNote;
        dst->velocity = gn.velocity;
        dst->startBeat = gn.startBeat;
        dst->lengthBeats = gn.lengthBeats;
        dst->isSelected = false;
    }

    if (geh.ownLoaded) {
        CSQSampleHeader shdr = { 0 };
        if (fread(&shdr, sizeof(shdr), 1, fp) != 1) return false;

        if (shdr.rawBytes == 0 || shdr.frameCount == 0 || shdr.rawBytes > 256 * 1024 * 1024 || shdr.compBytes > 256 * 1024 * 1024) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            return true;
        }
        // Corrupt-header guard: frameCount and rawBytes must agree, or the
        // own-sample PCM read / ownFrameCount would index out of bounds.
        if ((ma_uint64)shdr.rawBytes != (ma_uint64)shdr.frameCount * sizeof(float) * NUM_CHANNELS) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            return true;
        }

        size_t rawSize = (size_t)shdr.rawBytes;
        float* buf = (float*)malloc(rawSize);
        if (!buf) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            return false;
        }

        if (shdr.compBytes == shdr.rawBytes) {
            if (fread(buf, 1, rawSize, fp) != rawSize) {
                free(buf);
                return false;
            }
        }
        else {
            unsigned char* comp = (unsigned char*)malloc(shdr.compBytes);
            if (!comp) {
                free(buf);
                fseek(fp, (long)shdr.compBytes, SEEK_CUR);
                return false;
            }
            if (fread(comp, 1, shdr.compBytes, fp) != shdr.compBytes) {
                free(comp);
                free(buf);
                return false;
            }
            if (!csq_decompress_lz(comp, shdr.compBytes, (unsigned char*)buf, rawSize))
                memset(buf, 0, rawSize);
            free(comp);
        }

        e->ownFrames = buf;
        e->ownFrameCount = shdr.frameCount;
        e->ownLoaded = true;
        strncpy(e->ownSampleName, shdr.name, sizeof(e->ownSampleName) - 1);
        e->ownSampleName[sizeof(e->ownSampleName) - 1] = '\0';
    }
    return true;
}

 
static void build_sample_remap(const SequencerState* st,
                               const GranularEngine* trackGran,
                               const GranularEngine* clipGran,
                               int* outRemap,
                               int* outUsedCount)
{
    bool used[MAX_SAMPLES];
    memset(used, 0, sizeof(used));
    memset(outRemap, -1, sizeof(int) * MAX_SAMPLES);
    *outUsedCount = 0;

    for (int c = 0; c < st->clipCount; ++c) {
        int si = st->clips[c].sampleIndex;
        if (si >= 0 && si < st->sampleCount && si < MAX_SAMPLES)
            used[si] = true;
    }
    for (int t = 0; t < st->trackCount && t < MAX_TRACKS; ++t) {
        const GranularEngine* e = &trackGran[t];
        if ((e->enabled || e->noteCount > 0 || e->ownLoaded) &&
            e->sampleIndex >= 0 && e->sampleIndex < st->sampleCount && e->sampleIndex < MAX_SAMPLES)
            used[e->sampleIndex] = true;
    }
    for (int c = 0; c < st->clipCount && c < MAX_CLIPS; ++c) {
        if (!st->clips[c].isGranular) continue;
        const GranularEngine* e = &clipGran[c];
        if ((e->enabled || e->noteCount > 0 || e->ownLoaded) &&
            e->sampleIndex >= 0 && e->sampleIndex < st->sampleCount && e->sampleIndex < MAX_SAMPLES)
            used[e->sampleIndex] = true;
    }
    for (int s = 0; s < st->sampleCount && s < MAX_SAMPLES; ++s) {
        if (used[s])
            outRemap[s] = (*outUsedCount)++;
    }
}

 
static DWORD WINAPI SaveProjectThreadProc(LPVOID lpParam) {
    (void)lpParam;
    const char* path = g_Seq.jobPath;
    BOOL success = FALSE;
    FILE* fp = NULL;

    SequencerState* pStateCopy = (SequencerState*)malloc(sizeof(SequencerState));
    GranularEngine* pTrackGranCopy = (GranularEngine*)malloc(sizeof(GranularEngine) * MAX_TRACKS);
    GranularEngine* pClipGranCopy = (GranularEngine*)malloc(sizeof(GranularEngine) * MAX_CLIPS);
    FxChain* pTrackFxCopy = (FxChain*)malloc(sizeof(FxChain) * MAX_TRACKS);

    if (!pStateCopy || !pTrackGranCopy || !pClipGranCopy || !pTrackFxCopy) {
        cseq_report_error(g_hWnd, "Save Error", "Out of memory while initiating project save.");
        goto cleanup;
    }

    memset(pTrackGranCopy, 0, sizeof(GranularEngine) * MAX_TRACKS);
    memset(pClipGranCopy, 0, sizeof(GranularEngine) * MAX_CLIPS);
    memset(pTrackFxCopy, 0, sizeof(FxChain) * MAX_TRACKS);

    seq_lock();
    *pStateCopy = g_Seq;
    memcpy(pTrackGranCopy, g_TrackGran, sizeof(GranularEngine) * MAX_TRACKS);
    memcpy(pClipGranCopy, g_ClipGran, sizeof(GranularEngine) * MAX_CLIPS);

    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (pTrackGranCopy[t].ownLoaded && pTrackGranCopy[t].ownFrames && pTrackGranCopy[t].ownFrameCount > 0) {
            size_t sz = (size_t)pTrackGranCopy[t].ownFrameCount * sizeof(float) * NUM_CHANNELS;
            float* copy = (float*)malloc(sz);
            if (copy) {
                memcpy(copy, pTrackGranCopy[t].ownFrames, sz);
                pTrackGranCopy[t].ownFrames = copy;
            } else {
                pTrackGranCopy[t].ownLoaded = false;
                pTrackGranCopy[t].ownFrames = NULL;
                pTrackGranCopy[t].ownFrameCount = 0;
            }
        } else {
            pTrackGranCopy[t].ownFrames = NULL;
            pTrackGranCopy[t].ownLoaded = false;
        }
    }
    for (int c = 0; c < MAX_CLIPS; ++c) {
        pClipGranCopy[c].ownFrames = NULL;
        pClipGranCopy[c].ownLoaded = false;
    }
    for (int t = 0; t < MAX_TRACKS; ++t) {
         
        FxChain* d = &pTrackFxCopy[t];
        const FxChain* s = &g_TrackFx[t];
        d->count = (s->count >= 0 && s->count <= FX_MAX_SLOTS) ? s->count : 0;
        for (int i = 0; i < FX_MAX_SLOTS; ++i) {
            d->slots[i].desc = s->slots[i].desc;
            memcpy(d->slots[i].params, s->slots[i].params, sizeof(d->slots[i].params));
            d->slots[i].state = NULL;
        }
    }
    seq_unlock();

     
    fp = fopen_utf8(path, L"wb");
    if (!fp) {
        cseq_report_error(g_hWnd, "Save Error", "Could not open file for writing.");
        goto cleanup;
    }

    int sampleRemap[MAX_SAMPLES];
    int usedSampleCount = 0;
    build_sample_remap(pStateCopy, pTrackGranCopy, pClipGranCopy, sampleRemap, &usedSampleCount);

     
    CSQHeader hdr = { 0 };
    memcpy(hdr.magic, CSQ_MAGIC, 4);
    hdr.bpm             = pStateCopy->bpm;
    hdr.swing           = pStateCopy->swing;
    hdr.barCount        = pStateCopy->visibleBarCount;    
    hdr.trackCount      = pStateCopy->trackCount;
    hdr.sampleCount     = usedSampleCount;
    hdr.clipCount       = pStateCopy->clipCount;
    hdr.isLofi          = pStateCopy->isLofi ? 1 : 0;
    hdr.quantizeEnabled = pStateCopy->quantizeEnabled ? 1 : 0;
    {
         
        int storedBars = 0;
        int clipLimit = (pStateCopy->clipCount > MAX_CLIPS) ? MAX_CLIPS : pStateCopy->clipCount;
        for (int c = 0; c < clipLimit; ++c) {
            int b0, b1;
            clip_bar_range(&pStateCopy->clips[c], &b0, &b1);
            if (b1 + 1 > storedBars) storedBars = b1 + 1;
        }
        if (storedBars < pStateCopy->visibleBarCount) storedBars = pStateCopy->visibleBarCount;
        if (storedBars > MAX_BARS) storedBars = MAX_BARS;
        hdr.storedBarCount  = storedBars;
        hdr.visibleBarCount = pStateCopy->visibleBarCount;
    }
    fwrite(&hdr, sizeof(hdr), 1, fp);

     
    {
        CSQExtHeader ext = { 0 };
        memcpy(ext.magic, CSQ_EXT_MAGIC, 4);
        ext.version      = CSQ_EXT_VERSION;
        ext.gridDivision = cseq_sanitize_grid_division(pStateCopy->gridDivision);
        ext.masterMode   = (pStateCopy->masterMode == 1) ? 1 : 0;
        ext.timeSigNum   = (pStateCopy->timeSigNum > 0) ? pStateCopy->timeSigNum : 4;
        ext.timeSigDen   = (pStateCopy->timeSigDen > 0) ? pStateCopy->timeSigDen : 4;
        fwrite(&ext, sizeof(ext), 1, fp);
    }

     
    for (int t = 0; t < pStateCopy->trackCount; ++t) {
        CSQTrack trk = { 0 };
        trk.trackIndex = t;
        trk.isMuted    = pStateCopy->trackMuted[t] ? 1 : 0;
        trk.volume     = pStateCopy->trackVolume[t];
        trk.eqLow      = pStateCopy->trackEqLow[t];
        trk.eqMid      = pStateCopy->trackEqMid[t];
        trk.eqHigh     = pStateCopy->trackEqHigh[t];
        memcpy(trk.eqFreq, pStateCopy->trackEqFreq[t], sizeof(float) * 3);
        memcpy(trk.eqQ,    pStateCopy->trackEqQ[t],    sizeof(float) * 3);
        fwrite(&trk, sizeof(trk), 1, fp);
    }

     
    for (int c = 0; c < pStateCopy->clipCount; ++c) {
        const Clip* clp = &pStateCopy->clips[c];
        CSQ3ClipEntry entry = { 0 };

        if (clp->sampleIndex >= 0 && clp->sampleIndex < MAX_SAMPLES && sampleRemap[clp->sampleIndex] >= 0)
            entry.sampleIndex = sampleRemap[clp->sampleIndex];
        else
            entry.sampleIndex = -1;

        entry.track              = clp->track;
        entry.startBeat          = clp->startBeat;
        entry.lengthBeats        = clp->lengthBeats;
        entry.sampleOffsetFrames = clp->sampleOffsetFrames;
        entry.volume             = clp->volume;
        entry.playbackRate       = clp->playbackRate;
        entry.fadeInBeats        = clp->fadeInBeats;
        entry.fadeOutBeats       = clp->fadeOutBeats;
        entry.isSelected         = clp->isSelected ? 1 : 0;
        entry.isGranular         = clp->isGranular ? 1 : 0;
        entry.isMuted            = clp->isMuted ? 1 : 0;
        entry.isMidi             = clp->isMidi ? 1 : 0;
        entry.midiNoteCount      = clp->midiNoteCount;

        fwrite(&entry, sizeof(CSQ3ClipEntry), 1, fp);
    }

     
    job_set_progress(5);
    int written = 0;
    for (int s = 0; s < pStateCopy->sampleCount; ++s) {
        if (sampleRemap[s] < 0) continue;
        write_compressed_sample(fp, &pStateCopy->samples[s], written, usedSampleCount);
        written++;
    }

     
    {
        CSQGranSection gsec = { 0 };
        gsec.version = 1;
        for (int t = 0; t < pStateCopy->trackCount && t < MAX_TRACKS; ++t) {
            const GranularEngine* e = &pTrackGranCopy[t];
            if (e->enabled || e->noteCount > 0 || e->ownLoaded) gsec.trackGranCount++;
        }
        for (int c = 0; c < pStateCopy->clipCount && c < MAX_CLIPS; ++c) {
            if (!pStateCopy->clips[c].isGranular) continue;
            const GranularEngine* e = &pClipGranCopy[c];
            if (e->enabled || e->noteCount > 0 || e->ownLoaded) gsec.clipGranCount++;
        }
        fwrite(&gsec, sizeof(gsec), 1, fp);

        for (int t = 0; t < pStateCopy->trackCount && t < MAX_TRACKS; ++t) {
            const GranularEngine* e = &pTrackGranCopy[t];
            if (e->enabled || e->noteCount > 0 || e->ownLoaded) {
                GranularEngine tmp = *e;
                if (tmp.sampleIndex >= 0 && tmp.sampleIndex < MAX_SAMPLES && sampleRemap[tmp.sampleIndex] >= 0)
                    tmp.sampleIndex = sampleRemap[tmp.sampleIndex];
                else
                    tmp.sampleIndex = -1;
                write_gran_engine(fp, &tmp, t);
            }
        }
        for (int c = 0; c < pStateCopy->clipCount && c < MAX_CLIPS; ++c) {
            if (!pStateCopy->clips[c].isGranular) continue;
            const GranularEngine* e = &pClipGranCopy[c];
            if (e->enabled || e->noteCount > 0 || e->ownLoaded) {
                GranularEngine tmp = *e;
                if (tmp.sampleIndex >= 0 && tmp.sampleIndex < MAX_SAMPLES && sampleRemap[tmp.sampleIndex] >= 0)
                    tmp.sampleIndex = sampleRemap[tmp.sampleIndex];
                else
                    tmp.sampleIndex = -1;
                write_gran_engine(fp, &tmp, c);
            }
        }
    }

     
    {
        CSQMidiSection midiSec = { 0 };
        memcpy(midiSec.magic, CSQ_MIDI_MAGIC, 4);
        midiSec.version = 2;    

        int midiClipCount = 0;
        for (int c = 0; c < pStateCopy->clipCount; ++c) {
            if (pStateCopy->clips[c].isMidi && pStateCopy->clips[c].midiNoteCount > 0)
                midiClipCount++;
        }
        midiSec.clipCount = midiClipCount;
        fwrite(&midiSec, sizeof(midiSec), 1, fp);

        for (int c = 0; c < pStateCopy->clipCount; ++c) {
            const Clip* clip = &pStateCopy->clips[c];
            if (!clip->isMidi || clip->midiNoteCount == 0) continue;

            CSQMidiClipEntry entry;
            entry.clipIndex = c;
            entry.noteCount = clip->midiNoteCount;
            fwrite(&entry, sizeof(entry), 1, fp);
            fwrite(clip->midiNotes, sizeof(MidiNote), entry.noteCount, fp);
        }
    }

     
    {
        CSQFadeSection fsec = { 0 };
        memcpy(fsec.magic, CSQ_FADE_MAGIC, 4);
        fsec.version   = CSQ_FADE_VERSION;
        fsec.clipCount = pStateCopy->clipCount;
        fwrite(&fsec, sizeof(fsec), 1, fp);
        for (int c = 0; c < pStateCopy->clipCount; ++c) {
            uint8_t types[2];
            types[0] = pStateCopy->clips[c].fadeInType;
            types[1] = pStateCopy->clips[c].fadeOutType;
            fwrite(types, 1, sizeof(types), fp);
        }
    }

     
    {
        CSQAdsrSection asec = { 0 };
        memcpy(asec.magic, CSQ_ADSR_MAGIC, 4);
        asec.version   = CSQ_ADSR_VERSION;
        asec.clipCount = pStateCopy->clipCount;
        fwrite(&asec, sizeof(asec), 1, fp);
        for (int c = 0; c < pStateCopy->clipCount; ++c) {
            float adsr[4];
            adsr[0] = pStateCopy->clips[c].adsrAttack;
            adsr[1] = pStateCopy->clips[c].adsrDecay;
            adsr[2] = pStateCopy->clips[c].adsrSustain;
            adsr[3] = pStateCopy->clips[c].adsrRelease;
            fwrite(adsr, sizeof(float), 4, fp);
        }
    }

     
    {
        CSQFxSection fxs = { 0 };
        memcpy(fxs.magic, CSQ_FX_MAGIC, 4);
        fxs.version    = CSQ_FX_VERSION;
        fxs.trackCount = (pStateCopy->trackCount >= 0 && pStateCopy->trackCount <= MAX_TRACKS)
                         ? pStateCopy->trackCount : 0;
        fwrite(&fxs, sizeof(fxs), 1, fp);
        for (int t = 0; t < fxs.trackCount && t < MAX_TRACKS; ++t) {
            CSQFxChain fc;
            memset(&fc, 0, sizeof(fc));
            fc.count = pTrackFxCopy[t].count;
            for (int i = 0; i < FX_MAX_SLOTS; ++i) {
                fc.slots[i] = pTrackFxCopy[t].slots[i].desc
                              ? pTrackFxCopy[t].slots[i].desc->type : FX_TYPE_NONE;
                memcpy(fc.params[i], pTrackFxCopy[t].slots[i].params, sizeof(fc.params[i]));
            }
            fwrite(&fc, sizeof(fc), 1, fp);
        }
    }

    // --- SoundFont external reference (CSQSoundFontSection, trailing) ---
    // Path descriptors + preset selection only; .sf2 bytes are never embedded.
    {
        char sfPath[MAX_PATH] = { 0 };
        char sfName[64] = { 0 };
        int sfPreset = 0;
        bool hasSFont = false;

        sfont_init_lock();
        EnterCriticalSection(&g_SFont.lock);
        if (g_SFont.synth && g_SFont.path[0]) {
            strncpy(sfPath, g_SFont.path, MAX_PATH - 1);
            sfPath[MAX_PATH - 1] = '\0';
            strncpy(sfName, g_SFont.name, sizeof(sfName) - 1);
            sfName[sizeof(sfName) - 1] = '\0';
            sfPreset = g_SFont.activePreset;
            hasSFont = true;
        }
        LeaveCriticalSection(&g_SFont.lock);

        if (hasSFont) {
            CSQSoundFontSection sfs;
            memset(&sfs, 0, sizeof(sfs));
            memcpy(sfs.magic, CSQ_SFONT_MAGIC, 4);
            sfs.version      = CSQ_SFONT_VERSION;
            sfs.activePreset = sfPreset;
            strncpy(sfs.absPath, sfPath, sizeof(sfs.absPath) - 1);
            sfs.absPath[sizeof(sfs.absPath) - 1] = '\0';
            strncpy(sfs.name, sfName, sizeof(sfs.name) - 1);
            sfs.name[sizeof(sfs.name) - 1] = '\0';
            cseq_build_relative_path(path, sfPath, sfs.relPath, sizeof(sfs.relPath));
            sfs.flags = 0;

            fwrite(&sfs, sizeof(sfs), 1, fp);
        }
    }

    // --- Track Filter Plotter section (CSQV, trailing, optional) ---
    // Parameters only: coefficients/magnitude are recomputed on load.
    {
        CSQFilterSection vfs;
        memset(&vfs, 0, sizeof(vfs));
        memcpy(vfs.magic, CSQ_FILTER_MAGIC, 4);
        vfs.version    = CSQ_FILTER_VERSION;
        vfs.trackCount = (pStateCopy->trackCount >= 0 && pStateCopy->trackCount <= MAX_TRACKS)
                         ? pStateCopy->trackCount : 0;
        fwrite(&vfs, sizeof(vfs), 1, fp);
        for (int t = 0; t < vfs.trackCount && t < MAX_TRACKS; ++t) {
            CSQFilterTrack vf;
            memset(&vf, 0, sizeof(vf));
            vf.typeMask  = (int32_t)pStateCopy->trackFilter[t].typeMask;
            vf.frequency = pStateCopy->trackFilter[t].frequency;
            vf.q         = pStateCopy->trackFilter[t].q;
            vf.enabled   = pStateCopy->trackFilter[t].enabled ? 1 : 0;
            fwrite(&vf, sizeof(vf), 1, fp);
        }
    }

    // --- Synth Clip Kind section (CSQY, trailing, optional) ---
    // Notes ride the MIDI section; this only marks which clips are
    // Quadrum/Halo synth modules.
    {
        CSQSynthSection ysec;
        memset(&ysec, 0, sizeof(ysec));
        memcpy(ysec.magic, CSQ_SYNTH_MAGIC, 4);
        ysec.version = CSQ_SYNTH_VERSION;
        int clipLimit = (pStateCopy->clipCount > MAX_CLIPS) ? MAX_CLIPS : pStateCopy->clipCount;
        for (int c = 0; c < clipLimit; ++c) {
            if (pStateCopy->clips[c].clipKind != CLIP_KIND_SAMPLE) ysec.count++;
        }
        fwrite(&ysec, sizeof(ysec), 1, fp);
        for (int c = 0; c < clipLimit; ++c) {
            if (pStateCopy->clips[c].clipKind == CLIP_KIND_SAMPLE) continue;
            CSQSynthClip ys;
            memset(&ys, 0, sizeof(ys));
            ys.clipIndex = c;
            ys.clipKind  = pStateCopy->clips[c].clipKind;
            ys.synthAttack  = pStateCopy->clips[c].synthAttack;
            ys.synthDecay   = pStateCopy->clips[c].synthDecay;
            ys.synthSustain = pStateCopy->clips[c].synthSustain;
            ys.synthRelease = pStateCopy->clips[c].synthRelease;
            memcpy(ys.quadrumParams, pStateCopy->clips[c].quadrumParams, sizeof(ys.quadrumParams));
            ys.haloPatch = pStateCopy->clips[c].haloPatch;
            fwrite(&ys, sizeof(ys), 1, fp);
        }
    }

    // --- Track Trigger Probability section (CSQP, trailing, optional) ---
    {
        CSQProbSection psec;
        memset(&psec, 0, sizeof(psec));
        memcpy(psec.magic, CSQ_PROB_MAGIC, 4);
        psec.version     = CSQ_PROB_VERSION;
        psec.trackCount  = pStateCopy->trackCount;
        fwrite(&psec, sizeof(psec), 1, fp);
        for (int t = 0; t < pStateCopy->trackCount && t < MAX_TRACKS; ++t) {
            CSQProbTrack pt;
            memset(&pt, 0, sizeof(pt));
            pt.trackIndex = t;
            pt.prob       = pStateCopy->trackTriggerProb[t];
            pt.rngSeed    = pStateCopy->trackRngState[t];
            fwrite(&pt, sizeof(pt), 1, fp);
        }
    }

    // --- Master Audio Params section (CSQM, trailing, optional) ---
    // Persists the master/lofi/export settings so a reloaded project restores
    // the same mix/export characteristics (previously lost on reload).
    {
        CSQMasterSection ms;
        memset(&ms, 0, sizeof(ms));
        memcpy(ms.magic, CSQ_MASTER_MAGIC, 4);
        ms.version         = CSQ_MASTER_VERSION;
        ms.masterVolume    = pStateCopy->masterVolume;
        ms.lofiBitDepth    = pStateCopy->lofiBitDepth;
        ms.lofiSampleRate  = pStateCopy->lofiSampleRate;
        ms.exportBitDepth  = pStateCopy->exportBitDepth;
        fwrite(&ms, sizeof(ms), 1, fp);
    }

    // --- Track Mix section (CSQT, trailing, optional) ---
    // Persists per-track pan/width/solo, which the legacy CSQTrack record does
    // not carry, so a reloaded project keeps its mix state.
    {
        CSQTrackMixSection tms;
        memset(&tms, 0, sizeof(tms));
        memcpy(tms.magic, CSQ_TRACKMIX_MAGIC, 4);
        tms.version    = CSQ_TRACKMIX_VERSION;
        tms.trackCount = pStateCopy->trackCount;
        fwrite(&tms, sizeof(tms), 1, fp);
        for (int t = 0; t < pStateCopy->trackCount && t < MAX_TRACKS; ++t) {
            CSQTrackMix tm;
            memset(&tm, 0, sizeof(tm));
            tm.trackIndex = t;
            tm.pan        = pStateCopy->trackPan[t];
            tm.width      = pStateCopy->trackWidth[t];
            tm.solo       = pStateCopy->trackSolo[t] ? 1 : 0;
            tm.sidechainSource = pStateCopy->trackSidechainSource[t];
            fwrite(&tm, sizeof(tm), 1, fp);
        }
    }

    fclose(fp);
    fp = NULL;
    success = TRUE;

    job_set_progress(100);
    {
        const char* base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        strncpy(g_Seq.currentProjectName, base, sizeof(g_Seq.currentProjectName) - 1);
        strncpy(g_Seq.currentProjectFile, path, sizeof(g_Seq.currentProjectFile) - 1);
        g_Seq.isModified = false;
        update_window_title();
    }
    job_end("Project saved to .csq successfully.");

cleanup:
    if (!success && g_Seq.isBusy && g_Seq.jobKind == 1)
        job_end(NULL);

    if (fp) fclose(fp);

    if (pTrackGranCopy) {
        for (int t = 0; t < MAX_TRACKS; ++t) {
            if (pTrackGranCopy[t].ownFrames) {
                free(pTrackGranCopy[t].ownFrames);
                pTrackGranCopy[t].ownFrames = NULL;
            }
        }
        free(pTrackGranCopy);
    }
    if (pClipGranCopy) free(pClipGranCopy);
    if (pTrackFxCopy) free(pTrackFxCopy);
    if (pStateCopy) free(pStateCopy);
    return success ? 0 : 1;
}

static inline void save_project_to_csq(const char* path) {
    if (!job_begin(1, path)) return;
    HANDLE hThread = CreateThread(NULL, 0, SaveProjectThreadProc, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
    else job_end(NULL);
}

 
static DWORD WINAPI LoadProjectThreadProc(LPVOID lpParam) {
    (void)lpParam;
    const char* path = g_Seq.jobPath;
     
    FILE* fp = fopen_utf8(path, L"rb");
    if (!fp) {
        cseq_report_error(g_hWnd, "Load Error", "Could not open .csq project file.");
        job_end(NULL);
        return 1;
    }

    clear_clipboard();

     
    CSQHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (fread(&hdr, CSQ_HEADER_LEGACY_SIZE, 1, fp) != 1 ||
        (memcmp(hdr.magic, "CSQ1", 4) != 0 &&
         memcmp(hdr.magic, "CSQ2", 4) != 0 &&
         memcmp(hdr.magic, "CSQ3", 4) != 0 &&
         memcmp(hdr.magic, CSQ_MAGIC, 4) != 0)) {
        fclose(fp);
        cseq_report_error(g_hWnd, "Load Error", "Invalid or unsupported .csq file format.");
        job_end(NULL);
        return 1;
    }

    bool isCSQ4 = (memcmp(hdr.magic, CSQ_MAGIC, 4) == 0);
    bool isCSQ3 = (memcmp(hdr.magic, "CSQ3", 4) == 0);
    bool isCSQ2 = (memcmp(hdr.magic, "CSQ2", 4) == 0);
    bool isCSQ1 = (memcmp(hdr.magic, "CSQ1", 4) == 0);
     
    bool isCSQ3Layout = (isCSQ3 || isCSQ4);

    if (isCSQ4) {
        if (fread((unsigned char*)&hdr + CSQ_HEADER_LEGACY_SIZE,
                  sizeof(CSQHeader) - CSQ_HEADER_LEGACY_SIZE, 1, fp) != 1) {
            fclose(fp);
            cseq_report_error(g_hWnd, "Load Error", "Truncated or corrupted .csq file header.");
            job_end(NULL);
            return 1;
        }
    }

    hdr.trackCount  = clamp(hdr.trackCount,  MIN_TRACKS, MAX_TRACKS);
    hdr.barCount    = clamp(hdr.barCount,    MIN_BARS,   MAX_BARS);
    hdr.sampleCount = clamp(hdr.sampleCount, 0,          MAX_SAMPLES);
    hdr.clipCount   = clamp(hdr.clipCount,   0,          MAX_CLIPS);
    hdr.bpm         = clamp(hdr.bpm,         20.0f,      400.0f);

     
    int loadVisibleBars = hdr.barCount;
    if (isCSQ4 && hdr.visibleBarCount > 0) {
        loadVisibleBars = clamp(hdr.visibleBarCount, MIN_BARS, MAX_BARS);
    }

     
    int loadGridDiv    = GRID_1_16;    
    int loadMasterMode = 0;           
    int loadTimeSigNum = 4;           
    int loadTimeSigDen = 4;           
    {
        CSQExtHeader ext;
        memset(&ext, 0, sizeof(ext));
        size_t got = fread(&ext, 16, 1, fp);
        if (got == 1 && memcmp(ext.magic, CSQ_EXT_MAGIC, 4) == 0) {
            loadGridDiv    = cseq_sanitize_grid_division(ext.gridDivision);
            loadMasterMode = (ext.masterMode == 1) ? 1 : 0;
            if (ext.version >= 2) {
                if (fread((unsigned char*)&ext + 16, sizeof(ext) - 16, 1, fp) == 1) {
                    if (ext.timeSigNum > 0) loadTimeSigNum = ext.timeSigNum;
                    if (ext.timeSigDen > 0) loadTimeSigDen = ext.timeSigDen;
                }
            }
        } else {
            if (got == 1) fseek(fp, -(long)16, SEEK_CUR);   
        }
    }

    job_set_progress(5);

    if (g_Seq.deviceInitialized)
        ma_device_stop(&g_Seq.device);

    // Unload any SoundFont left over from the previous project BEFORE taking
    // seq_lock; it only touches g_SFont/g_SFontCache (never g_Seq), and the
    // device is stopped so nothing reads it concurrently. Keeping it out of
    // the lock avoids holding the hot seq_lock across the (slow) bank free.
    sfont_clear();

    clear_undo_stack();
    clear_redo_stack();

    seq_lock();
    g_granTrack = -1;
    g_granClip  = -1;
    if (g_granHwnd) ShowWindow(g_granHwnd, SW_HIDE);

    for (int i = 0; i < g_Seq.sampleCount; ++i) {
        sample_unmap(&g_Seq.samples[i]);
        free_peak_cache(&g_Seq.samples[i]);
        g_Seq.samples[i].loaded = false;
    }
    g_Seq.sampleCount = 0;
    g_Seq.clipCount   = 0;

    for (int t = 0; t < MAX_TRACKS; ++t) {
        if (g_TrackGran[t].ownFrames) {
            free(g_TrackGran[t].ownFrames);
            g_TrackGran[t].ownFrames = NULL;
        }
    }
    for (int c = 0; c < MAX_CLIPS; ++c) {
        if (g_ClipGran[c].ownFrames) {
            free(g_ClipGran[c].ownFrames);
            g_ClipGran[c].ownFrames = NULL;
        }
    }
    granular_init_all();

    // Reset per-clip synth engine runtime state before the state swap.
    synth_state_reset_all();

    fx_init_all();

    g_Seq.bpm             = hdr.bpm;
    g_Seq.swing           = hdr.swing;
    g_Seq.visibleBarCount = loadVisibleBars;
    g_Seq.trackCount      = hdr.trackCount;
    g_Seq.isLofi          = hdr.isLofi != 0;
    g_Seq.quantizeEnabled = hdr.quantizeEnabled != 0;
     
    g_Seq.gridDivision    = loadGridDiv;
    g_Seq.masterMode      = loadMasterMode;
    g_Seq.timeSigNum      = loadTimeSigNum;
    g_Seq.timeSigDen      = loadTimeSigDen;
    set_playback_frame(0);

     
    // Reset per-track mix state (pan/width/solo) these will be overwritten
    // by the CSQT section if present; otherwise keep defaults.
    for (int t = 0; t < g_Seq.trackCount; ++t) {
        g_Seq.trackPan[t]   = 0.0f;
        g_Seq.trackWidth[t] = 1.0f;
        g_Seq.trackSolo[t]  = false;
        g_Seq.trackSidechainSource[t] = -1;
    }

    // Read the legacy CSQTrack records (mute, volume, EQ parameters).
    for (int t = 0; t < g_Seq.trackCount; ++t) {
        CSQTrack trk;
        if (fread(&trk, sizeof(trk), 1, fp) == 1) {
            g_Seq.trackMuted[t]  = trk.isMuted != 0;
            g_Seq.trackVolume[t] = trk.volume;

            // Restore EQ gains (low, mid, high)
            g_Seq.trackEqLow[t]  = trk.eqLow;
            g_Seq.trackEqMid[t]  = trk.eqMid;
            g_Seq.trackEqHigh[t] = trk.eqHigh;
            memcpy(g_Seq.trackEqFreq[t], trk.eqFreq, sizeof(float) * 3);
            memcpy(g_Seq.trackEqQ[t],    trk.eqQ,    sizeof(float) * 3);
            init_track_theme(t);

            // Apply gains to the SmoothEQ3 (three‑band shelving)
            smooth_eq3_set_params(&g_Seq.trackEQ[t], trk.eqHigh, trk.eqMid, trk.eqLow);

            // **FIX: Restore peak biquads with the loaded frequency and Q, not defaults**
            float gains[3] = { trk.eqLow, trk.eqMid, trk.eqHigh };
            for (int b = 0; b < 3; ++b) {
                // Convert normalized freq (0..1) to Hz (matches UI mapping)
                float freqHz = 20.0f * powf(1000.0f, trk.eqFreq[b]);
                // Convert normalized Q (0..1) to actual Q (0.35..8.0)
                float q = 0.35f + trk.eqQ[b] * 4.65f;
                float gainDb = (gains[b] - 0.5f) * 24.0f;

                peak_biquad_clear(&g_Seq.trackPeak[t][b]);
                peak_biquad_set(&g_Seq.trackPeak[t][b], freqHz, q, gainDb, (float)SAMPLE_RATE);
            }

            // Enable the EQ (so it processes in the audio callback)
            g_Seq.trackEqActive[t] = true;
        }
    }
    job_set_progress(15);

     
    typedef struct {
        int sampleIndex, track;
        float startBeat, lengthBeats;
        ma_uint64 sampleOffsetFrames;
        float volume, playbackRate, fadeInBeats, fadeOutBeats;
        bool isSelected;
        bool isGranular;
        bool isMuted;
        float dragStartBeatOrig;
        float dragStartLengthOrig;
        int dragStartTrackOrig;
        ma_uint64 dragStartOffsetOrig;
    } ClipOld;

    for (int c = 0; c < hdr.clipCount; ++c) {
        Clip clp;
        memset(&clp, 0, sizeof(Clip));

        if (isCSQ3Layout) {
            CSQ3ClipEntry entry;
            if (fread(&entry, sizeof(CSQ3ClipEntry), 1, fp) != 1) break;

            clp.sampleIndex          = entry.sampleIndex;
            clp.track                = entry.track;
            clp.startBeat            = entry.startBeat;
            clp.lengthBeats          = entry.lengthBeats;
            clp.sampleOffsetFrames   = entry.sampleOffsetFrames;
            clp.volume               = entry.volume;
            clp.playbackRate         = entry.playbackRate;
            clp.fadeInBeats          = entry.fadeInBeats;
            clp.fadeOutBeats         = entry.fadeOutBeats;
            clp.isSelected           = entry.isSelected != 0;
            clp.isGranular           = entry.isGranular != 0;
            clp.isMuted              = entry.isMuted != 0;
            clp.isMidi               = entry.isMidi != 0;
            clp.midiNoteCount        = entry.midiNoteCount;
        } else if (isCSQ2 || isCSQ1) {
            ClipOld old;
            if (fread(&old, sizeof(ClipOld), 1, fp) != 1) break;

            clp.sampleIndex          = old.sampleIndex;
            clp.track                = old.track;
            clp.startBeat            = old.startBeat;
            clp.lengthBeats          = old.lengthBeats;
            clp.sampleOffsetFrames   = old.sampleOffsetFrames;
            clp.volume               = old.volume;
            clp.playbackRate         = old.playbackRate;
            clp.fadeInBeats          = old.fadeInBeats;
            clp.fadeOutBeats         = old.fadeOutBeats;
            clp.isSelected           = old.isSelected;
            clp.isGranular           = old.isGranular;
            clp.isMuted              = old.isMuted;
            clp.dragStartBeatOrig    = old.dragStartBeatOrig;
            clp.dragStartLengthOrig  = old.dragStartLengthOrig;
            clp.dragStartTrackOrig   = old.dragStartTrackOrig;
            clp.dragStartOffsetOrig  = old.dragStartOffsetOrig;
            clp.isMidi               = false;
            clp.midiNoteCount        = 0;
            memset(clp.midiNotes, 0, sizeof(clp.midiNotes));
        }

        if (g_Seq.clipCount < MAX_CLIPS) {
            clp.track = clamp(clp.track, 0, g_Seq.trackCount - 1);
            if (clp.midiNoteCount < 0 || clp.midiNoteCount > MIDI_MAX_NOTES)
                clp.midiNoteCount = 0;
            // MIDI clips created in-app default to sustain 1.0; a clip loaded
            // from a file (esp. one saved before the ADSR section existed) is
            // memset to zero, which would silence every note (sustain 0). Apply
            // the creation defaults here; the ADSR section overrides them for
            // newer files that carry explicit values.
            if (clp.isMidi) {
                clp.adsrAttack  = (clp.adsrAttack  == 0.0f) ? 5.0f  : clp.adsrAttack;
                clp.adsrDecay   = (clp.adsrDecay   == 0.0f) ? 0.0f  : clp.adsrDecay;
                clp.adsrSustain = (clp.adsrSustain == 0.0f) ? 1.0f  : clp.adsrSustain;
                clp.adsrRelease = (clp.adsrRelease == 0.0f) ? 10.0f : clp.adsrRelease;
            }
            g_Seq.clips[g_Seq.clipCount++] = clp;
        }
    }

    // Sanitize the timeline against the bar count. A project saved while clips
    // extended past visibleBarCount (older builds, or a bar-count reduction
    // that didn't trim) would otherwise load with clips overhanging into the
    // void past the last bar. Expand the bar count to contain them when
    // possible, then clamp any clip that still straddles the boundary.
    {
        float bpb = beats_per_bar();
        float maxClipEnd = 0.0f;
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            float end = g_Seq.clips[i].startBeat + g_Seq.clips[i].lengthBeats;
            if (end > maxClipEnd) maxClipEnd = end;
        }
        int neededBars = (int)ceilf(maxClipEnd / (bpb > 0.0f ? bpb : 1.0f));
        if (neededBars > g_Seq.visibleBarCount && neededBars <= MAX_BARS) {
            g_Seq.visibleBarCount = neededBars;
        }
        float limitBeats = total_beats();
        for (int i = 0; i < g_Seq.clipCount; ++i) {
            Clip* c = &g_Seq.clips[i];
            if (c->startBeat + c->lengthBeats > limitBeats) {
                if (c->startBeat >= limitBeats) {
                    c->startBeat = 0.0f;
                    c->lengthBeats = limitBeats;
                } else {
                    c->lengthBeats = limitBeats - c->startBeat;
                }
                if (c->fadeInBeats > c->lengthBeats)  c->fadeInBeats = c->lengthBeats;
                if (c->fadeOutBeats > c->lengthBeats) c->fadeOutBeats = c->lengthBeats;
            }
        }
    }
    job_set_progress(25);

    // The file's sample list is compacted on save (build_sample_remap), but on
    // load a sample entry can be skipped without consuming a slot (empty,
    // oversized, MAX_SAMPLES overflow, or an allocation/read failure). Clips
    // and granular engines reference their sample as an index into
    // g_Seq.samples[], so every skip shifts later indices. Track the file slot
    // -> loaded index mapping so those references can be repaired after the
    // pool is rebuilt.
    int loadRemap[MAX_SAMPLES];
    for (int i = 0; i < MAX_SAMPLES; ++i) loadRemap[i] = -1;

    for (int s = 0; s < hdr.sampleCount; ++s) {
        CSQSampleHeader shdr;
        if (fread(&shdr, sizeof(shdr), 1, fp) != 1) break;

        if (shdr.rawBytes == 0 || shdr.frameCount == 0 ||
            shdr.rawBytes > 256 * 1024 * 1024 || shdr.compBytes > 256 * 1024 * 1024) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            continue;
        }
        // Corrupt-header guard: frameCount and rawBytes must agree, otherwise
        // a huge frameCount with a small buffer would drive out-of-bounds
        // reads in the peak cache / cache-store paths below.
        if ((ma_uint64)shdr.rawBytes != (ma_uint64)shdr.frameCount * sizeof(float) * NUM_CHANNELS) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            continue;
        }
        if (g_Seq.sampleCount >= MAX_SAMPLES) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            continue;
        }

        size_t rawSize = shdr.rawBytes;
        AudioSample* as = &g_Seq.samples[g_Seq.sampleCount++];
        memset(as, 0, sizeof(AudioSample));
        strncpy(as->name,     shdr.name, sizeof(as->name) - 1);
        strncpy(as->filename, shdr.name, sizeof(as->filename) - 1);
        as->frameCount = shdr.frameCount;

        // Decode into a heap buffer first (all writes happen here), then move it
        // into the disk-backed cache via sample_install_cached.
        float* heapPcm = (float*)malloc(rawSize);
        if (!heapPcm) {
            fseek(fp, (long)shdr.compBytes, SEEK_CUR);
            g_Seq.sampleCount--;
            continue;
        }

        if (shdr.compBytes == 0) {
            memset(heapPcm, 0, rawSize);
        } else {
            unsigned char* compBuf = (unsigned char*)malloc(shdr.compBytes);
            if (!compBuf) {
                fseek(fp, (long)shdr.compBytes, SEEK_CUR);
                free(heapPcm);
                g_Seq.sampleCount--;
                continue;
            }
            if (fread(compBuf, 1, shdr.compBytes, fp) != shdr.compBytes) {
                free(compBuf);
                free(heapPcm);
                g_Seq.sampleCount--;
                continue;
            }
            if (shdr.compBytes == shdr.rawBytes) {
                memcpy(heapPcm, compBuf, rawSize);
            } else {
                if (!csq_decompress_lz(compBuf, shdr.compBytes, (unsigned char*)heapPcm, rawSize))
                    memset(heapPcm, 0, rawSize);
            }
            free(compBuf);
        }

        uint64_t hash = sample_hash_pcm(heapPcm, rawSize);
        sample_install_cached(as, heapPcm, shdr.frameCount, hash);
        as->loaded = true;
        generate_peak_cache_auto(as);
        loadRemap[s] = g_Seq.sampleCount - 1;
        job_set_progress(25 + (int)(((float)(s + 1) / (float)(hdr.sampleCount > 0 ? hdr.sampleCount : 1)) * 55.0f));
    }

    // Reconnect every clip to its sample in the rebuilt pool. The file stores
    // compacted indices, and any skipped sample entry shifts later indices;
    // map them through loadRemap so each clip resolves to the sample that was
    // actually loaded (or none if it was dropped).
    for (int c = 0; c < g_Seq.clipCount; ++c) {
        Clip* clp = &g_Seq.clips[c];
        if (clp->sampleIndex >= 0 && clp->sampleIndex < hdr.sampleCount)
            clp->sampleIndex = loadRemap[clp->sampleIndex];
        else
            clp->sampleIndex = -1;
    }

     
    if (isCSQ2 || isCSQ3Layout) {
        CSQGranSection gsec = { 0 };
        if (fread(&gsec, sizeof(gsec), 1, fp) == 1 && gsec.version == 1) {
            for (int i = 0; i < gsec.trackGranCount; ++i) {
                long pos = ftell(fp);
                CSQGranEngineHeader peek = { 0 };
                if (fread(&peek, sizeof(peek), 1, fp) != 1) break;
                fseek(fp, pos, SEEK_SET);
                int t = peek.trackIdx;
                if (t >= 0 && t < MAX_TRACKS) {
                    if (!read_gran_engine(fp, &g_TrackGran[t], loadRemap)) break;
                    g_TrackGran[t].trackIdx = t;
                    g_TrackGran[t].clipIdx  = -1;
                } else {
                    GranularEngine dummy = { 0 };
                    if (!read_gran_engine(fp, &dummy, loadRemap)) break;
                    if (dummy.ownFrames) free(dummy.ownFrames);
                }
            }
            for (int i = 0; i < gsec.clipGranCount; ++i) {
                long pos = ftell(fp);
                CSQGranEngineHeader peek = { 0 };
                if (fread(&peek, sizeof(peek), 1, fp) != 1) break;
                fseek(fp, pos, SEEK_SET);
                int c = peek.trackIdx;
                if (c >= 0 && c < MAX_CLIPS) {
                    if (!read_gran_engine(fp, &g_ClipGran[c], loadRemap)) break;
                    g_ClipGran[c].clipIdx  = c;
                    g_ClipGran[c].trackIdx = (c < g_Seq.clipCount) ? g_Seq.clips[c].track : -1;
                } else {
                    GranularEngine dummy = { 0 };
                    if (!read_gran_engine(fp, &dummy, loadRemap)) break;
                    if (dummy.ownFrames) free(dummy.ownFrames);
                }
            }
        }
    }

     
    if (isCSQ3Layout) {
        long midiStart = ftell(fp);
        CSQMidiSection midiSec = { 0 };
        if (fread(&midiSec, sizeof(midiSec), 1, fp) == 1 &&
            memcmp(midiSec.magic, CSQ_MIDI_MAGIC, 4) == 0 &&
            (midiSec.version == 1 || midiSec.version == 2)) {

             
            const bool v2Notes = (midiSec.version >= 2);
            const size_t recSize = v2Notes ? sizeof(MidiNote) : sizeof(CSQMidiNoteV1);

            for (int i = 0; i < midiSec.clipCount; ++i) {
                CSQMidiClipEntry entry;
                if (fread(&entry, sizeof(entry), 1, fp) != 1) break;

                if (entry.clipIndex < 0 || entry.clipIndex >= g_Seq.clipCount ||
                    entry.noteCount < 0 || entry.noteCount > MIDI_MAX_NOTES * 4) {
                    if (entry.noteCount > 0)
                        fseek(fp, entry.noteCount * (long)recSize, SEEK_CUR);
                    continue;
                }

                Clip* clip = &g_Seq.clips[entry.clipIndex];
                int toRead = entry.noteCount;
                if (toRead > MIDI_MAX_NOTES) toRead = MIDI_MAX_NOTES;

                clip->midiNoteCount = toRead;
                clip->isMidi = true;

                for (int nIdx = 0; nIdx < entry.noteCount; ++nIdx) {
                    if (v2Notes) {
                        MidiNote mn;
                        memset(&mn, 0, sizeof(mn));
                        if (fread(&mn, sizeof(mn), 1, fp) != 1) { nIdx = entry.noteCount; break; }
                        if (nIdx < toRead) {
                            mn.active = true;
                            clip->midiNotes[nIdx] = mn;
                        }
                    } else {
                        CSQMidiNoteV1 on;
                        if (fread(&on, sizeof(on), 1, fp) != 1) { nIdx = entry.noteCount; break; }
                        if (nIdx < toRead) {
                            MidiNote* mn = &clip->midiNotes[nIdx];
                            mn->pitch        = on.pitch;
                            mn->startBeat    = on.startBeat;
                            mn->lengthBeats  = on.lengthBeats;
                            mn->velocity     = on.velocity;
                            mn->active       = true;
                            mn->isSelected   = false;
                            mn->dragStartBeatOrig = 0.0f;
                            mn->dragLengthOrig    = 0.0f;
                            mn->dragPitchOrig     = 0;
                        }
                    }
                }
            }
        } else {
            fseek(fp, midiStart, SEEK_SET);
        }
    }

     
    if (isCSQ4) {
        long fsecStart = ftell(fp);
        CSQFadeSection fsec = { 0 };
        if (fread(&fsec, sizeof(fsec), 1, fp) == 1 &&
            memcmp(fsec.magic, CSQ_FADE_MAGIC, 4) == 0 &&
            fsec.version == CSQ_FADE_VERSION &&
            fsec.clipCount >= 0 && fsec.clipCount <= MAX_CLIPS) {
            for (int i = 0; i < fsec.clipCount && i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
                uint8_t types[2];
                if (fread(types, 1, sizeof(types), fp) != sizeof(types)) break;
                if (types[0] < FADE_CURVE_COUNT) g_Seq.clips[i].fadeInType  = types[0];
                if (types[1] < FADE_CURVE_COUNT) g_Seq.clips[i].fadeOutType = types[1];
            }
        } else {
            fseek(fp, fsecStart, SEEK_SET);
        }
    }

     
    if (isCSQ4) {
        long asecStart = ftell(fp);
        CSQAdsrSection asec = { 0 };
        if (fread(&asec, sizeof(asec), 1, fp) == 1 &&
            memcmp(asec.magic, CSQ_ADSR_MAGIC, 4) == 0 &&
            asec.version == CSQ_ADSR_VERSION &&
            asec.clipCount >= 0 && asec.clipCount <= MAX_CLIPS) {
            for (int i = 0; i < asec.clipCount && i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
                float adsr[4];
                if (fread(adsr, sizeof(float), 4, fp) != 4) break;
                g_Seq.clips[i].adsrAttack  = adsr[0];
                g_Seq.clips[i].adsrDecay   = adsr[1];
                g_Seq.clips[i].adsrSustain = adsr[2];
                g_Seq.clips[i].adsrRelease = adsr[3];
            }
        } else {
            fseek(fp, asecStart, SEEK_SET);
        }
    }

    // forwards/backwards compat
    // SaveProjectThreadProc already writes fxs.version = CSQ_FX_VERSION, so new saves will automatically use Version 2
    if (isCSQ4) {
        long fxStart = ftell(fp);
        CSQFxSection fxs = { 0 };
        if (fread(&fxs, sizeof(fxs), 1, fp) == 1 &&
            memcmp(fxs.magic, CSQ_FX_MAGIC, 4) == 0 &&
            (fxs.version == CSQ_FX_VERSION_LEGACY || fxs.version == CSQ_FX_VERSION) &&
            fxs.trackCount >= 0 && fxs.trackCount <= MAX_TRACKS) {
            for (int t = 0; t < fxs.trackCount; ++t) {
                if (fxs.version == CSQ_FX_VERSION_LEGACY) {
                    CSQFxChainV1 fc;
                    memset(&fc, 0, sizeof(fc));
                    if (fread(&fc, sizeof(fc), 1, fp) != 1) break;
                    if (fc.count < 0 || fc.count > 8) continue;
                    fx_chain_load(&g_TrackFx[t], (int)fc.count, fc.slots, &fc.params[0][0], (int)SAMPLE_RATE);
                } else {
                    CSQFxChain fc;
                    memset(&fc, 0, sizeof(fc));
                    if (fread(&fc, sizeof(fc), 1, fp) != 1) break;
                    if (fc.count < 0 || fc.count > FX_MAX_SLOTS) continue;
                    fx_chain_load(&g_TrackFx[t], (int)fc.count, fc.slots, &fc.params[0][0], (int)SAMPLE_RATE);
                }
            }
        } else {
            fseek(fp, fxStart, SEEK_SET);
        }
    }

    // --- SoundFont external reference (CSQSoundFontSection, trailing) ---
    // Optional: legacy files end at CSQE, so the fread fails or the magic
    // doesn't match and we rewind to sfStart. Runs before the audio device
    // restarts so the pre-rendered note cache is ready for playback.
    bool sfontMissing = false;
    char sfontMissingName[64] = { 0 };
    if (isCSQ4) {
        long sfStart = ftell(fp);
        CSQSoundFontSection sfs;
        memset(&sfs, 0, sizeof(sfs));

        if (fread(&sfs, sizeof(sfs), 1, fp) == 1 &&
            memcmp(sfs.magic, CSQ_SFONT_MAGIC, 4) == 0 &&
            sfs.version == CSQ_SFONT_VERSION) {

            char resolvedPath[MAX_PATH];
            if (cseq_resolve_soundfont_path(path, sfs.relPath, sfs.absPath,
                                            resolvedPath, sizeof(resolvedPath))) {
                // Synchronous load on this background thread: job_begin()
                // would fail here (job kind 2 already active), and the parse
                // + note-cache build completes before the device restarts.
                sfont_load_sync(resolvedPath, sfs.activePreset);
            } else {
                sfontMissing = true;
                strncpy(sfontMissingName, sfs.name, sizeof(sfontMissingName) - 1);
                sfontMissingName[sizeof(sfontMissingName) - 1] = '\0';
            }
        } else {
            fseek(fp, sfStart, SEEK_SET);
        }
    }

    // --- Track Filter Plotter section (CSQV, trailing, optional) ---
    // Missing (legacy) files or a magic mismatch rewind and leave the
    // per-track defaults the fx_init_all()/init pass established.
    if (isCSQ4) {
        long vfStart = ftell(fp);
        CSQFilterSection vfs;
        memset(&vfs, 0, sizeof(vfs));
        if (fread(&vfs, sizeof(vfs), 1, fp) == 1 &&
            memcmp(vfs.magic, CSQ_FILTER_MAGIC, 4) == 0 &&
            vfs.version == CSQ_FILTER_VERSION &&
            vfs.trackCount >= 0 && vfs.trackCount <= MAX_TRACKS) {
            for (int t = 0; t < vfs.trackCount && t < MAX_TRACKS; ++t) {
                CSQFilterTrack vf;
                if (fread(&vf, sizeof(vf), 1, fp) != 1) break;
                TrackFilter* f = &g_Seq.trackFilter[t];
                f->typeMask  = ((uint32_t)vf.typeMask & TRACK_FILTER_MASK_ALL);
                f->frequency = vf.frequency;
                f->q         = vf.q;
                f->enabled   = (vf.enabled != 0) && (vf.frequency > 0.0f);
                // Project load IS a context reset: clamp targets, reseed the
                // smoothed copies from them, rebuild coeffs + UI curve, and
                // start the states from silence.
                track_filter_clamp_params(f);
                track_filter_update(f, (float)SAMPLE_RATE);
                track_filter_clear_state(f);
            }
        } else {
            fseek(fp, vfStart, SEEK_SET);
        }
    }

    // --- Synth Clip Kind section (CSQY, trailing, optional) ---
    // Missing (legacy) files or a magic mismatch rewind and leave every clip
    // at its default kind (0 = sample/MIDI). Version 1 carried only the kind;
    // version 2 adds the synth-module envelope scaffolding; version 3 adds the
    // full engine patches (Quadrum per-voice bank + Halo patch).
    if (isCSQ4) {
        long ysStart = ftell(fp);
        CSQSynthSection ysec;
        memset(&ysec, 0, sizeof(ysec));
        if (fread(&ysec, sizeof(ysec), 1, fp) == 1 &&
            memcmp(ysec.magic, CSQ_SYNTH_MAGIC, 4) == 0 &&
            (ysec.version == 1 || ysec.version == 2 || ysec.version == CSQ_SYNTH_VERSION) &&
            ysec.count >= 0 && ysec.count <= MAX_CLIPS) {
            for (int i = 0; i < ysec.count; ++i) {
                CSQSynthClip ys;
                memset(&ys, 0, sizeof(ys));
                if (ysec.version >= CSQ_SYNTH_VERSION) {
                    // Version 3 record carries the full struct incl. patches.
                    if (fread(&ys, sizeof(ys), 1, fp) != 1) break;
                } else if (ysec.version == 2) {
                    // Version 2 record: {clipIndex, clipKind, reserved[3],
                    // 4 synth ADSR floats} — no patches.
                    int32_t vi = 0; uint8_t vk = 0; uint8_t vres[3] = {0,0,0};
                    float   sa=0, sd=0, ss=0, sr=0;
                    if (fread(&vi, sizeof(vi), 1, fp) != 1) break;
                    if (fread(&vk, sizeof(vk), 1, fp) != 1) break;
                    if (fread(&vres, sizeof(vres), 1, fp) != 1) break;
                    if (fread(&sa, sizeof(sa), 1, fp) != 1) break;
                    if (fread(&sd, sizeof(sd), 1, fp) != 1) break;
                    if (fread(&ss, sizeof(ss), 1, fp) != 1) break;
                    if (fread(&sr, sizeof(sr), 1, fp) != 1) break;
                    ys.clipIndex = vi; ys.clipKind = vk;
                    ys.synthAttack = sa; ys.synthDecay = sd;
                    ys.synthSustain = ss; ys.synthRelease = sr;
                } else {
                    // Version 1 records were {int32 clipIndex; uint8 clipKind}
                    // (5 bytes) — read just that prefix; synth ADSR stays 0.
                    int32_t vi = 0; uint8_t vk = 0;
                    if (fread(&vi, sizeof(vi), 1, fp) != 1) break;
                    if (fread(&vk, sizeof(vk), 1, fp) != 1) break;
                    ys.clipIndex = vi; ys.clipKind = vk;
                }
                if (ys.clipIndex < 0 || ys.clipIndex >= g_Seq.clipCount) continue;
                if (ys.clipKind == CLIP_KIND_QUADRUM || ys.clipKind == CLIP_KIND_HALO) {
                    Clip* yc = &g_Seq.clips[ys.clipIndex];
                    yc->clipKind = ys.clipKind;
                    if (ysec.version >= 2) {
                        yc->synthAttack  = ys.synthAttack;
                        yc->synthDecay   = ys.synthDecay;
                        yc->synthSustain = ys.synthSustain;
                        yc->synthRelease = ys.synthRelease;
                    } else {
                        yc->synthAttack  = 5.0f;
                        yc->synthDecay   = 0.0f;
                        yc->synthSustain = 1.0f;
                        yc->synthRelease = 10.0f;
                    }
                    // Version 3 carries full patches; older versions fall back
                    // to engine factory defaults.
                    if (ysec.version >= CSQ_SYNTH_VERSION) {
                        memcpy(yc->quadrumParams, ys.quadrumParams, sizeof(yc->quadrumParams));
                        yc->haloPatch = ys.haloPatch;
                    } else {
                        for (int v = 0; v < 8; ++v)
                            quadrum_get_preset((VoiceType)v, &yc->quadrumParams[v]);
                        halo_get_preset(0, &yc->haloPatch);
                    }
                }
            }
        } else {
            fseek(fp, ysStart, SEEK_SET);
        }
    }

    // --- Track Trigger Probability section (CSQP, trailing, optional) ---
    // Missing (legacy) files or a magic mismatch rewind and leave every track
    // at its default (prob 1.0, seed (t*1337)+1). The saved RNG seed is
    // restored so a reloaded project produces the same probability stream.
    if (isCSQ4) {
        long psStart = ftell(fp);
        CSQProbSection psec;
        memset(&psec, 0, sizeof(psec));
        if (fread(&psec, sizeof(psec), 1, fp) == 1 &&
            memcmp(psec.magic, CSQ_PROB_MAGIC, 4) == 0 &&
            psec.version == CSQ_PROB_VERSION &&
            psec.trackCount >= 0 && psec.trackCount <= MAX_TRACKS) {
            for (int t = 0; t < psec.trackCount && t < MAX_TRACKS; ++t) {
                CSQProbTrack pt;
                memset(&pt, 0, sizeof(pt));
                if (fread(&pt, sizeof(pt), 1, fp) != 1) break;
                if (pt.trackIndex < 0 || pt.trackIndex >= g_Seq.trackCount) continue;
                float prob = pt.prob;
                if (prob < 0.0f) prob = 0.0f;
                if (prob > 1.0f) prob = 1.0f;
                g_Seq.trackTriggerProb[pt.trackIndex] = prob;
                g_Seq.trackRngState[pt.trackIndex] = pt.rngSeed;
            }
        } else {
            fseek(fp, psStart, SEEK_SET);
        }
    }

    // --- Master Audio Params section (CSQM, trailing, optional) ---
    // Missing (legacy) files leave the current in-memory master/lofi/export
    // settings untouched (they are never reset to boot defaults on load).
    if (isCSQ4) {
        long msStart = ftell(fp);
        CSQMasterSection ms;
        memset(&ms, 0, sizeof(ms));
        if (fread(&ms, sizeof(ms), 1, fp) == 1 &&
            memcmp(ms.magic, CSQ_MASTER_MAGIC, 4) == 0 &&
            ms.version == CSQ_MASTER_VERSION) {
            g_Seq.masterVolume   = ms.masterVolume;
            g_Seq.lofiBitDepth   = ms.lofiBitDepth;
            g_Seq.lofiSampleRate = ms.lofiSampleRate;
            g_Seq.exportBitDepth = ms.exportBitDepth;
        } else {
            fseek(fp, msStart, SEEK_SET);
        }
    }

    // --- Track Mix section (CSQT, trailing, optional) ---
    // Restores per-track pan/width/solo (not carried by the legacy CSQTrack
    // record). Missing/legacy files keep the defaults set above.
    if (isCSQ4) {
        long tmsStart = ftell(fp);
        CSQTrackMixSection tms;
        memset(&tms, 0, sizeof(tms));
        if (fread(&tms, sizeof(tms), 1, fp) == 1 &&
            memcmp(tms.magic, CSQ_TRACKMIX_MAGIC, 4) == 0 &&
            tms.version >= 1 && tms.version <= CSQ_TRACKMIX_VERSION &&
            tms.trackCount >= 0 && tms.trackCount <= MAX_TRACKS) {
            for (int t = 0; t < tms.trackCount && t < MAX_TRACKS; ++t) {
                CSQTrackMix tm;
                memset(&tm, 0, sizeof(tm));
                // Read the v1-compatible fixed prefix, then the v2-only
                // sidechain field when present.
                int32_t prefix[4];
                if (fread(&prefix, sizeof(int32_t), 4, fp) != 4) break;
                tm.trackIndex = prefix[0];
                memcpy(&tm.pan,   &prefix[1], sizeof(float));
                memcpy(&tm.width, &prefix[2], sizeof(float));
                tm.solo = prefix[3];
                tm.sidechainSource = -1;
                if (tms.version >= 2) {
                    if (fread(&tm.sidechainSource, sizeof(int32_t), 1, fp) != 1) break;
                }
                if (tm.trackIndex < 0 || tm.trackIndex >= g_Seq.trackCount) continue;
                float pan = tm.pan;
                if (pan < -1.0f) pan = -1.0f;
                if (pan >  1.0f) pan =  1.0f;
                float width = tm.width;
                if (width < 0.0f) width = 0.0f;
                if (width > 2.0f) width = 2.0f;
                g_Seq.trackPan[tm.trackIndex]   = pan;
                g_Seq.trackWidth[tm.trackIndex] = width;
                g_Seq.trackSolo[tm.trackIndex]  = tm.solo != 0;
                if (tm.sidechainSource >= -1 && tm.sidechainSource < MAX_TRACKS)
                    g_Seq.trackSidechainSource[tm.trackIndex] = (int8_t)tm.sidechainSource;
            }
        } else {
            fseek(fp, tmsStart, SEEK_SET);
        }
    }

    // (Re)initialize per-clip synth runtime state for all loaded clips so the
    // Quadrum transient buffers are pre-allocated off the audio thread.
    for (int i = 0; i < g_Seq.clipCount && i < MAX_CLIPS; ++i) {
        synth_state_init_clip(i);
    }

    seq_unlock();
    fclose(fp);

     
    cseq_clip_structure_changed();
    mark_all_bars_dirty();

    if (g_Seq.deviceInitialized)
        ma_device_start(&g_Seq.device);

    job_set_progress(100);
    {
        const char* base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        strncpy(g_Seq.currentProjectName, base, sizeof(g_Seq.currentProjectName) - 1);
        strncpy(g_Seq.currentProjectFile, path, sizeof(g_Seq.currentProjectFile) - 1);
        g_Seq.isModified = false;
        update_window_title();
    }

     
    // This runs on the load worker thread; ask the UI thread to refresh.
    if (g_hWnd && IsWindow(g_hWnd)) {
        PostMessageA(g_hWnd, WM_APP_FULL_REDRAW, 0, 0);
    }

    job_end("Loaded .csq module project successfully.");

    // job_end overwrote the status message, so the missing-SoundFont note is
    // applied after it to survive on screen.
    if (sfontMissing) {
        const char* sfRef = sfontMissingName[0] ? sfontMissingName : "(unknown)";
        snprintf(g_Seq.exportMsg, sizeof(g_Seq.exportMsg),
                 "Note: SoundFont '%s' not found.", sfRef);
        g_Seq.exportMsgActive = true;
        g_Seq.exportMsgExpiry = GetTickCount64() + 5000;
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
    }
    return 0;
}

static inline void load_project_from_csq(const char* path) {
    if (!job_begin(2, path)) {
        cseq_report_error(g_hWnd, "Load Error", "Another file operation is already in progress.");
        return;
    }
    HANDLE hThread = CreateThread(NULL, 0, LoadProjectThreadProc, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
    else job_end(NULL);
}

 
static inline void save_project_dialog(HWND hwnd) {
    if (job_is_busy()) {
        MessageBoxA(hwnd, "Another file operation is already in progress.", "Save", MB_ICONINFORMATION);
        return;
    }
     
    OPENFILENAMEW ofn;
    wchar_t szFileW[MAX_PATH] = L"project.csq";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"cseq Module Project (*.csq)\0*.csq\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileW;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"csq";

    if (GetSaveFileNameW(&ofn)) {
        char szFile[MAX_PATH];
        if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) > 0) {
            save_project_to_csq(szFile);
        }
    }
}

static inline void load_project_dialog(HWND hwnd) {
    if (job_is_busy()) {
        MessageBoxA(hwnd, "Another file operation is already in progress.", "Load", MB_ICONINFORMATION);
        return;
    }
    OPENFILENAMEW ofn;
    wchar_t szFileW[MAX_PATH] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"cseq Module Project (*.csq)\0*.csq\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileW;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        char szFile[MAX_PATH];
        if (wide_to_utf8_buf(szFileW, szFile, MAX_PATH) > 0) {
            load_project_from_csq(szFile);
        }
    }
}
