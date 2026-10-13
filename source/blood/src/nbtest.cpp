//-------------------------------------------------------------------------
/*
This file is part of NotBlood (iOS port).

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.
*/
//-------------------------------------------------------------------------

// Desktop test harness for the iOS OpenGL path (platform/iOS/test-harness).
//
// Built only with NOTBLOOD_GL4ES_TEST on Linux: the engine renders through gl4es on an
// OpenGL ES context that is made to look like the iPad's (Apple's extension list,
// limited NPOT, 4096 max texture size), and a small script drives the game and
// captures screenshots, so rendering problems can be reproduced without a device.
//
//   NBTEST_SCRIPT="120:shot:menu;150:key:esc;200:renderer:classic;260:shot:classic;300:quit"
//
// Actions: shot:<name>  key:<esc|enter|up|down|left|right|space>  renderer:<classic|polymost>
//          look:<up|down|center>  osd:<console command>  coopcontinue:<slot>  players
//          menu:<netstart|coopload|ingame|close>  flash:<visibility>  quit

#include "compat.h"

#ifdef NOTBLOOD_GL4ES_TEST

#include "build.h"
#include "baselayer.h"
#include "glbuild.h"
#include "keyboard.h"
#include "sdl_inc.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "osdcmds.h"
#include "globals.h"
#include "player.h"
#include "view.h"
#include "coopsave.h"
#include "mmulti.h"
#include "network.h"
#include "osd.h"
#include "gamemenu.h"
#include "menuchosen.h"
#include "levels.h"
#include "endgame.h"

#include <dlfcn.h>
#include <vector>
#include <string>

extern "C" {
void set_getprocaddress(void *(*new_proc_address)(const char *));
void set_getmainfbsize(void (*new_getMainFBSize)(int *width, int *height));
void initialize_gl4es(void);
void *gl4es_GetProcAddress(const char *name);
}

void onvideomodechange(int32_t newmode);

//
// GLES driver that reports what the iPad reported (see stdout.txt from the device)
//

static SDL_Window *s_window;

typedef unsigned char const *(*glGetString_t)(unsigned);
typedef void (*glGetIntegerv_t)(unsigned, int *);

static glGetString_t s_realGetString;
static glGetIntegerv_t s_realGetIntegerv;

static char const kAppleExtensions[] =
    "GL_EXT_blend_minmax GL_OES_mapbuffer GL_OES_element_index_uint GL_OES_packed_depth_stencil "
    "GL_OES_depth24 GL_OES_rgb8_rgba8 GL_OES_depth_texture GL_EXT_texture_rg GL_OES_texture_float "
    "GL_OES_texture_half_float GL_EXT_color_buffer_half_float GL_EXT_shader_texture_lod "
    "GL_OES_standard_derivatives GL_EXT_texture_filter_anisotropic GL_APPLE_texture_2D_limited_npot "
    "GL_OES_vertex_array_object GL_EXT_debug_label GL_EXT_debug_marker GL_EXT_discard_framebuffer ";

static unsigned char const *fakeGetString(unsigned name)
{
    if (Bgetenv("NBTEST_NOLIMITS"))
        return s_realGetString(name);
    if (name == 0x1F03 /* GL_EXTENSIONS */)
    {
        // NBTEST_NPOT=1: the iPad's list plus full NPOT support (as Android GPUs have)
        static std::string const withNpot = std::string(kAppleExtensions) + "GL_OES_texture_npot ";
        return (unsigned char const *)(Bgetenv("NBTEST_NPOT") ? withNpot.c_str() : kAppleExtensions);
    }
    if (name == 0x1F00 /* GL_VENDOR */)
        return (unsigned char const *)"Apple Inc. (emulated)";
    if (name == 0x1F02 /* GL_VERSION */)
        return (unsigned char const *)"OpenGL ES 2.0 Apple (emulated)";
    return s_realGetString(name);
}

