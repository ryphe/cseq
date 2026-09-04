#define CSQ_MIDI_MAGIC "MIDI"

#pragma once
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <windows.h>
#include "miniaudio.h"
#include "fx.h"
#include "types.h"    // HaloPatch / QuadrumParams for the CSQY synth section

#pragma pack(push, 1)
typedef struct {
    char magic[4];
    float bpm, swing;
    int barCount, trackCount, sampleCount, clipCount, isLofi, quantizeEnabled;
    int storedBarCount;
    int visibleBarCount;
} CSQHeader;

 
#define CSQ_HEADER_LEGACY_SIZE 36
#define CSQ_MAGIC "CSQ4"

_Static_assert(offsetof(CSQHeader, storedBarCount) == CSQ_HEADER_LEGACY_SIZE,
               "CSQHeader legacy layout drift: CSQ1-3 files would misparse");

typedef struct {
    int trackIndex, isMuted;
    float volume, eqLow, eqMid, eqHigh, eqFreq[3], eqQ[3];
} CSQTrack;

typedef struct {
    char name[64];
    ma_uint64 frameCount;
    DWORD rawBytes, compBytes;
} CSQSampleHeader;

 
typedef struct {
    int32_t   sampleIndex;
    int32_t   track;
    float     startBeat;
    float     lengthBeats;
    ma_uint64 sampleOffsetFrames;
    float     volume;
    float     playbackRate;
    float     fadeInBeats;
    float     fadeOutBeats;
    uint8_t   isSelected;
    uint8_t   isGranular;
    uint8_t   isMuted;
    uint8_t   isMidi;
    int32_t   midiNoteCount;
} CSQ3ClipEntry;
#pragma pack(pop)

 
typedef struct {
    int32_t version;           
    int32_t trackGranCount;    
    int32_t clipGranCount;     
} CSQGranSection;

typedef struct {
    int32_t trackIdx;          
    int32_t enabled;
    float   grainSizeMs, density, position, posJitter;
    float   pitch, pitchJitter, panSpread, attack, release, volume;
    int32_t freeze, droneMode, sampleIndex, octaveShift;
    int32_t noteCount;
    int32_t ownLoaded;         
} CSQGranEngineHeader;

typedef struct {
    int32_t active;
    int32_t midiNote;
    float   velocity;
    float   startBeat;
    float   lengthBeats;
} CSQGranNote;

typedef struct {
    char    magic[4];           
    int32_t version;            
    int32_t clipCount;          
} CSQMidiSection;

 
typedef struct {
    char    magic[4];           
    int32_t version;            
    int32_t clipCount;
} CSQFadeSection;

#define CSQ_FADE_MAGIC   "CSQF"
#define CSQ_FADE_VERSION 1

 
typedef struct {
    char    magic[4];           
    int32_t version;            
    int32_t clipCount;
} CSQAdsrSection;

#define CSQ_ADSR_MAGIC   "CSQA"
#define CSQ_ADSR_VERSION 1

 
typedef struct {
    char    magic[4];           
    int32_t version;            
    int32_t trackCount;
} CSQFxSection;

typedef struct {
    int32_t count;                                  
    int32_t slots[FX_MAX_SLOTS];                    
    float   params[FX_MAX_SLOTS][FX_MAX_PARAMS];    
} CSQFxChain;

#define CSQ_FX_MAGIC   "CSQE"
#define CSQ_FX_VERSION 1

// --- SoundFont External Reference Section (trailing, optional) ---
// Stores only path descriptors + preset selection; .sf2 data is never embedded.
// Legacy loaders stop reading at CSQE and ignore these trailing bytes; legacy
// files make the fread fail here and the loader seeks back to sfStart.
#define CSQ_SFONT_MAGIC   "CSQS"
#define CSQ_SFONT_VERSION 1

// --- Track Filter Plotter Section (trailing, optional) ---
// Per-track stackable filter plotter settings. Trailing like CSQS: legacy
// loaders stop at CSQE / ignore unknown trailing sections via the rewind path.
#define CSQ_FILTER_MAGIC   "CSQV"
#define CSQ_FILTER_VERSION 2

#pragma pack(push, 1)
typedef struct {
    char    magic[4];               // "CSQV"
    int32_t version;                // CSQ_FILTER_VERSION
    int32_t trackCount;             // number of CSQFilterTrack entries that follow
} CSQFilterSection;

typedef struct {
    int32_t typeMask;               // TRACK_FILTER_BIT(type) per active band (v2)
    float   frequency;              // Hz
    float   q;                      // quality factor
    int32_t enabled;                // master bypass flag
} CSQFilterTrack;

// --- Synth Clip Kind Section (trailing, optional) ---
// Marks which clips are Quadrum/Halo synth modules. Notes themselves ride the
// existing MIDI section (synth clips keep isMidi=1). Trailing like CSQV.
#define CSQ_SYNTH_MAGIC   "CSQY"
#define CSQ_SYNTH_VERSION 3

