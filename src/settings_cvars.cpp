// The port's own player-facing settings, defined in one place so the in-game
// settings page and the tests see them without linking the renderer. Each
// declares when a change takes effect (in-game settings ticket 01).

#include <rex/cvar.h>

// Native is the default; false runs the Emulated renderer (ADR-0020).
REXCVAR_DEFINE_BOOL(wwe_native_renderer, true, "WWEAllStars",
                    "Draw with the native Vulkan renderer instead of the Emulated renderer "
                    "(Xenos GPU emulation)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(wwe_native_supersample, true, "WWEAllStars",
                    "Enhancement: 2x2 supersampling of screen-sized render targets, filtered "
                    "down to 1280x720 (not combined with wwe_native_resolution_scale)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// Read when Native starts: the mode picks texture views, a shader spec bit
// and the pipelines the prebuild plan builds (native/texture_formats.h).
REXCVAR_DEFINE_INT32(wwe_native_color_mode, 0, "WWEAllStars",
                     "Native's colour curve for Xenos gamma: 0 = Original (the Xbox 360's "
                     "piecewise-linear curve, as Xenia), 1 = Natural (sRGB; an Enhancement)")
    .range(0, 1)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// Handed to the presenter at startup and, through a change callback, on every
// change after Native starts (native_renderer.cpp, ApplyLiveSharpening).
REXCVAR_DEFINE_DOUBLE(wwe_native_cas, 0.5, "WWEAllStars",
                      "Enhancement: CAS sharpening strength in [0, 1] applied to Native's "
                      "output through the presenter; 0 = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(wwe_native_resolution_scale, 1, "WWEAllStars",
                     "Native renderer: render at this multiple of the game's resolution (1-3), "
                     "then let the presenter fit the frame to the window")
    .range(1, 3)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
// Read at each frame's start; part of the sampler cache key, so a change gets
// new samplers from the next frame (native/sampler_choice.h).
REXCVAR_DEFINE_INT32(wwe_native_anisotropy, 3, "WWEAllStars",
                     "Native renderer: anisotropic filtering for linear, mipmapped textures, as "
                     "anisotropic_override (-1 = the game's setting, 0 = off, 1..5 = 1x..16x)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Asked at every engine tick's step choice (game_tick_log.cpp), so a change applies at once.
REXCVAR_DEFINE_BOOL(wwe_60fps_entrances, true, "WWEAllStars",
                    "Run entrances and the arena intro at 60 fps instead of the original 30 "
                    "(an Enhancement)");
// Asked on every pad request and every hit, so a change applies at once.
REXCVAR_DEFINE_INT32(wwe_vibration_mode, 2, "WWEAllStars",
                     "Pad vibration: 0 = Off, 1 = Original (the Xbox 360's aerial hit pulse "
                     "only), 2 = Enhanced (a pulse on every hit plus big moments; an Enhancement)")
    .range(0, 2);
// Asked on every pad request, so a change applies at once.
REXCVAR_DEFINE_INT32(wwe_vibration_strength, 100, "WWEAllStars",
                     "Pad vibration strength in percent (0-100), applied to every rumble in "
                     "every mode with a perceptual curve (strength squared)")
    .range(0, 100);
