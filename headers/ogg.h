#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "utf8.h"

#ifndef SAMPLE_RATE
#define SAMPLE_RATE 44100
#endif

#ifndef NUM_CHANNELS
#define NUM_CHANNELS 2
#endif

#ifndef ma_uint64
typedef uint64_t ma_uint64;
#endif

#undef CSEQ_HAS_STB_VORBIS

#if defined(__has_include)
  #if __has_include("vorbis.c")
    #define CSEQ_HAS_STB_VORBIS 1
    #define STB_VORBIS_HEADER_ONLY
    #include "vorbis.c"
    #undef STB_VORBIS_HEADER_ONLY
  #endif
#endif

#if !defined(CSEQ_HAS_STB_VORBIS)
  #if defined(_MSC_VER)
    #pragma message("[NOTICE] vorbis.c not found: OGG decoding disabled. Place vorbis.c in the project folder to enable.")
  #elif defined(__GNUC__) || defined(__clang__)
    #pragma message "[NOTICE] vorbis.c not found: OGG decoding disabled. Place vorbis.c in the project folder to enable."
  #endif
#endif




typedef struct {
    
    const char          *filepath;
    const unsigned char *data;      
    size_t               size;

    
    float               *pcm;
    ma_uint64            frames;    
    int                  channels;
    int                  sample_rate;

    
#if defined(CSEQ_HAS_STB_VORBIS)
    stb_vorbis          *vorb;
#else
    void                *vorb;
#endif
    int                  vorb_channels;
    int                  vorb_sample_rate;
    ma_uint64            total_samples;
    bool                 opened;
} OggDecoder;


typedef OggDecoder OggOpusDecoder;


static inline bool ogg_open(OggDecoder *d, const char *path);
#define ogg_opus_open ogg_open


static inline bool ogg_open_memory(OggDecoder *d, const unsigned char *data, size_t size);
#define ogg_opus_open_memory ogg_open_memory


static inline bool ogg_decode_all(OggDecoder *d);
#define ogg_opus_decode_all ogg_decode_all


static inline void ogg_close(OggDecoder *d);
#define ogg_opus_close ogg_close

#if defined(CSEQ_HAS_STB_VORBIS)

static inline void ogg_convert_to_stereo(const float *in, int in_channels, ma_uint64 frames, float *out) {
    if (!in || !out || frames == 0) return;

    if (in_channels == 2) {
        memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }

    if (in_channels == 1) {
        for (ma_uint64 i = 0; i < frames; ++i) {
            float mono = in[i];
            out[i * 2 + 0] = mono;
            out[i * 2 + 1] = mono;
        }
        return;
    }

    if (in_channels == 6) {
        for (ma_uint64 i = 0; i < frames; ++i) {
            const float *src = in + (i * 6);
            float l   = src[0];
            float c   = src[1];
            float r   = src[2];
            float ls  = src[3];
            float rs  = src[4];
            float lfe = src[5];
            out[i * 2 + 0] = (l + 0.7071f * c + 0.7071f * ls + 0.5f * lfe) * 0.5f;
            out[i * 2 + 1] = (r + 0.7071f * c + 0.7071f * rs + 0.5f * lfe) * 0.5f;
        }
        return;
    }

    for (ma_uint64 i = 0; i < frames; ++i) {
        const float *src = in + (i * in_channels);
        float sum_l = 0.0f;
        float sum_r = 0.0f;
        for (int c = 0; c < in_channels; ++c) {
            if (c % 2 == 0) sum_l += src[c];
            else            sum_r += src[c];
        }
        out[i * 2 + 0] = sum_l / (float)((in_channels + 1) / 2);
        out[i * 2 + 1] = sum_r / (float)(in_channels / 2 > 0 ? in_channels / 2 : 1);
    }
}

static inline void ogg_resample_linear(const float *in, int channels, int in_rate,
                                       float *out, int out_rate,
                                       ma_uint64 in_frames, ma_uint64 *out_frames) {
    if (!in || !out || in_frames == 0) {
        if (out_frames) *out_frames = 0;
        return;
    }

    if (in_rate == out_rate) {
        if (out_frames) *out_frames = in_frames;
        memcpy(out, in, in_frames * channels * sizeof(float));
        return;
    }

    double ratio = (double)in_rate / (double)out_rate;
    ma_uint64 out_f = (ma_uint64)round((double)in_frames / ratio);
    if (out_f == 0) out_f = 1;
    if (out_frames) *out_frames = out_f;

    for (ma_uint64 i = 0; i < out_f; ++i) {
        double src_pos = (double)i * ratio;
        ma_uint64 idx0 = (ma_uint64)src_pos;
        ma_uint64 idx1 = idx0 + 1;
        float frac = (float)(src_pos - (double)idx0);

        if (idx0 >= in_frames) idx0 = in_frames - 1;
        if (idx1 >= in_frames) idx1 = in_frames - 1;

        for (int c = 0; c < channels; ++c) {
            float v0 = in[idx0 * channels + c];
            float v1 = in[idx1 * channels + c];
            out[i * channels + c] = v0 + (v1 - v0) * frac;
        }
    }
}

