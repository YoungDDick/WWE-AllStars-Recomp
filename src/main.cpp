#include <rex/cvar.h>

#include "generated/default/wweallstars_init.h"

#include "wweallstars_app.h"

REXCVAR_DEFINE_BOOL(wwe_profile, false, "WWEAllStars",
                    "Start the on-demand Tracy profiler (connect with .tools/tracy-0.13.1)");

REX_DEFINE_APP(wweallstars, WweAllStarsApp::Create)