static void fakeGetIntegerv(unsigned pname, int *data)
{
    s_realGetIntegerv(pname, data);

    if (Bgetenv("NBTEST_NOLIMITS")) // the driver's own limits (an Android GPU rather than the iPad)
        return;

    switch (pname)
    {
        case 0x0D33: /* GL_MAX_TEXTURE_SIZE */ *data = min(*data, 4096); break;
        case 0x8DFC: /* GL_MAX_VARYING_VECTORS */ *data = min(*data, 8); break;
        case 0x8872: /* GL_MAX_TEXTURE_IMAGE_UNITS */ *data = min(*data, 8); break;
        case 0x8869: /* GL_MAX_VERTEX_ATTRIBS */ *data = min(*data, 16); break;
    }
}

// per-frame counts of the GLES calls gl4es makes, to see what the real driver has to do
#define NBTEST_COUNTED(X) X(glBufferSubData) X(glBufferData) X(glTexImage2D) X(glTexSubImage2D) X(glDrawArrays) \
    X(glDrawElements) X(glUseProgram) X(glBindTexture) X(glGetError) X(glFlush) X(glFinish) X(glReadPixels) \
    X(glCompileShader) X(glLinkProgram) X(glGenerateMipmap) X(glBindFramebuffer) X(glVertexAttribPointer)

#define NBTEST_DECL(f) static void *s_real_##f; static int s_cnt_##f;
NBTEST_COUNTED(NBTEST_DECL)

#if defined __x86_64__
// forward any arguments untouched: count, then tail-jump to the real function
#define NBTEST_WRAP(f) extern "C" void nbtest_wrap_##f(void); \
    extern "C" void *nbtest_real_##f(void) { s_cnt_##f++; return s_real_##f; } \
    __asm__(".text\n.globl nbtest_wrap_" #f "\nnbtest_wrap_" #f ":\n" \
            "push %rdi\npush %rsi\npush %rdx\npush %rcx\npush %r8\npush %r9\nsub $8,%rsp\n" \
            "call nbtest_real_" #f "\nadd $8,%rsp\npop %r9\npop %r8\npop %rcx\npop %rdx\npop %rsi\npop %rdi\n" \
            "jmp *%rax\n");
NBTEST_COUNTED(NBTEST_WRAP)
#endif

static void nbtest_logcounts(int frames)
{
    char buf[1024];
    int len = 0;
#define NBTEST_LOG(f) if (s_cnt_##f) len += Bsnprintf(buf + len, sizeof(buf) - len, " %s=%d", #f + 2, s_cnt_##f / frames); s_cnt_##f = 0;
    NBTEST_COUNTED(NBTEST_LOG)
    LOG_F(INFO, "nbtest: GLES calls per frame:%s", buf);
}

// NBTEST_FP16FOG=1: fog values gl4es declares mediump behave like half floats (as on phone GPUs:
// beyond 65504 they become infinite, tiny ones zero), which Mesa doesn't do by itself
typedef void (*glShaderSource_t)(unsigned, int, char const *const *, int const *);
static glShaderSource_t s_realShaderSource;

static void fp16ShaderSource(unsigned shader, int count, char const *const *strings, int const *lengths)
{
    std::string src;
    for (int i = 0; i < count; i++)
        src += lengths && lengths[i] >= 0 ? std::string(strings[i], lengths[i]) : std::string(strings[i]);

    if (src.find("_gl4es_Fog") != std::string::npos && src.find("gl_Frag") != std::string::npos)
    {
        static char const *const names[] = { "_gl4es_Fog.start", "_gl4es_Fog.end", "_gl4es_Fog.scale", "_gl4es_FogFragCoord" };
        for (char const *n : names)
        {
            std::string const from = n, to = std::string("nb16(") + n + ")";
            for (size_t pos = src.find("void main"); pos != std::string::npos && (pos = src.find(from, pos)) != std::string::npos; pos += to.size())
            {
                if (src.compare(pos > 5 ? pos - 5 : 0, 5, "nb16(") == 0)
                    continue;
                src.replace(pos, from.size(), to);
            }
        }
        LOG_F(INFO, "nbtest: fp16 fog emulation in a fragment shader");
        size_t const m = src.find("void main");
        if (m != std::string::npos)
            src.insert(m, "float nb16(float x) { float a = abs(x); float z = a - a;"
                          " if (a > 65504.0) return sign(x) / z; if (a < 6e-8) return sign(x) * z; return x; }\n");
    }

    char const *p = src.c_str();
    s_realShaderSource(shader, 1, &p, NULL);
}

