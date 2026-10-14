#pragma once
#include <cstddef>
#include <cstdint>
#include "Settings.generated.hpp"
namespace cineverse {
struct Vec3 { float x,y,z; };
struct Vec4 { float x,y,z,w; };
struct Prepared {
    Settings s;
    int full_width = 3840, full_height = 2160, frame = 0;
    float scale_x = 1, scale_y = 1;
    float exposure_gain = 1;
    Vec3 wb_target{1,1,1}, wb_base{1,1,1};
    int passthrough = 1;
    int input_profile = -1, input_gamut = 0, input_gamma = 0, capture_id = 0, neutral = 1;
};
Prepared prepare(const Settings&, int width, int height, int frame, float scale_x=1, float scale_y=1);
Vec3 process_rgb(Vec3 rgb, const Prepared&, float x, float y);
Vec3 process_associated(Vec3 rgb, float alpha, const Prepared&, float x, float y);
// BGRA float32 on the GPU; byte pitches and origins are explicit.
struct GPUFrame {
    std::int64_t src_pitch, dst_pitch;
    int src_width, src_height, dst_width, dst_height;
    int src_origin_x, src_origin_y, dst_origin_x, dst_origin_y;
};
static_assert(sizeof(int)==4 && sizeof(float)==4, "Requires 32-bit scalars");
static_assert(sizeof(Settings)==60 && sizeof(GPUFrame)==48, "CPU/GPU layout contract");
}
