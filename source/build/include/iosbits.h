// iOS/iPadOS platform glue for NotBlood

#ifndef iosbits_h_
#define iosbits_h_

#include "compat.h"

#ifdef EDUKE32_IOS

#include "sdl_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Real program entry is in iosbits.mm; it hands control to SDL's UIKit
// runloop, which then calls this (the engine's former main()).
int eduke32_ios_main(int argc, char *argv[]);

// Returns a heap-allocated (Xstrdup) path; caller frees.
char *ios_getappdir(void);       // read-only app bundle resources
char *ios_getdocumentsdir(void); // visible in the Files app

// chdir() to the Documents folder (shown in Files) and drop the readme there.
void ios_setupdocuments(void);

// Nags (with a recheck loop) until BLOOD.RFF shows up in the Documents folder.
void ios_checkgamedata(void);

// Crash guard for the GL renderer: returns true if OpenGL should not be used this run
// (DISABLE_OPENGL.txt exists, or the previous run died before ios_glstartupok()).
bool ios_glsafemode(void);
void ios_glstartupok(void);

// Sets up gl4es on the current SDL OpenGL ES context (0 on success).
int ios_initgl4es(SDL_Window *window);
// GetProcAddress for the engine's GL loader (returns gl4es' desktop GL entry points).
void *ios_glGetProcAddress(const char *name);

// Size the game should render at: the GL drawable once it exists, else the native panel size.
// (implemented in sdlayer.cpp)
void ios_getrendersize(int32_t *w, int32_t *h);

// Landscape screen size: physical pixels and UIKit points.
void ios_getscreensize(int32_t *pixelw, int32_t *pixelh, int32_t *pointw, int32_t *pointh);

// Render resolution as a percentage of the panel's: the GL view renders that many pixels and
// the iPad scales them up to the screen for free (fewer pixels: cooler, longer battery).
// ios_scaledsize() is the drawable size the view gets for a percentage (same math as UIKit/SDL);
// ios_setrenderscale() switches the view to it.
void ios_scaledsize(int pct, int32_t *w, int32_t *h);
void ios_setrenderscale(SDL_Window *window, int pct);
// the percentages the video mode list offers, and the one a listed size stands for
#define kIosRenderScales 10
extern int const ios_renderscales[kIosRenderScales];
int ios_scaleforsize(int32_t w);

#ifdef NOTBLOOD_ANDROID
// A crash appends the signal and a backtrace to this file (the log; see androidbits.cpp).
void android_installcrashhandler(char const *pzLogFile);
// gl4es can run Polymost's palette-indexed textures on this GPU (after ios_initgl4es()).
bool android_glindexedcolors(void);
#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// Hooks implemented by the game-side touch layer (source/blood/src/touchcontrols.cpp).
bool touch_handleEvent(SDL_Event const *ev);                    // true if the event was consumed
void touch_render(SDL_Renderer *renderer, SDL_Rect const *gameRect); // draw overlay after the game frame (Metal fallback)
void touch_renderGL(int outw, int outh);                         // same, on the GL path (before swap)
void touch_updateGameFunctions(int32_t *flags, int32_t numFlags); // OR in on-screen button states
void touch_notifyHardwareInput(void);                            // keyboard/mouse activity seen
void touch_notifyController(bool connected);                     // a game controller is (dis)connected
void touch_releaseAll(void);                                     // drop all fingers (app was suspended)
#endif

#endif // EDUKE32_IOS

#endif // iosbits_h_
