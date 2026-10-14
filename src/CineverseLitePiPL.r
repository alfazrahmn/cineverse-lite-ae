#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "PluginFlags.h"
resource 'PiPL' (16000) {
 {
  Kind { AEEffect },
  Name { "Cineverse Lite" },
  Category { "Cineverse" },
  CodeWin64X86 { "EffectMain" },
  AE_PiPL_Version { 2, 0 },
  AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
  AE_Effect_Version { CVL_EFFECT_VERSION },
  AE_Effect_Info_Flags { 0 },
  AE_Effect_Global_OutFlags { CVL_OUT_FLAGS },
  AE_Effect_Global_OutFlags_2 { CVL_OUT_FLAGS2 },
  AE_Effect_Match_Name { "Cineverse Lite AE 1" },
  AE_Reserved_Info { 0 },
  AE_Effect_Support_URL { "" }
 }
};
