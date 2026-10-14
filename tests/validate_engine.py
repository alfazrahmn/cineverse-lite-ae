"""Offline float32 port comparisons. This does not execute After Effects or a GPU."""
from pathlib import Path
import ctypes as C, hashlib, json, re, subprocess, sys
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'test-output';BUILD.mkdir(exist_ok=True)
spec=json.loads((ROOT/'CONTROL_SPEC.json').read_text())
class Settings(C.Structure):
    _fields_=[(u['field'],C.c_float if u['kind']=='DCTLUI_SLIDER_FLOAT' else C.c_int) for u in spec]
    def __init__(self,**kwargs):
        super().__init__()
        for u in spec:setattr(self,u['field'],float(u['default'].rstrip('f')) if u['kind']=='DCTLUI_SLIDER_FLOAT' else int(u['default']))
        for k,v in kwargs.items():setattr(self,k,v)

original=(ROOT/'reference/Cineverse_Lite_v1.dctl').read_text()
original='\n'.join(l for l in original.splitlines() if not l.startswith('DEFINE_UI_PARAMS('))
loc='\n'.join(('float' if u['kind']=='DCTLUI_SLIDER_FLOAT' else 'int')+' '+u['source_id']+'=s.'+u['field']+';' for u in spec)
original=original.replace('float3 transform(int p_Width, int p_Height, int p_X, int p_Y, float p_R, float p_G, float p_B)',
    'float3 transform(const cineverse::Settings& s,int TIMELINE_FRAME_INDEX,int p_Width, int p_Height, int p_X, int p_Y, float p_R, float p_G, float p_B)')
original=original.replace('\n{\n float3 raw=make_float3(p_R,p_G,p_B);','\n{\n'+loc+'\n float3 raw=make_float3(p_R,p_G,p_B);')
shim='''
#include "Engine.hpp"
#include "Compat.hpp"
#include <algorithm>
namespace reference {
using namespace cineverse;
using float3=Vec3;using float4=Vec4;
inline float3 make_float3(float x,float y,float z){return {x,y,z};}
inline float4 make_float4(float x,float y,float z,float w){return {x,y,z,w};}
'''
wrapper='''
}
extern "C" void cvl_reference(const cineverse::Settings* s,const float* in,float* out,int n,int width,int frame){
 for(int j=0;j<n;++j){const auto v=reference::transform(*s,frame,width,2160,j%width,j/width,in[3*j],in[3*j+1],in[3*j+2]);out[3*j]=v.x;out[3*j+1]=v.y;out[3*j+2]=v.z;}}
extern "C" void cvl_port(const cineverse::Settings* s,const float* in,float* out,int n,int width,int frame){
 const auto p=cineverse::prepare(*s,width,2160,frame);
 for(int j=0;j<n;++j){const auto v=cineverse::process_rgb({in[3*j],in[3*j+1],in[3*j+2]},p,static_cast<float>(j%width),static_cast<float>(j/width));out[3*j]=v.x;out[3*j+1]=v.y;out[3*j+2]=v.z;}}
extern "C" void cvl_associated(const cineverse::Settings* s,const float* in,float* out,int n,float scale,int frame){
 const auto p=cineverse::prepare(*s,3840,2160,frame,scale,scale);
 for(int j=0;j<n;++j){const auto v=cineverse::process_associated({in[4*j],in[4*j+1],in[4*j+2]},in[4*j+3],p,j*scale,0);out[4*j]=v.x;out[4*j+1]=v.y;out[4*j+2]=v.z;out[4*j+3]=in[4*j+3];}}
'''
(BUILD/'reference.cpp').write_text(shim+original+wrapper)
cmd=['g++','-std=c++17','-O2','-ffp-contract=off','-shared','-fPIC','-I'+str(ROOT/'src'),str(BUILD/'reference.cpp'),str(ROOT/'src/Engine.cpp'),'-o',str(BUILD/'reference.so')]
subprocess.run(cmd,check=True)
lib=C.CDLL(str(BUILD/'reference.so'));P=C.POINTER(C.c_float)
for fn in ('cvl_reference','cvl_port'):getattr(lib,fn).argtypes=[C.POINTER(Settings),P,P,C.c_int,C.c_int,C.c_int]
lib.cvl_associated.argtypes=[C.POINTER(Settings),P,P,C.c_int,C.c_float,C.c_int]
def run(s,x,fn='cvl_port',width=3840,frame=17):
    x=np.ascontiguousarray(x,np.float32);y=np.empty_like(x)
    getattr(lib,fn)(C.byref(s),x.ctypes.data_as(P),y.ctypes.data_as(P),len(x),width,frame)
    return y
checks=[]
def check(name,condition,**details):
    checks.append(dict(name=name,passed=bool(condition),**details))
    if not condition:raise AssertionError(name+': '+str(details))