typedef struct {
    char    magic[4];               // "CSQY"
    int32_t version;                // CSQ_SYNTH_VERSION
    int32_t count;                  // number of CSQSynthClip entries that follow
} CSQSynthSection;

typedef struct {
    int32_t clipIndex;              // index into the saved clip order
    uint8_t clipKind;               // CLIP_KIND_QUADRUM / CLIP_KIND_HALO
    uint8_t reserved[3];
    // Synth-module envelope scaffolding, mapped directly to the internal
    // synth engines' per-voice envelopes. Added in version 2.
    float   synthAttack, synthDecay, synthSustain, synthRelease;
    // Full engine patches. Added in version 3: Quadrum carries a per-voice
    // (8) parameter bank, Halo a single polyphonic patch.
    QuadrumParams quadrumParams[8];
    HaloPatch      haloPatch;
} CSQSynthClip;
#pragma pack(pop)

// --- Track Trigger Probability Section (trailing, optional) ---
// Persists the per-track global trigger probability and its RNG seed. Trailing
// like CSQY/CSQV: legacy loaders stop reading at the last known section and
// ignore these trailing bytes, so old files (and old loaders) are unaffected.
#define CSQ_PROB_MAGIC    "CSQP"
#define CSQ_PROB_VERSION  1

#pragma pack(push, 1)
typedef struct {
    char    magic[4];               // "CSQP"
    int32_t version;                // CSQ_PROB_VERSION
    int32_t trackCount;             // number of CSQProbTrack entries that follow
} CSQProbSection;

typedef struct {
    int32_t  trackIndex;            // track index into the saved track order
    float    prob;                  // trigger probability 0..1 (default 1.0)
    uint32_t rngSeed;               // xorshift32 RNG state (persisted so
                                    // playback and export agree after reload)
} CSQProbTrack;
#pragma pack(pop)

// --- Master Audio Params Section (trailing, optional) ---
// Persists the master/lofi/export settings that live on g_Seq. Trailing like
// CSQP: legacy loaders stop reading at the last known section and ignore
// these trailing bytes, so old files (and old loaders) are unaffected.
#define CSQ_MASTER_MAGIC    "CSQM"
#define CSQ_MASTER_VERSION  1

#pragma pack(push, 1)
typedef struct {
    char    magic[4];               // "CSQM"
    int32_t version;                // CSQ_MASTER_VERSION
    float   masterVolume;           // 0..1.5 master bus gain
    int32_t lofiBitDepth;           // 8..12 lo-fi bit depth
    float   lofiSampleRate;         // lo-fi sample-rate downsample (Hz)
    int32_t exportBitDepth;         // 16/24/32 WAV export depth
} CSQMasterSection;
#pragma pack(pop)

// --- Track Mix Section (trailing, optional) ---
// Persists the per-track pan/width/solo mix state that CSQTrack (legacy) does
// not carry. Trailing like CSQM/CSQP for backward/forward compatibility.
#define CSQ_TRACKMIX_MAGIC    "CSQT"
#define CSQ_TRACKMIX_VERSION  1

#pragma pack(push, 1)
typedef struct {
    char    magic[4];               // "CSQT"
    int32_t version;                // CSQ_TRACKMIX_VERSION
    int32_t trackCount;             // number of CSQTrackMix entries that follow
} CSQTrackMixSection;

typedef struct {
    int32_t trackIndex;             // track index into the saved track order
    float   pan;                    // -1..+1 stereo pan
    float   width;                  // 0..1 stereo width
    int32_t solo;                   // solo flag
} CSQTrackMix;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    char    magic[4];               // "CSQS"
    int32_t version;                // CSQ_SFONT_VERSION (1)
    int32_t activePreset;           // Active preset slot index
    char    relPath[MAX_PATH];      // Path relative to the .csq project directory
    char    absPath[MAX_PATH];      // Full absolute filesystem path (fallback)
    char    name[64];               // SoundFont filename or friendly display name
    int32_t flags;                  // Reserved for future extensions (zero-init)
} CSQSoundFontSection;
#pragma pack(pop)

