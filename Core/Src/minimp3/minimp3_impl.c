/*
 * minimp3 (https://github.com/lieff/minimp3, CC0 / public domain)
 * Implementation is compiled here once. The decoder is always built with -O2,
 * even in the Debug configuration (-O0 would be too slow for real time).
 */
#pragma GCC optimize ("O2")

#define MINIMP3_NO_SIMD            /* Cortex-M7 has no NEON / SSE */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
