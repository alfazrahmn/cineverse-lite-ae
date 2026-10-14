#include <cuda_runtime.h>
#include "Core.generated.hpp"
namespace {
__global__ void render(const char* src,char* dst,cineverse::GPUFrame frame,cineverse::Prepared p) {
    const int x=blockIdx.x*blockDim.x+threadIdx.x,y=blockIdx.y*blockDim.y+threadIdx.y;
    if(x>=frame.dst_width || y>=frame.dst_height)return;
    const int ix=x+frame.dst_origin_x-frame.src_origin_x,iy=y+frame.dst_origin_y-frame.src_origin_y;
    float4 pixel=make_float4(0,0,0,0); // BGRA, unlike the CPU's ARGB structs
    if(src && ix>=0 && iy>=0 && ix<frame.src_width && iy<frame.src_height)
        pixel=reinterpret_cast<const float4*>(src+static_cast<long long>(iy)*frame.src_pitch)[ix];
    if(!p.passthrough && pixel.w>0.0f) {
        const cineverse::Vec3 rgb=cineverse::transform(p,(x+frame.dst_origin_x)*p.scale_x,(y+frame.dst_origin_y)*p.scale_y,
          pixel.z/pixel.w,pixel.y/pixel.w,pixel.x/pixel.w);
        pixel.x=rgb.z*pixel.w;pixel.y=rgb.y*pixel.w;pixel.z=rgb.x*pixel.w;
    }
    reinterpret_cast<float4*>(dst+static_cast<long long>(y)*frame.dst_pitch)[x]=pixel;
}
}
extern "C" int cvl_cuda_launch(void* queue,const void* src,void* dst,cineverse::GPUFrame frame,const cineverse::Prepared* p) {
    if(frame.dst_width<=0 || frame.dst_height<=0)return 0;
    const auto stream=static_cast<cudaStream_t>(queue);
    if(p->passthrough && src && frame.src_width==frame.dst_width && frame.src_height==frame.dst_height
      && frame.src_origin_x==frame.dst_origin_x && frame.src_origin_y==frame.dst_origin_y
      && frame.src_pitch>0 && frame.dst_pitch>0) {
        const cudaError_t copy=cudaMemcpy2DAsync(dst,static_cast<size_t>(frame.dst_pitch),src,
          static_cast<size_t>(frame.src_pitch),static_cast<size_t>(frame.dst_width)*sizeof(float4),
          frame.dst_height,cudaMemcpyDeviceToDevice,stream);
        if(copy!=cudaSuccess)return static_cast<int>(copy);
        return static_cast<int>(cudaStreamSynchronize(stream));
    }
    render<<<dim3((frame.dst_width+31)/32,(frame.dst_height+3)/4),dim3(32,4),0,stream>>>(
      static_cast<const char*>(src),static_cast<char*>(dst),frame,*p);
    const cudaError_t launch=cudaGetLastError();
    if(launch!=cudaSuccess)return static_cast<int>(launch);
    // Checkouts are not released until AE's supplied queue has completed this render.
    return static_cast<int>(cudaStreamSynchronize(stream));
}
