/* libopus build configuration for cseq (decoder-only, MSVC x64).
 *
 * This file is placed at opus/opus/src/config.h so that the vendored
 * libopus sources' `#include "config.h"` resolves here via the
 * current-file-directory rule. It is the minimal configuration needed to
 * compile the Opus decoder on MSVC.
 *
 * Notes:
 *  - OPUS_BUILD is required by every libopus source.
 *  - USE_ALLOCA selects stack allocation via _alloca (MSVC has no VLAs);
 *    stack_alloc.h requires exactly one of VAR_ARRAYS / USE_ALLOCA /
 *    NONTHREADSAFE_PSEUDOSTACK.
 *  - restrict -> __restrict for MSVC.
 *  - HAVE_LRINTF / HAVE_LRINT are deliberately NOT defined: MSVC lacks
 *    lrintf/lrint, so celt/float_cast.h falls back to (int)(floor(.5+x)).
 *  - Intrinsics/RTCD/ASM are left off: scalar x64 fallback.
 *  - FIXED_POINT / ENABLE_DRED / ENABLE_QEXT / ENABLE_DEEP_PLC / CUSTOM_MODES
 *    are left undefined (floating-point decoder build).
 */
#define OPUS_BUILD
#define USE_ALLOCA
#define restrict __restrict
