#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "opus_wrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Include the Xiph opusfile header
#include "opusfile.h"

// --- Stream Callbacks (UTF-8 Safe on Windows) --------------------------------

static int op_cb_read(void* stream, unsigned char* ptr, int nbytes) {
    return (int)fread(ptr, 1, (size_t)nbytes, (FILE*)stream);
}

static int op_cb_seek(void* stream, opus_int64 offset, int whence) {
#ifdef _WIN32
    return _fseeki64((FILE*)stream, offset, whence);
#else
    return fseeko((FILE*)stream, offset, whence);
#endif
}

static opus_int64 op_cb_tell(void* stream) {
#ifdef _WIN32
    return _ftelli64((FILE*)stream);
#else
    return ftello((FILE*)stream);
#endif
}

static int op_cb_close(void* stream) {
    return fclose((FILE*)stream);
}

static const OpusFileCallbacks kOpusCallbacks = {
    op_cb_read,
    op_cb_seek,
    op_cb_tell,
    op_cb_close
};

// --- Resampling Helper (48 kHz -> SAMPLE_RATE) -------------------------------

// Opus decoded audio is strictly 48 kHz. If the host engine runs at 44.1 kHz
// or another sample rate, resample using fractional cubic Hermite interpolation.
static inline float hermite_interp(float p0, float p1, float p2, float p3, float t) {
    float c0 = p1;
    float c1 = 0.5f * (p2 - p0);
    float c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static float* opus_resample_stereo_48k(const float* inPcm, uint64_t inFrames,
                                       uint32_t dstRate, uint64_t* outFrames) {
    if (dstRate == 48000) {
        *outFrames = inFrames;
        float* copy = (float*)malloc((size_t)inFrames * 2 * sizeof(float));
        if (copy) memcpy(copy, inPcm, (size_t)inFrames * 2 * sizeof(float));
        return copy;
    }

    double ratio = (double)dstRate / 48000.0;
    uint64_t targetFrames = (uint64_t)floor((double)inFrames * ratio + 0.5);
    if (targetFrames == 0) return NULL;

    float* outPcm = (float*)malloc((size_t)targetFrames * 2 * sizeof(float));
    if (!outPcm) return NULL;

    for (uint64_t i = 0; i < targetFrames; ++i) {
        double srcPos = (double)i / ratio;
        int64_t i1 = (int64_t)srcPos;
        float frac = (float)(srcPos - (double)i1);

        int64_t i0 = (i1 > 0) ? i1 - 1 : 0;
        int64_t i2 = (i1 + 1 < (int64_t)inFrames) ? i1 + 1 : (int64_t)inFrames - 1;
        int64_t i3 = (i1 + 2 < (int64_t)inFrames) ? i1 + 2 : i2;

        outPcm[i * 2 + 0] = hermite_interp(inPcm[i0 * 2 + 0], inPcm[i1 * 2 + 0],
                                           inPcm[i2 * 2 + 0], inPcm[i3 * 2 + 0], frac);
        outPcm[i * 2 + 1] = hermite_interp(inPcm[i0 * 2 + 1], inPcm[i1 * 2 + 1],
                                           inPcm[i2 * 2 + 1], inPcm[i3 * 2 + 1], frac);
    }

    *outFrames = targetFrames;
    return outPcm;
}

// --- Public API --------------------------------------------------------------

// Reads stream metadata from the opened OggOpusFile into the decoder struct.
static void opus_read_meta(OpusWrapDecoder* d, OggOpusFile* of) {
    const OpusHead* head = op_head(of, 0);
    d->channels = op_channel_count(of, 0);
    d->sample_rate = (head && head->input_sample_rate > 0) ? (int)head->input_sample_rate : 48000;
    ogg_int64_t total = op_pcm_total(of, -1);
    d->total_frames_48k = (total > 0) ? (uint64_t)total : 0;
}

bool opus_open(OpusWrapDecoder* d, const char* filepath) {
    if (!d || !filepath || !filepath[0]) return false;
    memset(d, 0, sizeof(OpusWrapDecoder));

    FILE* fp = NULL;
#ifdef _WIN32
    wchar_t wpath[MAX_PATH * 2];
    if (MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) > 0) {
        fp = _wfopen(wpath, L"rb");
    }
#else
    fp = fopen(filepath, "rb");
#endif
    if (!fp) return false;

    int err = 0;
    OggOpusFile* of = op_open_callbacks(fp, &kOpusCallbacks, NULL, 0, &err);
    if (!of || err != 0) {
        fclose(fp);
        return false;
    }

    d->internal = (void*)of;
    d->file = (void*)fp;
    opus_read_meta(d, of);
    return true;
}

bool opus_open_memory(OpusWrapDecoder* d, const void* data, size_t size) {
    if (!d || !data || size == 0) return false;
    memset(d, 0, sizeof(OpusWrapDecoder));

    int err = 0;
    OggOpusFile* of = op_open_memory((const unsigned char*)data, size, &err);
    if (!of || err != 0) return false;

    d->internal = (void*)of;
    opus_read_meta(d, of);
    return true;
}

bool opus_decode_all(OpusWrapDecoder* d) {
    if (!d || !d->internal) return false;
    OggOpusFile* of = (OggOpusFile*)d->internal;

    ogg_int64_t totalFrames48k = op_pcm_total(of, -1);
    if (totalFrames48k <= 0) return false;

    size_t totalFloats48k = (size_t)totalFrames48k * 2;
    float* pcm48k = (float*)malloc(totalFloats48k * sizeof(float));
    if (!pcm48k) return false;

    ogg_int64_t framesRead = 0;
    while (framesRead < totalFrames48k) {
        int read = op_read_float_stereo(of, pcm48k + framesRead * 2,
                                        (int)(totalFrames48k - framesRead) * 2);
        if (read <= 0) break;
        framesRead += read;
    }

    if (framesRead == 0) {
        free(pcm48k);
        return false;
    }

    // Convert from 48 kHz to engine SAMPLE_RATE
    uint64_t finalFrames = 0;
    float* finalPcm = opus_resample_stereo_48k(pcm48k, (uint64_t)framesRead,
                                               (uint32_t)SAMPLE_RATE, &finalFrames);
    free(pcm48k);

    if (!finalPcm || finalFrames == 0) return false;

    d->pcm = finalPcm;
    d->frames = finalFrames;
    return true;
}

void opus_close(OpusWrapDecoder* d) {
    if (!d) return;
    if (d->internal) {
        op_free((OggOpusFile*)d->internal);
        d->internal = NULL;
    }
    // op_free automatically invokes the close callback on the FILE*
    d->file = NULL;

    if (d->pcm) {
        free(d->pcm);
        d->pcm = NULL;
    }
    d->frames = 0;
}