// NBTEST_TEXLOG=1: log the texture formats gl4es hands to the real driver
typedef void (*glTexImage2D_t)(unsigned, int, int, int, int, int, unsigned, unsigned, void const *);
typedef void (*glTexSubImage2D_t)(unsigned, int, int, int, int, int, unsigned, unsigned, void const *);
static glTexImage2D_t s_realTexImage2D;
static glTexSubImage2D_t s_realTexSubImage2D;
static int s_texLogCount;

static void logTexImage2D(unsigned target, int level, int internalformat, int w, int h, int border, unsigned format, unsigned type, void const *data)
{
    s_realTexImage2D(target, level, internalformat, w, h, border, format, type, data);
    if (s_texLogCount++ < 60)
    {
        static auto const getError = (unsigned (*)(void))SDL_GL_GetProcAddress("glGetError");
        LOG_F(INFO, "nbtest: glTexImage2D level %d internal 0x%x %dx%d format 0x%x type 0x%x data %p error 0x%x", level, internalformat, w, h, format, type, data, getError());
    }
}

static void logTexSubImage2D(unsigned target, int level, int x, int y, int w, int h, unsigned format, unsigned type, void const *data)
{
    if (w != 256 && s_texLogCount++ < 400)
        LOG_F(INFO, "nbtest: glTexSubImage2D level %d at %d,%d %dx%d format 0x%x type 0x%x data %p", level, x, y, w, h, format, type, data);
    s_realTexSubImage2D(target, level, x, y, w, h, format, type, data);
}

typedef void (*glBindTexture_t)(unsigned, unsigned);
typedef void (*glActiveTexture_t)(unsigned);
static glBindTexture_t s_realBindTexture2;
static glActiveTexture_t s_realActiveTexture;
static void logBindTexture(unsigned target, unsigned tex)
{
    if (s_texLogCount < 400)
        LOG_F(INFO, "nbtest: glBindTexture 0x%x %u", target, tex);
    s_realBindTexture2(target, tex);
}
static void logActiveTexture(unsigned unit)
{
    if (s_texLogCount < 400)
        LOG_F(INFO, "nbtest: glActiveTexture 0x%x", unit);
    s_realActiveTexture(unit);
}

typedef void (*glPixelStorei_t)(unsigned, int);
static glPixelStorei_t s_realPixelStorei;
static void logPixelStorei(unsigned pname, int param)
{
    LOG_F(INFO, "nbtest: glPixelStorei 0x%x %d", pname, param);
    s_realPixelStorei(pname, param);
}

