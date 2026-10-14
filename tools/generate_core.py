"""Deterministic mechanical port of the approved Lite source; never edits its tables."""
from pathlib import Path
import hashlib, json, re

ROOT = Path(__file__).resolve().parents[1]
source_path = ROOT / 'reference/Cineverse_Lite_v1.dctl'
source = source_path.read_text()
ui = []
for line in source.splitlines():
    if not line.startswith('DEFINE_UI_PARAMS('):
        continue
    name, label, kind, default = re.match(r'DEFINE_UI_PARAMS\((\w+),\s*([^,]+),\s*(\w+),\s*([^,)]+)', line).groups()
    record = dict(source_id=name, field=name[4:], label=label, kind=kind, default=default)
    if kind == 'DCTLUI_COMBO_BOX':
        record['choices'] = [v.strip() for v in re.findall(r'\{([^}]+)\}', line)[1].split(',')]
    elif kind == 'DCTLUI_SLIDER_FLOAT':
        values = line.rstrip(')').split(',')
        record.update(minimum=values[-3].strip(), maximum=values[-2].strip(), step=values[-1].strip())
    ui.append(record)
assert len(ui) == 15

core = '\n'.join(l for l in source.splitlines() if not l.startswith('DEFINE_UI_PARAMS('))
core = re.sub(r'\bfloat3\b', 'Vec3', core)
core = re.sub(r'\bfloat4\b', 'Vec4', core)
core = core.replace('make_float3', 'makeVec3').replace('make_float4', 'makeVec4')
core = core.replace('int width,int x,int y,int frame,float amount', 'int width,float x,float y,int frame,float amount')
signature = '__DEVICE__ Vec3 transform(int p_Width, int p_Height, int p_X, int p_Y, float p_R, float p_G, float p_B)'
assert signature in core
locals_ = '\n'.join(('float' if u['kind'] == 'DCTLUI_SLIDER_FLOAT' else 'int') + ' ' + u['source_id'] + '=p.s.' + u['field'] + ';' for u in ui)
locals_ += '\nint p_Width=p.full_width, TIMELINE_FRAME_INDEX=p.frame;\nfloat p_X=x,p_Y=y;\n'
core = core.replace(signature + '\n{', '__DEVICE__ Vec3 transform(const Prepared& p, float x, float y, float p_R, float p_G, float p_B)\n{\n' + locals_)
core = core.replace('float gain=_exp2f(cvl_ev);', 'float gain=p.exposure_gain;')
old_wb = 'code=cv_uploaded_to_code(cv_temperature_tint(cv_uploaded_from_code(code),cvl_temp,cvl_tint));'
new_wb = '''{
   Vec3 lms=cv_to_lms(cv_uploaded_from_code(code));
   code=cv_uploaded_to_code(cv_from_lms(makeVec3(lms.x*p.wb_target.x/p.wb_base.x,
     lms.y*p.wb_target.y/p.wb_base.y,lms.z*p.wb_target.z/p.wb_base.z)));
  }'''
assert old_wb in core
core = core.replace(old_wb, new_wb)
core = core.replace('int input=cvl_input-1,ig=cv_profile_gamut(input),it=cv_profile_gamma(input);',
                    'int input=p.input_profile,ig=p.input_gamut,it=p.input_gamma;')
core = re.sub(r' int neutral=cvl_ev==0\.0f.*?;', ' int neutral=p.neutral;', core, flags=re.S)
core = core.replace('cv_capture_id(input)', 'p.capture_id')
core = '#pragma once\n#include "Compat.hpp"\n#include "Engine.hpp"\nnamespace cineverse {\n' + core + '\n}\n'
(ROOT/'src').mkdir(exist_ok=True)
(ROOT/'src/Core.generated.hpp').write_text(core)

fields = '\n'.join('    ' + ('float' if u['kind']=='DCTLUI_SLIDER_FLOAT' else 'int') + ' ' + u['field'] + ' = ' + u['default'] + ';' for u in ui)
(ROOT/'src/Settings.generated.hpp').write_text('#pragma once\nnamespace cineverse {\nstruct Settings {\n' + fields + '\n};\n}\n')
params=[]
for index,u in enumerate(ui,1):
    field=u['field']
    if u['kind']=='DCTLUI_COMBO_BOX':
        text='|'.join(u['choices'])
        params.append(f'    AEFX_CLR_STRUCT(def); PF_ADD_POPUP("{u["label"]}", {len(u["choices"])}, {int(u["default"])+1}, "{text}", {index});')
    elif u['kind']=='DCTLUI_CHECK_BOX':
        params.append(f'    PF_ADD_CHECKBOXX("{u["label"]}", {u["default"]}, 0, {index});')
    else:
        precision='PF_Precision_TEN_THOUSANDTHS' if float(u['step'].rstrip('f'))<.001 else 'PF_Precision_THOUSANDTHS'
        params.append(f'    PF_ADD_FLOAT_SLIDERX("{u["label"]}", {u["minimum"]}, {u["maximum"]}, {u["minimum"]}, {u["maximum"]}, {u["default"]}, {precision}, 0, 0, {index});')
(ROOT/'src/Params.generated.inc').write_text('\n'.join(params)+'\n')
reads=[]
for index,u in enumerate(ui,1):
    if u['kind']=='DCTLUI_SLIDER_FLOAT':value='static_cast<float>(params['+str(index)+']->u.fs_d.value)'
    elif u['kind']=='DCTLUI_COMBO_BOX':value='params['+str(index)+']->u.pd.value-1'
    else:value='params['+str(index)+']->u.bd.value ? 1 : 0'
    reads.append('    s.'+u['field']+'='+value+';')
(ROOT/'src/ReadParams.generated.inc').write_text('\n'.join(reads)+'\n')
(ROOT/'CONTROL_SPEC.json').write_text(json.dumps(ui,indent=2)+'\n')
manifest={'source_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),
          'source_version':'Cineverse Lite v1.1 Compact',
          'stock_math':'Retained compact negative approximation and original 65-knot print curves',
          'changes':['Native parameter block replaces DCTL UI globals', 'Per-frame exposure gain and white-balance whites', 'Floating-point grain coordinates for preview scaling', 'Profile dispatch and neutral-state decisions prepared once per frame'],
          'constant_bytes':sum(int(n)*4 for n in re.findall(r'__CONSTANT__\s+(?:int|float)\s+\w+\[(\d+)\]',source))}
(ROOT/'PORT_MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest))