rng=np.random.default_rng(260214)
x=rng.uniform(0,1,(256,3)).astype(np.float32)
x[:8]=[[0,0,0],[1,1,1],[.3360433]*3,[.5,.3,.2],[1,0,0],[0,1,0],[0,0,1],[.002,.001,.003]]
settings=[{},dict(ev=.75,contrast=.15,temp=20,tint=-17,sat=1.1,fsat=1.4),
 dict(ev=-1,contrast=-.25,pivot=.4,temp=-40,tint=0,sat=.7,fsat=.2,neg=1,neg_strength=.5,print=1,print_strength=.5),
 dict(neg=1,neg_strength=1,print=1,print_strength=1,grain_on=1,grain_amount=.75)]
max_error=0.0
for profile in range(15):
 for output in (0,1):
  for variant,values in enumerate(settings):
   s=Settings(input=profile,output=output,**values)
   a=run(s,x,'cvl_reference');b=run(s,x)
   error=float(np.max(np.abs(a-b)));max_error=max(max_error,error)
   check(f'profile_{profile}_output_{output}_variant_{variant}',np.isfinite(b).all() and error<=3e-6,max_absolute_error=error)
 print('Checked input profile',profile,flush=True)

hdr=np.array([[-.1,.2,2.0],[.0001,.18,12.0],[2.0,3.0,4.0]],np.float32)
check('default_is_exact_identity',np.array_equal(run(Settings(),hdr),hdr))
check('neutral_rec709_is_exact_identity',np.array_equal(run(Settings(input=1),hdr),hdr))
for output in (0,1):
 check(f'hdr_reference_output_{output}',np.allclose(run(Settings(input=1,output=output,ev=.5),hdr),run(Settings(input=1,output=output,ev=.5),hdr,'cvl_reference'),atol=3e-6,rtol=0))

rgba=np.column_stack([x[:16],np.linspace(0,1,16)]).astype(np.float32)
rgba[:,:3]*=rgba[:,3:]
def associated(s,a,scale=1,frame=17):
 a=np.ascontiguousarray(a,np.float32);b=np.empty_like(a)
 lib.cvl_associated(C.byref(s),a.ctypes.data_as(P),b.ctypes.data_as(P),len(a),scale,frame);return b
s=Settings(input=1,ev=.8,neg=1,print=1)
y=associated(s,rgba)
check('alpha_preserved',np.array_equal(y[:,3],rgba[:,3]))
expected=run(s,rgba[1:,:3]/rgba[1:,3:])*rgba[1:,3:]
check('premultiplication_matches_straight_reference',np.allclose(y[1:,:3],expected,atol=3e-6,rtol=0))
hidden=np.array([[.4,-.1,2.,0]],np.float32)
check('hidden_zero_alpha_rgb_preserved',np.array_equal(associated(s,hidden),hidden))
check('associated_identity_bit_exact',np.array_equal(associated(Settings(),rgba),rgba))

s=Settings(input=2,grain_on=1,grain_amount=1)
clean=np.full((64,3),.3360433,np.float32)
one=run(s,clean,frame=7);two=run(s,clean,frame=8)
check('grain_changes_each_frame',not np.array_equal(one,two))
check('grain_repeated_frame_deterministic',np.array_equal(one,run(s,clean,frame=7)))
a=np.column_stack([clean,np.ones(64)]).astype(np.float32)
half=associated(s,a,scale=2,frame=7)
full=run(s,np.full((128,3),.3360433,np.float32),frame=7)
check('half_preview_uses_full_resolution_grain_coordinates',np.allclose(half[:,:3],full[::2],atol=1e-7,rtol=0))

from concurrent.futures import ThreadPoolExecutor
jobs=[Settings(input=2,ev=j/10,temp=j*3,tint=j-5,neg=j%2,print=1,grain_on=1) for j in range(12)]
serial=[run(s,x,frame=j) for j,s in enumerate(jobs)]
with ThreadPoolExecutor(max_workers=6) as pool:
 parallel=list(pool.map(lambda pair:run(pair[1],x,frame=pair[0]),enumerate(jobs)))
check('concurrent_parameter_blocks_match_serial',all(np.array_equal(a,b) for a,b in zip(serial,parallel)))
check('stock_defaults_half_strength',Settings().neg_strength==.5 and Settings().print_strength==.5)
check('controls_exact_order_and_count',len(spec)==15 and [u['field'] for u in spec]==['input','output','ev','contrast','pivot','temp','tint','sat','fsat','neg','neg_strength','print','print_strength','grain_on','grain_amount'])
report=dict(scope='Offline Linux float32 C++ execution, not native AE or GPU execution',checks=checks,
            maximum_port_vs_reference_absolute_error=max_error,passed=len(checks),failed=0,
            native_after_effects_tested=False,cuda_compiled_or_executed=False,
            source_sha256=hashlib.sha256((ROOT/'reference/Cineverse_Lite_v1.dctl').read_bytes()).hexdigest())
(ROOT/'VALIDATION_ENGINE.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='checks'}))