static void *glesProcAddress(char const *name)
{
    if (Bgetenv("NBTEST_TEXLOG"))
    {
        if (!strcmp(name, "glBindTexture"))
        {
            s_realBindTexture2 = (glBindTexture_t)SDL_GL_GetProcAddress(name);
            return (void *)logBindTexture;
        }
        if (!strcmp(name, "glActiveTexture"))
        {
            s_realActiveTexture = (glActiveTexture_t)SDL_GL_GetProcAddress(name);
            return (void *)logActiveTexture;
        }
        if (!strcmp(name, "glPixelStorei"))
        {
            s_realPixelStorei = (glPixelStorei_t)SDL_GL_GetProcAddress(name);
            return (void *)logPixelStorei;
        }
        if (!strcmp(name, "glTexImage2D"))
        {
            s_realTexImage2D = (glTexImage2D_t)SDL_GL_GetProcAddress(name);
            return (void *)logTexImage2D;
        }
        if (!strcmp(name, "glTexSubImage2D"))
        {
            s_realTexSubImage2D = (glTexSubImage2D_t)SDL_GL_GetProcAddress(name);
            return (void *)logTexSubImage2D;
        }
    }

    if (!strcmp(name, "glShaderSource") && Bgetenv("NBTEST_FP16FOG"))
    {
        s_realShaderSource = (glShaderSource_t)SDL_GL_GetProcAddress(name);
        return (void *)fp16ShaderSource;
    }

#if defined __x86_64__
#define NBTEST_HOOK(f) if (!strcmp(name, #f)) { s_real_##f = SDL_GL_GetProcAddress(name); return (void *)nbtest_wrap_##f; }
    NBTEST_COUNTED(NBTEST_HOOK)
#endif

    if (!strcmp(name, "glGetString"))
    {
        s_realGetString = (glGetString_t)SDL_GL_GetProcAddress(name);
        return (void *)fakeGetString;
    }
    if (!strcmp(name, "glGetIntegerv"))
    {
        s_realGetIntegerv = (glGetIntegerv_t)SDL_GL_GetProcAddress(name);
        return (void *)fakeGetIntegerv;
    }
    return SDL_GL_GetProcAddress(name);
}

static void mainFBSize(int *w, int *h)
{
    SDL_GL_GetDrawableSize(s_window, w, h);
}

int nbtest_initgl4es(SDL_Window *window)
{
    s_window = window;

    static bool initialized;
    if (!initialized)
    {
        setenv("LIBGL_USEVBO", "0", 1); // same as iosbits.mm
        if (Bgetenv("NBTEST_FP16FOG"))
            setenv("LIBGL_NOHIGHP", "1", 1); // fog in mediump, as gl4es does without highp
        set_getprocaddress(glesProcAddress);
        set_getmainfbsize(mainFBSize);
        initialize_gl4es();
        initialized = true;
    }
    return 0;
}

// NBTEST_STATECOUNT=1: count the game's own state changes per frame (what makes gl4es switch shader variants)
typedef void (*glCap_t)(unsigned);
typedef void (*glAlphaFunc_t)(unsigned, float);
typedef void (*glMatrixMode_t)(unsigned);
typedef void (*glLoadMatrixf_t)(float const *);
static glCap_t s_g4Enable, s_g4Disable;
static glAlphaFunc_t s_g4AlphaFunc;
static glMatrixMode_t s_g4MatrixMode;
static glLoadMatrixf_t s_g4LoadMatrixf;
static int s_cntAlphaTestOn, s_cntAlphaTestOff, s_cntFogOn, s_cntFogOff, s_cntBlend, s_cntAlphaFunc, s_cntTexMatrix, s_cntOtherCap;
static unsigned s_matrixMode;
static void cntEnable(unsigned cap) { if (cap == 0x0BC0) s_cntAlphaTestOn++; else if (cap == 0x0B60) s_cntFogOn++; else if (cap == 0x0BE2) s_cntBlend++; else s_cntOtherCap++; s_g4Enable(cap); }
static void cntDisable(unsigned cap) { if (cap == 0x0BC0) s_cntAlphaTestOff++; else if (cap == 0x0B60) s_cntFogOff++; else if (cap == 0x0BE2) s_cntBlend++; else s_cntOtherCap++; s_g4Disable(cap); }
static void cntAlphaFunc(unsigned f, float r) { s_cntAlphaFunc++; s_g4AlphaFunc(f, r); }
static void cntMatrixMode(unsigned m) { s_matrixMode = m; s_g4MatrixMode(m); }
static void cntLoadMatrixf(float const *m) { if (s_matrixMode == 0x1702 /*GL_TEXTURE*/) s_cntTexMatrix++; s_g4LoadMatrixf(m); }

