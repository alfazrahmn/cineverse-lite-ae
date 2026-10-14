#pragma once
#ifndef CVL_ENABLE_CUDA
#define CVL_ENABLE_CUDA 0
#endif
// Numeric PiPL and runtime values share one definition; no stale resource flags.
#define CVL_EFFECT_VERSION 65537
#define CVL_OUT_FLAGS 0x02000404
#if CVL_ENABLE_CUDA
#define CVL_OUT_FLAGS2 0x0a001401
#else
#define CVL_OUT_FLAGS2 0x08001401
#endif
