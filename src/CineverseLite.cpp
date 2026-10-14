// Native AE adapter. Adobe SDK headers are supplied separately by the developer.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <new>
#include <atomic>
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectGPUSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "entry.h"
#include "Engine.hpp"
#include "PluginFlags.h"
#if CVL_ENABLE_CUDA
#include <cuda.h>
extern "C" int cvl_cuda_launch(void*,const void*,void*,cineverse::GPUFrame,const cineverse::Prepared*);
#endif

namespace {
using cineverse::Prepared;
using cineverse::Settings;
using cineverse::Vec3;
constexpr int PARAM_COUNT=16; // input layer plus exactly 15 user controls
std::atomic<int> last_backend{0}; // Diagnostic only; never drives image processing or shared parameters.

// Suite references are local to each invocation, including MFR threads.
template<class T> class Suite {
    SPBasicSuite* basic_;
    const char* name_;
    A_long version_;
    const T* suite_=nullptr;
public:
    Suite(PF_InData* in,const char* name,A_long version):basic_(in->pica_basicP),name_(name),version_(version) {
        if(!basic_ || basic_->AcquireSuite(name,version,reinterpret_cast<const void**>(&suite_))!=kSPNoError)
            throw PF_Err_INVALID_CALLBACK;
    }
    ~Suite(){if(suite_)basic_->ReleaseSuite(name_,version_);}
    Suite(const Suite&)=delete;
    Suite& operator=(const Suite&)=delete;
    const T* operator->()const{return suite_;}
};

Settings read_values(PF_ParamDef* params[]) {
    Settings s;
#include "ReadParams.generated.inc"
    return s;
}

PF_Err checkout_settings(PF_InData* in,Settings& s) {
    PF_ParamDef storage[PARAM_COUNT]{};
    PF_ParamDef* params[PARAM_COUNT]{};
    PF_Err error=PF_Err_NONE;
    int obtained=0;
    for(int index=1;index<PARAM_COUNT;++index) {
        error=PF_CHECKOUT_PARAM(in,index,in->current_time,in->time_step,in->time_scale,&storage[index]);
        if(error)break;
        params[index]=&storage[index]; obtained=index;
    }
    if(!error)s=read_values(params);
    for(int index=obtained;index>=1;--index) {
        PF_Err cleanup=PF_CHECKIN_PARAM(in,&storage[index]);
        if(!error)error=cleanup;
    }
    return error;
}

Prepared prepare_frame(PF_InData* in,const Settings& s,int width,int height) {
    const float sx=in->downsample_x.num>0 ? static_cast<float>(in->downsample_x.den)/in->downsample_x.num : 1.0f;
    const float sy=in->downsample_y.num>0 ? static_cast<float>(in->downsample_y.den)/in->downsample_y.num : 1.0f;
    const int step=in->local_time_step>0?in->local_time_step:(in->time_step>0?in->time_step:1);
    const double frame=std::floor(static_cast<double>(in->current_time)/step);
    return cineverse::prepare(s,width,height,static_cast<int>(frame),sx,sy);
}

PF_Err parameters(PF_InData* in_data,PF_OutData* out_data) {
    PF_ParamDef def{};
#include "Params.generated.inc"
    out_data->num_params=PARAM_COUNT;
    return PF_Err_NONE;
}

struct Job { const Prepared* p; const PF_EffectWorld* src; const PF_EffectWorld* dst; };
template<class Pixel> Pixel input_pixel(const Job& j,A_long x,A_long y) {
    const A_long ix=x+j.dst->origin_x-j.src->origin_x;
    const A_long iy=y+j.dst->origin_y-j.src->origin_y;
    if(!j.src->data || ix<0 || iy<0 || ix>=j.src->width || iy>=j.src->height)return {};
    const auto* row=reinterpret_cast<const char*>(j.src->data)+static_cast<std::ptrdiff_t>(iy)*j.src->rowbytes;
    return reinterpret_cast<const Pixel*>(row)[ix];
}
Vec3 grade(const Job& j,A_long x,A_long y,Vec3 rgb,float alpha) {
    return cineverse::process_associated(rgb,alpha,*j.p,
      (x+j.dst->origin_x)*j.p->scale_x,(y+j.dst->origin_y)*j.p->scale_y);
}
template<class Channel> Channel quantize(float value,int maximum) {
    if(!std::isfinite(value))return 0;
    return static_cast<Channel>(std::floor(std::clamp(value,0.0f,1.0f)*maximum+0.5f));
}
PF_Err pixel8(void* data,A_long x,A_long y,PF_Pixel8*,PF_Pixel8* out) {
    const auto& j=*static_cast<const Job*>(data);
    const auto in=input_pixel<PF_Pixel8>(j,x,y);
    if(j.p->passthrough){*out=in;return PF_Err_NONE;}
    const auto rgb=grade(j,x,y,{in.red/255.0f,in.green/255.0f,in.blue/255.0f},in.alpha/255.0f);
    out->alpha=in.alpha;
    out->red=quantize<A_u_char>(rgb.x,255); out->green=quantize<A_u_char>(rgb.y,255); out->blue=quantize<A_u_char>(rgb.z,255);
    return PF_Err_NONE;
}
PF_Err pixel16(void* data,A_long x,A_long y,PF_Pixel16*,PF_Pixel16* out) {
    const auto& j=*static_cast<const Job*>(data);
    const auto in=input_pixel<PF_Pixel16>(j,x,y);
    if(j.p->passthrough){*out=in;return PF_Err_NONE;}
    const auto rgb=grade(j,x,y,{in.red/32768.0f,in.green/32768.0f,in.blue/32768.0f},in.alpha/32768.0f);
    out->alpha=in.alpha;
    out->red=quantize<A_u_short>(rgb.x,32768); out->green=quantize<A_u_short>(rgb.y,32768); out->blue=quantize<A_u_short>(rgb.z,32768);
    return PF_Err_NONE;
}
PF_Err pixel32(void* data,A_long x,A_long y,PF_PixelFloat*,PF_PixelFloat* out) {
    const auto& j=*static_cast<const Job*>(data);
    const auto in=input_pixel<PF_PixelFloat>(j,x,y);
    if(j.p->passthrough){*out=in;return PF_Err_NONE;}
    const auto rgb=grade(j,x,y,{in.red,in.green,in.blue},in.alpha);
    out->alpha=in.alpha; out->red=rgb.x; out->green=rgb.y; out->blue=rgb.z;
    return PF_Err_NONE;
}
PF_Err cpu_render(PF_InData* in,const Prepared& p,PF_EffectWorld* src,PF_EffectWorld* dst,PF_PixelFormat format) {
    Job job{&p,src,dst};
    // A NULL iterator input lets us explicitly align checked-out buffers with differing origins.
    switch(format) {
    case PF_PixelFormat_ARGB128: {
        Suite<PF_iterateFloatSuite2> suite(in,kPFIterateFloatSuite,kPFIterateFloatSuiteVersion2);
        return suite->iterate(in,0,dst->height,nullptr,nullptr,&job,pixel32,dst);
    }
    case PF_PixelFormat_ARGB64: {
        Suite<PF_iterate16Suite2> suite(in,kPFIterate16Suite,kPFIterate16SuiteVersion2);
        return suite->iterate(in,0,dst->height,nullptr,nullptr,&job,pixel16,dst);
    }
    case PF_PixelFormat_ARGB32: {
        Suite<PF_Iterate8Suite2> suite(in,kPFIterate8Suite,kPFIterate8SuiteVersion2);
        return suite->iterate(in,0,dst->height,nullptr,nullptr,&job,pixel8,dst);
    }
    default:return PF_Err_BAD_CALLBACK_PARAM;
    }
}

PF_Err gpu_render(PF_InData* in,PF_SmartRenderExtra* extra,const Prepared& p,
                  PF_EffectWorld* src,PF_EffectWorld* dst,PF_PixelFormat format) {
#if CVL_ENABLE_CUDA
    if(extra->input->what_gpu!=PF_GPU_Framework_CUDA || format!=PF_PixelFormat_GPU_BGRA128)
        return PF_Err_BAD_CALLBACK_PARAM;
    Suite<PF_GPUDeviceSuite1> gpu(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1);
    PF_GPUDeviceInfo info{};
    PF_Err error=gpu->GetDeviceInfo(in->effect_ref,extra->input->device_index,&info);
    void *input=nullptr,*output=nullptr;
    if(!error)error=gpu->GetGPUWorldData(in->effect_ref,src,&input);
    if(!error)error=gpu->GetGPUWorldData(in->effect_ref,dst,&output);
    if(error)return error;
    if(!info.contextPV || !output || (src->width>0 && src->height>0 && !input))return PF_Err_BAD_CALLBACK_PARAM;
    if(cuCtxPushCurrent(static_cast<CUcontext>(info.contextPV))!=CUDA_SUCCESS)return PF_Err_INTERNAL_STRUCT_DAMAGED;
    const cineverse::GPUFrame frame{src->rowbytes,dst->rowbytes,src->width,src->height,dst->width,dst->height,
      src->origin_x,src->origin_y,dst->origin_x,dst->origin_y};
    const int result=cvl_cuda_launch(info.command_queuePV,input,output,frame,&p);
    CUcontext previous=nullptr;
    const CUresult popped=cuCtxPopCurrent(&previous);
    return result==0 && popped==CUDA_SUCCESS ? PF_Err_NONE : PF_Err_INTERNAL_STRUCT_DAMAGED;
#else
    (void)in;(void)extra;(void)p;(void)src;(void)dst;(void)format;
    return PF_Err_BAD_CALLBACK_PARAM;
#endif
}

struct PixelCheckout {
    PF_InData* in; PF_SmartRenderExtra* extra; bool active=false;
    ~PixelCheckout(){if(active)extra->cb->checkin_layer_pixels(in->effect_ref,0);}
    PF_Err close(){if(!active)return PF_Err_NONE;active=false;return extra->cb->checkin_layer_pixels(in->effect_ref,0);}
};
PF_Err smart_render(PF_InData* in,PF_SmartRenderExtra* extra,bool gpu) {
    const auto* p=static_cast<const Prepared*>(extra->input->pre_render_data);
    if(!p)return PF_Err_INTERNAL_STRUCT_DAMAGED;
    PF_EffectWorld *src=nullptr,*dst=nullptr;
    PixelCheckout checkout{in,extra};
    PF_Err error=extra->cb->checkout_layer_pixels(in->effect_ref,0,&src);
    if(error)return error;
    checkout.active=true;
    error=extra->cb->checkout_output(in->effect_ref,&dst);
    if(!error && (!src || !dst))error=PF_Err_BAD_CALLBACK_PARAM;
    if(!error && dst->width>0 && dst->height>0) {
        Suite<PF_WorldSuite2> world(in,kPFWorldSuite,kPFWorldSuiteVersion2);
        PF_PixelFormat format=PF_PixelFormat_INVALID;
        // Empty input still uses the output's format; it is treated as transparent black.
        error=world->PF_GetPixelFormat(dst,&format);
        if(!error) {
            error=gpu?gpu_render(in,extra,*p,src,dst,format):cpu_render(in,*p,src,dst,format);
            if(!error)last_backend.store(gpu?2:1,std::memory_order_relaxed);
        }
    }
    const PF_Err cleanup=checkout.close();
    return error?error:cleanup;
}
void dispose_pre_render(void* data){delete static_cast<Prepared*>(data);}
PF_Err pre_render(PF_InData* in,PF_PreRenderExtra* extra) {
    Settings s;
    PF_Err error=checkout_settings(in,s);
    if(error)return error;
    PF_RenderRequest request=extra->input->output_request;
    request.channel_mask=PF_ChannelMask_ARGB; // grading needs all RGB plus alpha, even for a channel-only output
    request.preserve_rgb_of_zero_alpha=TRUE;
    PF_CheckoutResult result{};
    error=extra->cb->checkout_layer(in->effect_ref,0,0,&request,in->current_time,in->time_step,in->time_scale,&result);
    if(error)return error;
    auto* p=new(std::nothrow)Prepared(prepare_frame(in,s,result.ref_width>0?result.ref_width:in->width,
      result.ref_height>0?result.ref_height:in->height));
    if(!p)return PF_Err_OUT_OF_MEMORY;
    extra->output->pre_render_data=p;
    extra->output->delete_pre_render_data_func=dispose_pre_render;
    extra->output->result_rect=result.result_rect;
    extra->output->max_result_rect=result.max_result_rect;
    extra->output->solid=result.solid;
#if CVL_ENABLE_CUDA
    if(extra->input->what_gpu==PF_GPU_Framework_CUDA && extra->input->bitdepth==32)
        extra->output->flags|=PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
#endif
    return PF_Err_NONE;
}
PF_Err setup_gpu(PF_InData* in,PF_OutData* out,PF_GPUDeviceSetupExtra* extra) {
    extra->output->gpu_data=nullptr;
    out->out_flags2=0; // an unsupported GPU framework uses AE's CPU path
#if CVL_ENABLE_CUDA
    if(extra->input->what_gpu==PF_GPU_Framework_CUDA) {
        Suite<PF_GPUDeviceSuite1> gpu(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1);
        PF_GPUDeviceInfo info{};
        PF_Err error=gpu->GetDeviceInfo(in->effect_ref,extra->input->device_index,&info);
        if(error)return error;
        int driver=0,major=0,minor=0;
        const CUdevice device=static_cast<CUdevice>(reinterpret_cast<std::intptr_t>(info.devicePV));
        if(info.compatibleB && cuDriverGetVersion(&driver)==CUDA_SUCCESS && driver>=12080
          && cuDeviceGetAttribute(&major,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,device)==CUDA_SUCCESS
          && cuDeviceGetAttribute(&minor,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,device)==CUDA_SUCCESS
          && (major>6 || (major==6 && minor>=1)))out->out_flags2=PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    }
#else
    (void)in;
#endif
    return PF_Err_NONE;
}
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,PF_PluginDataCB2 callback,
    SPBasicSuite*,const char*,const char*) {
    PF_Err result=PF_Err_INVALID_CALLBACK;
    PF_REGISTER_EFFECT_EXT2(inPtr,callback,"Cineverse Lite","Cineverse Lite AE 1","Cineverse",AE_RESERVED_INFO,"EffectMain","");
    return result;
}
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd,PF_InData* in,PF_OutData* out,
    PF_ParamDef* params[],PF_LayerDef* output,void* extra) {
    try {
        switch(cmd) {
        case PF_Cmd_ABOUT:
            std::snprintf(out->return_msg,sizeof(out->return_msg),
              "Cineverse Lite AE 0.2 %s Test\rLast completed render: %s\rUse 32 bpc and the documented unmanaged RGB workflow.",
              CVL_ENABLE_CUDA?"CUDA":"CPU",last_backend.load(std::memory_order_relaxed)==2?"CUDA":
              (last_backend.load(std::memory_order_relaxed)==1?"CPU":"not rendered yet"));
            return PF_Err_NONE;
        case PF_Cmd_GLOBAL_SETUP:
            if(in->appl_id!=kAppID_AfterEffects)return PF_Err_BAD_CALLBACK_PARAM;
            out->my_version=CVL_EFFECT_VERSION;out->out_flags=CVL_OUT_FLAGS;out->out_flags2=CVL_OUT_FLAGS2;
            return PF_Err_NONE;
        case PF_Cmd_PARAMS_SETUP:return parameters(in,out);
        case PF_Cmd_QUERY_DYNAMIC_FLAGS: {
            Settings s;
            const PF_Err error=checkout_settings(in,s);
            if(error)return error;
            if(s.input!=0 && s.grain_on && s.grain_amount!=0)out->out_flags|=PF_OutFlag_NON_PARAM_VARY;
            else out->out_flags&=~PF_OutFlag_NON_PARAM_VARY;
            return PF_Err_NONE;
        }
        case PF_Cmd_GPU_DEVICE_SETUP:return setup_gpu(in,out,static_cast<PF_GPUDeviceSetupExtra*>(extra));
        case PF_Cmd_GPU_DEVICE_SETDOWN:return PF_Err_NONE; // no plug-in-owned GPU allocations
        case PF_Cmd_SMART_PRE_RENDER:return pre_render(in,static_cast<PF_PreRenderExtra*>(extra));
        case PF_Cmd_SMART_RENDER:return smart_render(in,static_cast<PF_SmartRenderExtra*>(extra),false);
        case PF_Cmd_SMART_RENDER_GPU:return smart_render(in,static_cast<PF_SmartRenderExtra*>(extra),true);
        case PF_Cmd_RENDER: {
            const auto p=prepare_frame(in,read_values(params),in->width,in->height);
            last_backend.store(1,std::memory_order_relaxed);
            return cpu_render(in,p,&params[0]->u.ld,output,PF_WORLD_IS_DEEP(output)?PF_PixelFormat_ARGB64:PF_PixelFormat_ARGB32);
        }
        default:return PF_Err_NONE;
        }
    } catch(PF_Err error){return error;}
      catch(const std::bad_alloc&){return PF_Err_OUT_OF_MEMORY;}
      catch(...){return PF_Err_INTERNAL_STRUCT_DAMAGED;}
}
