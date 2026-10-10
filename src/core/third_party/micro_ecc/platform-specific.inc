/* micro-ecc platform shim for Cal Transfer (modified from BSD-2 upstream).
 * No platform default RNG: clip46_p256.c installs an explicit checked OS RNG.
 * Avoids platform-specific C99 declarations and silent RNG failures.
 */
#include "types.h"
