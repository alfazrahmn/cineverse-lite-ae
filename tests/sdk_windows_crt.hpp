// Offline GCC-only CRT adapter. The production project uses Microsoft's real CRT.
#pragma once
#include <cstring>
#include <cstddef>
constexpr std::size_t _TRUNCATE=static_cast<std::size_t>(-1);
inline int strncpy_s(char* dst,std::size_t size,const char* src,std::size_t) {
    if(!dst || !src || !size)return 1;
    std::strncpy(dst,src,size-1);dst[size-1]=0;return 0;
}