void nbtest_logstatecounts(int frames)
{
    if (!Bgetenv("NBTEST_STATECOUNT"))
        return;
    LOG_F(INFO, "nbtest: game GL state per frame: alphatest on %d off %d, alphafunc %d, fog on %d off %d, blend %d, other caps %d, texture matrix %d",
          s_cntAlphaTestOn / frames, s_cntAlphaTestOff / frames, s_cntAlphaFunc / frames, s_cntFogOn / frames, s_cntFogOff / frames,
          s_cntBlend / frames, s_cntOtherCap / frames, s_cntTexMatrix / frames);
    s_cntAlphaTestOn = s_cntAlphaTestOff = s_cntAlphaFunc = s_cntFogOn = s_cntFogOff = s_cntBlend = s_cntOtherCap = s_cntTexMatrix = 0;
}

void *nbtest_glGetProcAddress(const char *name)
{
    if (Bgetenv("NBTEST_STATECOUNT"))
    {
        if (!strcmp(name, "glEnable")) { s_g4Enable = (glCap_t)gl4es_GetProcAddress(name); return (void *)cntEnable; }
        if (!strcmp(name, "glDisable")) { s_g4Disable = (glCap_t)gl4es_GetProcAddress(name); return (void *)cntDisable; }
        if (!strcmp(name, "glAlphaFunc")) { s_g4AlphaFunc = (glAlphaFunc_t)gl4es_GetProcAddress(name); return (void *)cntAlphaFunc; }
        if (!strcmp(name, "glMatrixMode")) { s_g4MatrixMode = (glMatrixMode_t)gl4es_GetProcAddress(name); return (void *)cntMatrixMode; }
        if (!strcmp(name, "glLoadMatrixf")) { s_g4LoadMatrixf = (glLoadMatrixf_t)gl4es_GetProcAddress(name); return (void *)cntLoadMatrixf; }
    }
    return gl4es_GetProcAddress(name);
}

//
// scripted run
//

struct Step
{
    int frame;
    std::string action, arg;
};

static std::vector<Step> s_steps;
static int s_frame;
static std::string s_pendingShot;
static int s_releaseKey = -1;

static void parseScript(void)
{
    static bool parsed;
    if (parsed)
        return;
    parsed = true;

    char const *env = getenv("NBTEST_SCRIPT");
    if (!env)
        return;

    std::string s(env), item;
    size_t pos = 0;

    while (pos <= s.size())
    {
        size_t end = s.find(';', pos);
        if (end == std::string::npos)
            end = s.size();
        item = s.substr(pos, end - pos);
        pos = end + 1;

        if (item.empty())
            continue;

        Step st;
        size_t const c1 = item.find(':'), c2 = item.find(':', c1 + 1);
        st.frame = atoi(item.substr(0, c1).c_str());
        st.action = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
        st.arg = c2 == std::string::npos ? "" : item.substr(c2 + 1);
        s_steps.push_back(st);
    }
}

static int keyFromName(std::string const &n)
{
    if (n == "esc") return sc_Escape;
    if (n == "enter") return sc_Return;
    if (n == "up") return sc_UpArrow;
    if (n == "down") return sc_DownArrow;
    if (n == "left") return sc_LeftArrow;
    if (n == "right") return sc_RightArrow;
    if (n == "space") return sc_Space;
    return -1;
}

