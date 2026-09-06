#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

// Forward declaration of internal reader state to avoid leaking
// opusfile / ogg headers into the rest of the application.
// Named OpusWrapDecoder (not OpusDecoder) to avoid colliding with the
// opaque `typedef struct OpusDecoder OpusDecoder;` from libopus's opus.h.
typedef struct OpusWrapDecoder {
    float*    pcm;       // Interleaved stereo PCM frames at SAMPLE_RATE
    uint64_t  frames;    // Total decoded frames at SAMPLE_RATE
    void*     internal;  // Internal OggOpusFile pointer
    void*     file;      // Internal FILE* handle (if file-backed)
} OpusWrapDecoder;

#ifdef __cplusplus
extern "C" {
#endif

// Opens an Opus stream from a UTF-8 filesystem path.
// Returns true on success; call opus_close() when finished.
bool opus_open(OpusWrapDecoder* d, const char* filepath);

// Opens an Opus stream from a memory buffer.
// The memory buffer must remain valid until opus_close() is called.
bool opus_open_memory(OpusWrapDecoder* d, const void* data, size_t size);

// Decodes the entire stream to 32-bit float stereo PCM resampled to SAMPLE_RATE.
// Populates d->pcm and d->frames. Returns true on success.
bool opus_decode_all(OpusWrapDecoder* d);

// Cleans up the decoder stream. If d->pcm was not adopted by the caller,
// it is freed here.
void opus_close(OpusWrapDecoder* d);

#ifdef __cplusplus
}
#endif