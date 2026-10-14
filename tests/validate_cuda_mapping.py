"""Execute the CUDA pixel body as ordinary CPU C++ to check layout/coordinate logic.
This is deliberately NOT a CUDA compiler or GPU test.
"""
from pathlib import Path
import subprocess,json
ROOT=Path(__file__).resolve().parents[1]
build=ROOT/'test-output';build.mkdir(exist_ok=True)
source=(ROOT/'src/CineverseLiteCUDA.cu').read_text()
source=source.split('extern "C" int cvl_cuda_launch')[0].replace('#include <cuda_runtime.h>','')
prefix='''
#define __CUDACC__ 1
#define __device__
#define __constant__
#define __global__
#define __forceinline__ inline
#include <cmath>
#include <vector>
#include <iostream>
#include <stdexcept>
struct float4 {float x,y,z,w;};
float4 make_float4(float x,float y,float z,float w){return {x,y,z,w};}
struct Index {int x,y,z;};
Index blockIdx,threadIdx,blockDim{16,8,1};
'''
tests='''
int main(){try {
 int cases=0;
 for(int profile: {0,1,2,8,14}) {
  cineverse::Settings s;s.input=profile;s.neg=1;s.print=1;s.ev=.25f;s.temp=15;s.tint=-10;s.grain_on=1;
  const auto p=cineverse::prepare(s,3840,2160,17,2,2);
  cineverse::GPUFrame f{7*16,9*16,5,3,7,4,10,20,9,19};
  std::vector<float4> src(7*3),dst(9*4,make_float4(-100,-100,-100,-100));
  for(int y=0;y<3;++y)for(int x=0;x<5;++x){const float a=(x+y)%4==0?0.0f:((x+y)%4==1?.5f:1.f);src[y*7+x]=make_float4(.3f*a,.4f*a,.2f*a,a);}
  src[0]=make_float4(.8f,.5f,.2f,0); // hidden RGB
  for(int y=0;y<f.dst_height;++y)for(int x=0;x<f.dst_width;++x){
   blockIdx={x/16,y/8,0};threadIdx={x%16,y%8,0};
   render(reinterpret_cast<const char*>(src.data()),reinterpret_cast<char*>(dst.data()),f,p);
   int ix=x+f.dst_origin_x-f.src_origin_x,iy=y+f.dst_origin_y-f.src_origin_y;
   auto in=make_float4(0,0,0,0);
   if(ix>=0&&iy>=0&&ix<f.src_width&&iy<f.src_height)in=src[iy*7+ix];
   const auto e=cineverse::process_associated({in.z,in.y,in.x},in.w,p,(x+f.dst_origin_x)*p.scale_x,(y+f.dst_origin_y)*p.scale_y);
   const auto got=dst[y*9+x];
   if(std::fabs(got.x-e.z)>3e-6f||std::fabs(got.y-e.y)>3e-6f||std::fabs(got.z-e.x)>3e-6f||got.w!=in.w)throw std::runtime_error("BGRA/alpha/origin mismatch");
  }
  for(int y=0;y<4;++y)for(int x=7;x<9;++x)if(dst[y*9+x].w!=-100)throw std::runtime_error("Wrote padding");
  ++cases;
 }
 std::cout<<"PASSED "<<cases<<" offline CUDA-body mapping cases; no CUDA compiler or GPU execution.\\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\\n';return 1;}}
'''
(build/'cuda_body.cpp').write_text(prefix+source+tests)
subprocess.run(['g++','-std=c++17','-O2','-ffp-contract=off','-I'+str(ROOT/'src'),str(build/'cuda_body.cpp'),str(ROOT/'src/Engine.cpp'),'-o',str(build/'cuda_body')],check=True)
r=subprocess.run([str(build/'cuda_body')],check=True,capture_output=True,text=True)
print(r.stdout.strip())
(ROOT/'VALIDATION_CUDA_MAPPING.json').write_text(json.dumps(dict(scope='Offline CPU execution of CUDA pixel body only',passed=5,failed=0,
 checks=['BGRA channel order','Independent byte pitches','Cropped buffer origins','Alpha association and hidden RGB','Full-resolution coordinates with preview scaling'],
 cuda_compiled=False,gpu_executed=False,results=r.stdout.strip()),indent=2)+'\n')
