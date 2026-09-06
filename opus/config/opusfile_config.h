/* libopusfile build configuration for cseq (MSVC x64).
 *
 * Placed at opus/opusfile/src/config.h so the vendored opusfile sources'
 * `#include "config.h"` resolves here via the current-file-directory rule.
 *
 * All optional features are left off:
 *  - No HTTP support (OP_ENABLE_HTTP undefined) -> no openssl/winsock deps.
 *  - Floating-point API stays enabled (OP_DISABLE_FLOAT_API undefined).
 *  - Fixed-point build off (OP_FIXED_POINT undefined).
 *  - Assertions off (OP_ENABLE_ASSERTIONS undefined).
 *  - HAVE_LRINTF undefined (MSVC lacks lrintf).
 */
