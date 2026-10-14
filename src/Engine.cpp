#include "Core.generated.hpp"
#include <algorithm>
namespace cineverse {
Prepared prepare(const Settings& settings,int width,int height,int frame,float scale_x,float scale_y) {
    Prepared p;
    p.s=settings; p.full_width=std::max(1,width); p.full_height=std::max(1,height); p.frame=frame;
    p.scale_x=scale_x>0?scale_x:1; p.scale_y=scale_y>0?scale_y:1;
    p.exposure_gain=_exp2f(settings.ev);
    const bool neutral=settings.ev==0 && settings.contrast==0 && settings.temp==0 && settings.tint==0
      && settings.sat==1 && settings.fsat==1 && (!settings.neg || settings.neg_strength==0)
      && (!settings.print || settings.print_strength==0) && (!settings.grain_on || settings.grain_amount==0);
    p.passthrough=settings.input==0 || (settings.input==1 && settings.output==0 && neutral);
    p.neutral=neutral?1:0;
    p.input_profile=settings.input-1;
    if(settings.input!=0) {
        p.input_gamut=cv_profile_gamut(p.input_profile);
        p.input_gamma=cv_profile_gamma(p.input_profile);
        p.capture_id=cv_capture_id(p.input_profile);
    }
    if(settings.temp!=0 || settings.tint!=0) {
        float t=1.0e6f/(1.0e6f/6504.0f+settings.temp);
        float r=1.0f/t;
        float x=cv_daylight_x(t),x0=cv_daylight_x(6504.0f);
        float y=-3.0f*x*x+2.87f*x-0.275f;
        float y0=-3.0f*x0*x0+2.87f*x0-0.275f;
        if(settings.tint==0) {
            x+=0.3127f-x0; y+=0.3290f-y0;
            p.wb_target=cv_white_lms(makeVec3(x/y,1.0f,(1.0f-x-y)/y));
        } else {
            float dx=t<=7000.0f ? -13.821e9f*r*r+5.9356e6f*r+99.11f : -6.0192e9f*r*r+3.8036e6f*r+247.48f;
            float dy=(-6.0f*x+2.87f)*dx;
            x+=0.3127f-x0; y+=0.3290f-y0;
            float d=-2.0f*x+12.0f*y+3.0f,dd=-2.0f*dx+12.0f*dy;
            float du=4.0f*(dx*d-x*dd),dv=6.0f*(dy*d-y*dd);
            float offset=0.0002f*settings.tint/_sqrtf(du*du+dv*dv);
            float u=4.0f*x/d+offset*dv,vc=6.0f*y/d-offset*du;
            p.wb_target=cv_white_lms(makeVec3(1.5f*u/vc,1.0f,(4.0f-u-10.0f*vc)/(2.0f*vc)));
        }
        p.wb_base=cv_white_lms(makeVec3(0.3127f/0.3290f,1.0f,0.3583f/0.3290f));
    }
    return p;
}
Vec3 process_rgb(Vec3 rgb,const Prepared& p,float x,float y) {
    if(p.passthrough)return rgb;
    return transform(p,x,y,rgb.x,rgb.y,rgb.z);
}
Vec3 process_associated(Vec3 rgb,float alpha,const Prepared& p,float x,float y) {
    // Preserve hidden RGB and exact identity. Do not create opacity or grain on empty pixels.
    if(p.passthrough || !(alpha>0.0f))return rgb;
    Vec3 straight{rgb.x/alpha,rgb.y/alpha,rgb.z/alpha};
    Vec3 out=process_rgb(straight,p,x,y);
    return {out.x*alpha,out.y*alpha,out.z*alpha};
}
}