typedef struct {
    int32_t clipIndex;
    int32_t noteCount;
     
} CSQMidiClipEntry;

 
typedef struct {
    int32_t pitch;
    float   startBeat;
    float   lengthBeats;
    float   velocity;
} CSQMidiNoteV1;

 
static inline unsigned char* csq_compress_lz(const unsigned char *src, size_t srcSize, size_t *outSize) {
    if (!src || srcSize == 0) return NULL;
    size_t maxDst = srcSize + (srcSize / 8) + 512;
    unsigned char *dst = (unsigned char*)malloc(maxDst);
    if (!dst) return NULL;

    #define CSQ_HASH_SIZE 8192
    int32_t head[CSQ_HASH_SIZE];
    memset(head, -1, sizeof(head));

    size_t inPos = 0, outPos = 0;
    size_t litStart = 0;

    while (inPos < srcSize) {
        if (outPos + 512 >= maxDst) {
            maxDst = maxDst * 2 + 1024;
            unsigned char *nDst = (unsigned char*)realloc(dst, maxDst);
            if (!nDst) { free(dst); return NULL; }
            dst = nDst;
        }

        size_t bestLen = 0;
        size_t bestOff = 0;

        if (inPos + 3 <= srcSize) {
            uint32_t h = ((src[inPos] << 10) ^ (src[inPos + 1] << 5) ^ src[inPos + 2]) & (CSQ_HASH_SIZE - 1);
            int32_t matchPos = head[h];
            head[h] = (int32_t)inPos;

            if (matchPos >= 0 && (inPos - (size_t)matchPos) <= 4095) {
                size_t off = inPos - (size_t)matchPos;
                size_t maxMatch = srcSize - inPos;
                if (maxMatch > 127) maxMatch = 127;

                size_t l = 0;
                while (l < maxMatch && src[matchPos + l] == src[inPos + l]) l++;

                if (l >= 3) {
                    bestLen = l;
                    bestOff = off;
                }
            }
        }

        if (bestLen >= 3) {
            
            while (litStart < inPos) {
                if (outPos + 256 >= maxDst) {
                    maxDst = maxDst * 2 + 1024;
                    unsigned char *nDst = (unsigned char*)realloc(dst, maxDst);
                    if (!nDst) { free(dst); return NULL; }
                    dst = nDst;
                }
                size_t chunk = inPos - litStart;
                if (chunk > 127) chunk = 127;
                dst[outPos++] = (unsigned char)chunk;
                memcpy(&dst[outPos], &src[litStart], chunk);
                outPos += chunk;
                litStart += chunk;
            }

            if (outPos + 8 >= maxDst) {
                maxDst = maxDst * 2 + 1024;
                unsigned char *nDst = (unsigned char*)realloc(dst, maxDst);
                if (!nDst) { free(dst); return NULL; }
                dst = nDst;
            }
            dst[outPos++] = 0x80 | (unsigned char)(bestLen & 0x7F);
            dst[outPos++] = (unsigned char)(bestOff & 0xFF);
            dst[outPos++] = (unsigned char)((bestOff >> 8) & 0x0F);
            inPos += bestLen;
            litStart = inPos;
        } else {
            inPos++;
            if (inPos - litStart >= 127) {
                if (outPos + 256 >= maxDst) {
                    maxDst = maxDst * 2 + 1024;
                    unsigned char *nDst = (unsigned char*)realloc(dst, maxDst);
                    if (!nDst) { free(dst); return NULL; }
                    dst = nDst;
                }
                size_t chunk = inPos - litStart;
                dst[outPos++] = (unsigned char)chunk;
                memcpy(&dst[outPos], &src[litStart], chunk);
                outPos += chunk;
                litStart = inPos;
            }
        }
    }

    
    while (litStart < inPos) {
        if (outPos + 256 >= maxDst) {
            maxDst = maxDst * 2 + 1024;
            unsigned char *nDst = (unsigned char*)realloc(dst, maxDst);
            if (!nDst) { free(dst); return NULL; }
            dst = nDst;
        }
        size_t chunk = inPos - litStart;
        if (chunk > 127) chunk = 127;
        dst[outPos++] = (unsigned char)chunk;
        memcpy(&dst[outPos], &src[litStart], chunk);
        outPos += chunk;
        litStart += chunk;
    }

    #undef CSQ_HASH_SIZE
    *outSize = outPos;
    return dst;
}

 
static inline bool csq_decompress_lz(const unsigned char *src, size_t srcSize, unsigned char *dst, size_t origSize) {
    if (!src || !dst || srcSize == 0 || origSize == 0) return false;
    size_t inPos = 0, outPos = 0;
    while (inPos < srcSize && outPos < origSize) {
        unsigned char tag = src[inPos++];
        if (tag & 0x80) {
            size_t len = (size_t)(tag & 0x7F);
            if (inPos + 1 >= srcSize) return false;
            size_t off = (size_t)src[inPos] | ((size_t)(src[inPos + 1] & 0x0F) << 8);
            inPos += 2;
            if (off == 0 || off > outPos || outPos + len > origSize) return false;
            for (size_t i = 0; i < len; ++i) {
                dst[outPos] = dst[outPos - off];
                outPos++;
            }
        } else {
            size_t len = (size_t)tag;
            if (len == 0) continue;
            if (inPos + len > srcSize || outPos + len > origSize) return false;
            memcpy(&dst[outPos], &src[inPos], len);
            inPos += len;
            outPos += len;
        }
    }
    return (outPos == origSize);
}