// one line per player with everything a co-op save carries, to compare machines
static void logPlayers(void)
{
    for (int p = connecthead; p >= 0; p = connectpoint2[p])
    {
        PLAYER *pPlayer = &gPlayer[p];
        char weapons[kWeaponMax + 1] = {}, ammo[160] = {};
        int len = 0;
        for (int i = 0; i < kWeaponMax; i++)
            weapons[i] = pPlayer->hasWeapon[i] ? '1' : '0';
        for (int i = 0; i < 12; i++)
            len += Bsnprintf(ammo + len, sizeof(ammo) - len, "%s%d", i ? "," : "", pPlayer->ammoCount[i]);
        LOG_F(INFO, "nbtest: player %d %s health %d weapon %d/%d weapons %s ammo %s armor %d,%d,%d pack %d,%d,%d,%d,%d item %d sprite %d type %d at %d,%d,%d keys %d%d%d%d%d%d",
              p, gProfile[p].name, pPlayer->pXSprite ? pPlayer->pXSprite->health : -1, pPlayer->curWeapon, pPlayer->input.newWeapon,
              weapons, ammo, pPlayer->armor[0], pPlayer->armor[1], pPlayer->armor[2],
              pPlayer->packSlots[0].curAmount, pPlayer->packSlots[1].curAmount, pPlayer->packSlots[2].curAmount,
              pPlayer->packSlots[3].curAmount, pPlayer->packSlots[4].curAmount, pPlayer->packItemId,
              pPlayer->nSprite, pPlayer->pSprite ? pPlayer->pSprite->type : -1, pPlayer->pSprite ? pPlayer->pSprite->x : 0,
              pPlayer->pSprite ? pPlayer->pSprite->y : 0, pPlayer->pSprite ? pPlayer->pSprite->z : 0,
              pPlayer->hasKey[1], pPlayer->hasKey[2], pPlayer->hasKey[3], pPlayer->hasKey[4], pPlayer->hasKey[5], pPlayer->hasKey[6]);
    }
    LOG_F(INFO, "nbtest: game type %d episode %d level %d difficulty %d friendlyfire %d numplayers %d outofsync %d leveltime %d kills %d/%d",
          gGameOptions.nGameType, gGameOptions.nEpisode, gGameOptions.nLevel, gGameOptions.nDifficulty,
          gGameOptions.bFriendlyFire, numplayers, bOutOfSync, gLevelTime, gKillMgr.at4, gKillMgr.at0);
}

extern CGameMenu menuNetStart, menuMainWithSave, menuNetwork, menuMain;

static void openMenu(std::string const &name)
{
    if (name == "netstart")
        gGameMenuMgr.Push(&menuNetStart, -1);
    else if (name == "coopload")
    {
        SetupCoopLoadMenu(NULL);
        gGameMenuMgr.Push(&menuCoopLoad, -1);
    }
    else if (name == "coopsave")
    {
        SetupCoopSaveMenu(NULL);
        gGameMenuMgr.Push(&menuCoopSave, -1);
    }
    else if (name == "coopmid" || name == "coopcarry")
        OpenCoopSlotsMenu(name == "coopmid" ? kCoopSaveMid : kCoopSaveCarry);
    else if (name == "online")
    {
        SetupOnlineMenu(NULL);
        gGameMenuMgr.Push(&menuOnline, -1);
    }
    else if (name == "mods")
    {
        SetupModsMenu(NULL);
        gGameMenuMgr.Push(&menuMods, -1);
    }
    else if (name == "lan")
    {
        SetupLanGamesMenu(NULL);
        gGameMenuMgr.Push(&menuLanGames, -1);
    }
    else if (name == "host")
    {
        extern CGameMenu menuNetworkHost;
        SetupNetworkHostAddress();
        gGameMenuMgr.Push(&menuNetworkHost, -1);
    }
    else if (name == "main")
        gGameMenuMgr.Push(&menuMain, -1);
    else if (name == "network")
        gGameMenuMgr.Push(&menuNetwork, -1);
    else if (name == "ingame")
        gGameMenuMgr.Push(&menuMainWithSave, -1);
    else if (name == "close")
        gGameMenuMgr.Deactivate();
}

static void switchRenderer(bool polymost)
{
    int const newbpp = polymost ? 32 : 8;
    LOG_F(INFO, "nbtest: switching renderer to %s", polymost ? "polymost" : "classic");

    if (videoSetGameMode(fullscreen, xres, yres, newbpp, upscalefactor) < 0)
        LOG_F(ERROR, "nbtest: videoSetGameMode failed");
    else
        onvideomodechange(newbpp > 8);

    viewResizeView(gViewSize);
    gSetup.bpp = bpp;
}