static inline bool ogg_open(OggDecoder *d, const char *path) {
    if (!d || !path) return false;
    memset(d, 0, sizeof(OggDecoder));

     
    int error = 0;
    FILE *fp = fopen_utf8(path, L"rb");
    if (fp) {
        d->vorb = stb_vorbis_open_file(fp, 1, &error, NULL);
    } else {
        d->vorb = stb_vorbis_open_filename(path, &error, NULL);
    }
    if (!d->vorb) return false;

    stb_vorbis_info info = stb_vorbis_get_info(d->vorb);
    d->vorb_channels     = info.channels;
    d->vorb_sample_rate  = (int)info.sample_rate;
    d->total_samples     = (ma_uint64)stb_vorbis_stream_length_in_samples(d->vorb);
    d->filepath          = path;
    d->opened            = true;

    return true;
}

static inline bool ogg_open_memory(OggDecoder *d, const unsigned char *data, size_t size) {
    if (!d || !data || size == 0) return false;
    memset(d, 0, sizeof(OggDecoder));

    int error = 0;
    d->vorb = stb_vorbis_open_memory(data, (int)size, &error, NULL);
    if (!d->vorb) return false;

    stb_vorbis_info info = stb_vorbis_get_info(d->vorb);
    d->vorb_channels     = info.channels;
    d->vorb_sample_rate  = (int)info.sample_rate;
    d->total_samples     = (ma_uint64)stb_vorbis_stream_length_in_samples(d->vorb);
    d->data              = data;
    d->size              = size;
    d->opened            = true;

    return true;
}

static inline bool ogg_decode_all(OggDecoder *d) {
    if (!d || !d->opened || !d->vorb) return false;
    if (d->vorb_channels <= 0 || d->vorb_sample_rate <= 0) return false;

    ma_uint64 capacity = (d->total_samples > 0) ? d->total_samples : 16384;
    float *raw_pcm = (float *)malloc(capacity * d->vorb_channels * sizeof(float));
    if (!raw_pcm) return false;

    ma_uint64 raw_frames = 0;
    const int CHUNK_FRAMES = 4096;

    for (;;) {
        if (raw_frames + CHUNK_FRAMES > capacity) {
            ma_uint64 new_cap = capacity * 2;
            if (new_cap < raw_frames + CHUNK_FRAMES) new_cap = raw_frames + CHUNK_FRAMES;
            float *new_buf = (float *)realloc(raw_pcm, new_cap * d->vorb_channels * sizeof(float));
            if (!new_buf) {
                free(raw_pcm);
                return false;
            }
            raw_pcm = new_buf;
            capacity = new_cap;
        }

        int read_samples = stb_vorbis_get_samples_float_interleaved(
            d->vorb,
            d->vorb_channels,
            raw_pcm + (raw_frames * d->vorb_channels),
            CHUNK_FRAMES * d->vorb_channels
        );

        if (read_samples <= 0) break;
        raw_frames += (ma_uint64)read_samples;
    }

    if (raw_frames == 0) {
        free(raw_pcm);
        return false;
    }

    if (d->vorb_channels != NUM_CHANNELS) {
        float *stereo = (float *)malloc(raw_frames * NUM_CHANNELS * sizeof(float));
        if (!stereo) {
            free(raw_pcm);
            return false;
        }
        ogg_convert_to_stereo(raw_pcm, d->vorb_channels, raw_frames, stereo);
        free(raw_pcm);
        raw_pcm = stereo;
    }

    if (d->vorb_sample_rate != SAMPLE_RATE) {
        ma_uint64 out_frames = 0;
        ma_uint64 out_alloc = (ma_uint64)ceil((double)raw_frames * (double)SAMPLE_RATE / (double)d->vorb_sample_rate) + 16;
        float *resampled = (float *)malloc(out_alloc * NUM_CHANNELS * sizeof(float));
        if (!resampled) {
            free(raw_pcm);
            return false;
        }
        ogg_resample_linear(raw_pcm, NUM_CHANNELS, d->vorb_sample_rate,
                            resampled, SAMPLE_RATE,
                            raw_frames, &out_frames);
        free(raw_pcm);
        raw_pcm = resampled;
        raw_frames = out_frames;
    }

    d->pcm         = raw_pcm;
    d->frames      = raw_frames;
    d->channels    = NUM_CHANNELS;
    d->sample_rate = SAMPLE_RATE;

    return true;
}

static inline void ogg_close(OggDecoder *d) {
    if (!d) return;
    if (d->vorb) {
        stb_vorbis_close(d->vorb);
        d->vorb = NULL;
    }
    if (d->pcm) {
        free(d->pcm);
        d->pcm = NULL;
    }
    d->frames = 0;
    d->opened = false;
}

#else

static inline bool ogg_open(OggDecoder *d, const char *path) { (void)d; (void)path; return false; }
static inline bool ogg_open_memory(OggDecoder *d, const unsigned char *data, size_t size) { (void)d; (void)data; (void)size; return false; }
static inline bool ogg_decode_all(OggDecoder *d) { (void)d; return false; }
static inline void ogg_close(OggDecoder *d) { (void)d; }

#endif