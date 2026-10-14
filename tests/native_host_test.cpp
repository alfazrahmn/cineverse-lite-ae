// Host-callback simulation against Adobe's actual headers. This is not an AE host run.
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include "../src/CineverseLite.cpp"
static_assert(CVL_OUT_FLAGS==(PF_OutFlag_DEEP_COLOR_AWARE|PF_OutFlag_PIX_INDEPENDENT|PF_OutFlag_NON_PARAM_VARY),"PiPL/runtime flags mismatch");
static_assert(CVL_OUT_FLAGS2==(PF_OutFlag2_FLOAT_COLOR_AWARE|PF_OutFlag2_SUPPORTS_SMART_RENDER|
    PF_OutFlag2_SUPPORTS_THREADED_RENDERING|PF_OutFlag2_SUPPORTS_QUERY_DYNAMIC_FLAGS),"CPU PiPL flags mismatch");
namespace {
int count=0;
void verify(const char* name,bool ok){++count;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
struct Host {
    std::vector<PF_ParamDef> parameters{PF_ParamDef{}};
    PF_InData in{};PF_OutData out{};SPBasicSuite basic{};
    PF_EffectWorld src{},dst{};
    PF_PixelFormat format=PF_PixelFormat_ARGB128;
    int parameter_checkouts=0,parameter_checkins=0,pixel_checkins=0;
    int fail_parameter=0,fail_output=0,fail_iteration=0;
    int suites_acquired=0,suites_released=0;
    PF_RenderRequest requested{};
};
Host* current=nullptr;
PF_Err add_param(PF_ProgPtr ptr,PF_ParamIndex,PF_ParamDef* def){reinterpret_cast<Host*>(ptr)->parameters.push_back(*def);return PF_Err_NONE;}
PF_Err checkout_param(PF_ProgPtr ptr,PF_ParamIndex index,A_long,A_long,A_u_long,PF_ParamDef* out){
    auto& h=*reinterpret_cast<Host*>(ptr);if(index==h.fail_parameter)return PF_Err_BAD_CALLBACK_PARAM;
    *out=h.parameters.at(index);++h.parameter_checkouts;return PF_Err_NONE;
}
PF_Err checkin_param(PF_ProgPtr ptr,PF_ParamDef*){++reinterpret_cast<Host*>(ptr)->parameter_checkins;return PF_Err_NONE;}
PF_Err get_format(const PF_EffectWorld*,PF_PixelFormat* format){*format=current->format;return PF_Err_NONE;}
template<class Pixel> PF_Err iterate(PF_InData*,A_long,A_long,PF_EffectWorld* source,const PF_Rect*,void* data,
    PF_Err (*callback)(void*,A_long,A_long,Pixel*,Pixel*),PF_EffectWorld* dst) {
    if(source)return PF_Err_BAD_CALLBACK_PARAM;
    if(current->fail_iteration)return PF_Err_BAD_CALLBACK_PARAM;
    for(int y=0;y<dst->height;++y)for(int x=0;x<dst->width;++x){
        auto* row=reinterpret_cast<Pixel*>(reinterpret_cast<char*>(dst->data)+y*dst->rowbytes);
        const auto err=callback(data,x,y,nullptr,&row[x]);if(err)return err;
    }
    return PF_Err_NONE;
}
PF_WorldSuite2 world{};PF_Iterate8Suite2 it8{};PF_iterate16Suite2 it16{};PF_iterateFloatSuite2 it32{};
SPErr acquire(const char* name,int32,const void** suite){
    if(std::strcmp(name,kPFWorldSuite)==0)*suite=&world;
    else if(std::strcmp(name,kPFIterate8Suite)==0)*suite=&it8;
    else if(std::strcmp(name,kPFIterate16Suite)==0)*suite=&it16;
    else if(std::strcmp(name,kPFIterateFloatSuite)==0)*suite=&it32;
    else return kSPBadParameterError;
    ++current->suites_acquired;return kSPNoError;
}
SPErr release(const char*,int32){++current->suites_released;return kSPNoError;}
PF_Err checkout_layer(PF_ProgPtr ptr,PF_ParamIndex,A_long,const PF_RenderRequest* request,A_long,A_long,A_u_long,PF_CheckoutResult* result){
    auto& h=*reinterpret_cast<Host*>(ptr);h.requested=*request;
    result->result_rect={h.src.origin_x,h.src.origin_y,h.src.origin_x+h.src.width,h.src.origin_y+h.src.height};
    result->max_result_rect=result->result_rect;result->ref_width=3840;result->ref_height=2160;return PF_Err_NONE;
}
PF_Err checkout_pixels(PF_ProgPtr ptr,A_long,PF_EffectWorld** world_){*world_=&reinterpret_cast<Host*>(ptr)->src;return PF_Err_NONE;}
PF_Err checkin_pixels(PF_ProgPtr ptr,A_long){++reinterpret_cast<Host*>(ptr)->pixel_checkins;return PF_Err_NONE;}
PF_Err checkout_output(PF_ProgPtr ptr,PF_EffectWorld** world_){auto& h=*reinterpret_cast<Host*>(ptr);if(h.fail_output)return PF_Err_BAD_CALLBACK_PARAM;*world_=&h.dst;return PF_Err_NONE;}
bool same(float a,float b){return std::fabs(a-b)<3e-6f;}
template<class Pixel> void world_buffer(PF_EffectWorld& w,std::vector<Pixel>& pixels,int width,int height,int padding,int ox,int oy){
    pixels.resize((width+padding)*height);w.data=reinterpret_cast<PF_PixelPtr>(pixels.data());
    w.width=width;w.height=height;w.rowbytes=(width+padding)*sizeof(Pixel);w.origin_x=ox;w.origin_y=oy;
}
PF_Err run_smart(Host& h){
    PF_PreRenderInput pi{};pi.bitdepth=32;PF_PreRenderOutput po{};
    PF_PreRenderCallbacks pcb{};pcb.checkout_layer=checkout_layer;PF_PreRenderExtra pre{&pi,&po,&pcb};
    auto e=EffectMain(PF_Cmd_SMART_PRE_RENDER,&h.in,&h.out,nullptr,nullptr,&pre);
    if(e)return e;
    PF_SmartRenderInput si{};si.pre_render_data=po.pre_render_data;si.bitdepth=32;
    PF_SmartRenderCallbacks scb{};scb.checkout_layer_pixels=checkout_pixels;scb.checkout_output=checkout_output;scb.checkin_layer_pixels=checkin_pixels;
    PF_SmartRenderExtra smart{&si,&scb};
    e=EffectMain(PF_Cmd_SMART_RENDER,&h.in,&h.out,nullptr,nullptr,&smart);
    po.delete_pre_render_data_func(po.pre_render_data);
    return e;
}
}
int main(){try{
    Host h;current=&h;
    world.PF_GetPixelFormat=get_format;it8.iterate=iterate<PF_Pixel8>;it16.iterate=iterate<PF_Pixel16>;it32.iterate=iterate<PF_PixelFloat>;
    h.basic.AcquireSuite=acquire;h.basic.ReleaseSuite=release;
    h.in.effect_ref=reinterpret_cast<PF_ProgPtr>(&h);h.in.pica_basicP=&h.basic;h.in.appl_id=kAppID_AfterEffects;
    h.in.width=3840;h.in.height=2160;h.in.time_step=1000;h.in.local_time_step=1000;h.in.current_time=17000;
    h.in.downsample_x={1,1};h.in.downsample_y={1,1};
    h.in.inter.add_param=add_param;h.in.inter.checkout_param=checkout_param;h.in.inter.checkin_param=checkin_param;
    verify("global_setup",EffectMain(PF_Cmd_GLOBAL_SETUP,&h.in,&h.out,nullptr,nullptr,nullptr)==0);
    verify("resource_flags_match_runtime",h.out.out_flags==CVL_OUT_FLAGS && h.out.out_flags2==CVL_OUT_FLAGS2);
    verify("params_setup",EffectMain(PF_Cmd_PARAMS_SETUP,&h.in,&h.out,nullptr,nullptr,nullptr)==0);
    verify("exactly_15_controls",h.out.num_params==16 && h.parameters.size()==16);
    verify("profiles_first",std::string(h.parameters[1].PF_DEF_NAME)=="Input Profile" && std::string(h.parameters[2].PF_DEF_NAME)=="Output Profile");
    verify("negative_print_default_half",h.parameters[11].u.fs_d.value==.5 && h.parameters[13].u.fs_d.value==.5);
    verify("default_input_unselected",h.parameters[1].u.pd.value==1);
    verify("default_grain_off",h.parameters[14].u.bd.value==0);
    h.out.out_flags=CVL_OUT_FLAGS;
    verify("dynamic_flags_query",EffectMain(PF_Cmd_QUERY_DYNAMIC_FLAGS,&h.in,&h.out,nullptr,nullptr,nullptr)==0);
    verify("grain_off_allows_static_cache",!(h.out.out_flags&PF_OutFlag_NON_PARAM_VARY));
    std::vector<PF_PixelFloat> input,output;
    world_buffer(h.src,input,3,2,2,10,20);world_buffer(h.dst,output,2,2,3,11,20);
    input[1]={1,.2f,.3f,.4f};input[2]={.5f,.1f,.2f,.3f};input[6]={0,.4f,-.1f,2.f};input[7]={1,-.1f,.2f,2.f};
    verify("smart_render_identity",run_smart(h)==0);
    verify("roi_origin_and_source_pitch",std::memcmp(&output[0],&input[1],sizeof(PF_PixelFloat))==0 && std::memcmp(&output[5],&input[6],sizeof(PF_PixelFloat))==0);
    verify("output_row_padding_untouched",output[2].alpha==0 && output[7].alpha==0);
    verify("all_channels_checked_out",h.requested.channel_mask==PF_ChannelMask_ARGB && h.requested.preserve_rgb_of_zero_alpha);
    verify("parameter_checkouts_balanced",h.parameter_checkouts==h.parameter_checkins);
    verify("suite_checkouts_balanced",h.suites_acquired==h.suites_released);
    h.parameters[1].u.pd.value=2;h.parameters[3].u.fs_d.value=.7;
    verify("smart_render_graded",run_smart(h)==0);
    const auto p=cineverse::prepare(Settings{2-1,0,.7f},3840,2160,17);
    const auto expected=cineverse::process_associated({input[1].red,input[1].green,input[1].blue},input[1].alpha,p,11,20);
    verify("graded_pixel_matches_engine",same(output[0].red,expected.x)&&same(output[0].green,expected.y)&&same(output[0].blue,expected.z));
    verify("alpha_unchanged",output[1].alpha==.5f);
    verify("hidden_rgb_unchanged",output[5].red==.4f && output[5].green==-.1f && output[5].blue==2.f && output[5].alpha==0);
    h.parameters[14].u.bd.value=1;
    verify("dynamic_grain_query",EffectMain(PF_Cmd_QUERY_DYNAMIC_FLAGS,&h.in,&h.out,nullptr,nullptr,nullptr)==0);
    verify("grain_enables_time_dependence",h.out.out_flags&PF_OutFlag_NON_PARAM_VARY);
    h.parameters[14].u.bd.value=0;
    h.fail_parameter=5;const auto balance=h.parameter_checkouts-h.parameter_checkins;
    verify("checkout_error_propagates",run_smart(h)==PF_Err_BAD_CALLBACK_PARAM);
    verify("partial_checkout_cleanup",h.parameter_checkouts-h.parameter_checkins==balance);h.fail_parameter=0;
    h.fail_output=1;const auto checked=h.pixel_checkins;
    verify("output_error_propagates",run_smart(h)==PF_Err_BAD_CALLBACK_PARAM);
    verify("output_error_releases_input",h.pixel_checkins==checked+1);h.fail_output=0;
    h.fail_iteration=1;
    verify("iterator_error_propagates",run_smart(h)==PF_Err_BAD_CALLBACK_PARAM);
    verify("error_suite_cleanup",h.suites_acquired==h.suites_released);h.fail_iteration=0;
    std::vector<PF_Pixel8> in8,out8;
    world_buffer(h.src,in8,2,1,1,0,0);world_buffer(h.dst,out8,2,1,2,0,0);h.format=PF_PixelFormat_ARGB32;
    h.parameters[1].u.pd.value=1;in8[0]={127,20,60,90};in8[1]={255,255,0,127};
    verify("8_bit_render",run_smart(h)==0);
    verify("8_bit_identity_exact",std::memcmp(in8.data(),out8.data(),2*sizeof(PF_Pixel8))==0);
    std::vector<PF_Pixel16> in16,out16;
    world_buffer(h.src,in16,2,1,1,0,0);world_buffer(h.dst,out16,2,1,2,0,0);h.format=PF_PixelFormat_ARGB64;
    in16[0]={16384,2000,6000,9000};in16[1]={32768,32768,0,16000};
    verify("16_bit_render",run_smart(h)==0);
    verify("16_bit_identity_exact",std::memcmp(in16.data(),out16.data(),2*sizeof(PF_Pixel16))==0);
    h.parameters[1].u.pd.value=2;h.parameters[3].u.fs_d.value=2;
    verify("16_bit_graded_render",run_smart(h)==0);
    verify("16_bit_alpha_and_32768_channel_max",out16[0].alpha==16384 && out16[1].alpha==32768 && out16[1].red<=32768);
    verify("final_parameter_cleanup",h.parameter_checkouts==h.parameter_checkins);
    verify("final_suite_cleanup",h.suites_acquired==h.suites_released);
    std::cout<<"PASSED "<<count<<" simulated-host checks; no native AE or GPU execution.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