void nbtest_frame(void)
{
    parseScript();
    s_frame++;
    if (s_frame == 1)
        LOG_F(INFO, "nbtest: glGetError after startup: 0x%x", glGetError());

    static bool s_loggedOutOfSync;
    if (bOutOfSync && !s_loggedOutOfSync)
    {
        LOG_F(ERROR, "nbtest: OUT OF SYNC at frame %d", s_frame);
        s_loggedOutOfSync = true;
    }

    if (s_releaseKey >= 0)
    {
        keySetState(s_releaseKey, 0);
        s_releaseKey = -1;
    }

    for (auto const &st : s_steps)
    {
        if (st.frame != s_frame)
            continue;

        LOG_F(INFO, "nbtest: frame %d: %s %s", s_frame, st.action.c_str(), st.arg.c_str());

        if (st.action == "shot")
            s_pendingShot = st.arg.empty() ? "shot" : st.arg;
        else if (st.action == "key")
        {
            int const sc = keyFromName(st.arg);
            if (sc >= 0)
            {
                keySetState(sc, 1);
                s_releaseKey = sc;
            }
        }
        else if (st.action == "renderer")
            switchRenderer(st.arg == "polymost");
        else if (st.action == "look")
        {
            if (gMe)
                gViewLook = st.arg == "up" ? F16(200) : st.arg == "down" ? F16(-250) : 0;
        }
        else if (st.action == "osd")
            OSD_Dispatch(st.arg.c_str());
        else if (st.action == "coopcontinue")
            LOG_F(INFO, "nbtest: coopContinueGame -> %d", coopContinueGame(atoi(st.arg.c_str())));
        else if (st.action == "menu")
            openMenu(st.arg);
        else if (st.action == "players")
            logPlayers();
        else if (st.action == "flash")
        {
            // what an explosion or a flare does to the player's view (brighter, visibility down)
            if (gMe)
                gMe->visibility = atoi(st.arg.c_str());
        }
        else if (st.action == "quit")
        {
            LOG_F(INFO, "nbtest: done");
            exit(0);
        }
    }
}

// called right before the GL swap: grab the back buffer if a screenshot is pending
void nbtest_beforeswap(int w, int h)
{
    {
        static uint64_t t0;
        static int frames;
        uint64_t const now = timerGetNanoTicks();
        if (!t0)
            t0 = now;
        if (++frames == 120)
        {
            double const ms = (double)(now - t0) / timerGetNanoTickRate() * 1000.0 / frames;
            LOG_F(INFO, "nbtest: %.2f ms/frame (%.1f fps) at %dx%d", ms, 1000.0 / ms, w, h);
            nbtest_logcounts(frames);
            nbtest_logstatecounts(frames);
            t0 = now;
            frames = 0;
        }
    }

    if (s_pendingShot.empty())
        return;

    {
        int depthBits = -1, stencilBits = -1;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depthBits);
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencilBits);
        GLint vp[4] = {};
        glGetIntegerv(GL_VIEWPORT, vp);
        LOG_F(INFO, "nbtest: rendmode %d bpp %d xdim %d ydim %d depth %d stencil %d viewport %d,%d %dx%d glError 0x%x",
              videoGetRenderMode(), bpp, xdim, ydim, depthBits, stencilBits, vp[0], vp[1], vp[2], vp[3], glGetError());
    }

    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    char name[256];
    Bsnprintf(name, sizeof(name), "nbtest_%s.ppm", s_pendingShot.c_str());

    if (FILE *fp = fopen(name, "wb"))
    {
        fprintf(fp, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--)
            for (int x = 0; x < w; x++)
                fwrite(&px[((size_t)y * w + x) * 4], 1, 3, fp);
        fclose(fp);
        LOG_F(INFO, "nbtest: wrote %s (%dx%d)", name, w, h);
    }

    s_pendingShot.clear();
}

#endif // NOTBLOOD_GL4ES_TEST
