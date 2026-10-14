#pragma once
#include <cmath>
#ifdef __CUDACC__
#define __DEVICE__ static __device__ __forceinline__
#define __CONSTANT__ static __device__ __constant__ const
#else
#define __DEVICE__ static inline
#define __CONSTANT__ static const
#endif
#include "Engine.hpp"
namespace cineverse {
using uint=unsigned int;
__DEVICE__ Vec3 makeVec3(float x,float y,float z){return {x,y,z};}
__DEVICE__ Vec4 makeVec4(float x,float y,float z,float w){return {x,y,z,w};}
__DEVICE__ float _clampf(float x,float a,float b){return x<a?a:(x>b?b:x);}
__DEVICE__ float _powf(float x,float y){return ::powf(x,y);}
__DEVICE__ float _logf(float x){return ::logf(x);}
__DEVICE__ float _log2f(float x){return ::log2f(x);}
__DEVICE__ float _log10f(float x){return ::log10f(x);}
__DEVICE__ float _expf(float x){return ::expf(x);}
__DEVICE__ float _exp2f(float x){return ::exp2f(x);}
__DEVICE__ float _fmaxf(float x,float y){return ::fmaxf(x,y);}
__DEVICE__ float _fminf(float x,float y){return ::fminf(x,y);}
__DEVICE__ float _fabs(float x){return ::fabsf(x);}
__DEVICE__ float _sqrtf(float x){return ::sqrtf(x);}
__DEVICE__ float _sinf(float x){return ::sinf(x);}
__DEVICE__ float _cosf(float x){return ::cosf(x);}
__DEVICE__ float _atan2f(float x,float y){return ::atan2f(x,y);}
__DEVICE__ float _floorf(float x){return ::floorf(x);}
}
