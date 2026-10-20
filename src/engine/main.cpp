// main.cpp: initialisation & main loop

#include "engine.h"
#include "version.h"
#include "joystick.h"

extern "C"
{
    #ifdef RR_NEED_AUTH
        #include <tomcrypt.h>
    #endif
    #include "uv.h"
}
extern void cleargamma();

void cleanup()
{
    recorder::stop();
    extern void stopLocalServer(); stopLocalServer();
    cleanupserver();
    SDL_ShowCursor(1);
    if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
    cleargamma();
    freeocta(worldroot);
    extern void clear_command(); clear_command();
    extern void clear_console(); clear_console();
    extern void clear_mdls();    clear_mdls();
    extern void clear_sound();   clear_sound();
    SDL_Quit();
}

void quit()                     // normal exit
{
    extern void writeinitcfg();
    writeinitcfg();
    writeservercfg();
    abortconnect();
    disconnect();
    localdisconnect();
    writecfg();
    cleanup();
    exit(EXIT_SUCCESS);
}

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        defvformatstring(msg,s,s);
        puts(msg);

        if(errors <= 1) // avoid recursion
        {
            if(SDL_WasInit(SDL_INIT_VIDEO))
            {
                SDL_ShowCursor(1);
                if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
                cleargamma();
            }
            #ifdef WIN32
                MessageBox(NULL, msg, "Revelade Revolution fatal error", MB_OK|MB_SYSTEMMODAL);
            #endif
            SDL_Quit();
        }
    }

    uv_tty_reset_mode();
    ASSERT(0);
    exit(EXIT_FAILURE);
}

SDL_Window *screen = NULL;
SDL_GLContext glcontext = NULL;
static bool screen_fullscreen = false;

int curtime = 0, lastmillis = 1, elapsedtime = 0, totalmillis = 1;

dynent *player = NULL;

int initing = NOT_INITING;
static bool restoredinits = false;

bool initwarning(const char *desc, int level, int type)
{
    if(initing < level)
    {
        addchange(desc, type);
        return true;
    }
    return false;
}

#define SCR_MINW 320
#define SCR_MINH 200
#define SCR_MAXW 10000
#define SCR_MAXH 10000
#define SCR_DEFAULTW 1024
#define SCR_DEFAULTH 768
VARF(scr_w, SCR_MINW, -1, SCR_MAXW, initwarning("screen resolution"));
VARF(scr_h, SCR_MINH, -1, SCR_MAXH, initwarning("screen resolution"));
VARF(colorbits, 0, 0, 32, initwarning("color depth"));
VARF(depthbits, 0, 0, 32, initwarning("depth-buffer precision"));
VARF(stencilbits, 0, 0, 32, initwarning("stencil-buffer precision"));
VARF(fsaa, -1, -1, 16, initwarning("anti-aliasing"));
VARF(vsync, -1, -1, 1, initwarning("vertical sync"));

void writeinitcfg()
{
    if(restoredinits) return;
    stream *f = openutf8file("config/init.cfg", "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// modify settings in game\n");
    extern int fullscreen;
    f->printf("fullscreen %d\n", fullscreen);
    f->printf("scr_w %d\n", scr_w);
    f->printf("scr_h %d\n", scr_h);
    f->printf("colorbits %d\n", colorbits);
    f->printf("depthbits %d\n", depthbits);
    f->printf("stencilbits %d\n", stencilbits);
    f->printf("fsaa %d\n", fsaa);
    f->printf("vsync %d\n", vsync);
    extern int useshaders, shaderprecision, forceglsl;
    f->printf("shaders %d\n", useshaders);
    f->printf("shaderprecision %d\n", shaderprecision);
    f->printf("forceglsl %d\n", forceglsl);
    extern int soundchans, soundfreq, soundbufferlen;
    f->printf("soundchans %d\n", soundchans);
    f->printf("soundfreq %d\n", soundfreq);
    f->printf("soundbufferlen %d\n", soundbufferlen);
    delete f;
}

COMMAND(quit, "");

static void getbackgroundres(int &w, int &h)
{
    float wk = 1, hk = 1;
    if(w < 1024) wk = 1024.0f/w;
    if(h < 768) hk = 768.0f/h;
    wk = hk = max(wk, hk);
    w = int(ceil(w*wk));
    h = int(ceil(h*hk));
}

string backgroundcaption = "";
Texture *backgroundmapshot = NULL;
Texture *backgroundmapbg = NULL;
string backgroundmapname = "";
char *backgroundmapinfo = NULL;

void restorebackground()
{
    if(renderedframe) return;
    renderbackground(backgroundcaption[0] ? backgroundcaption : NULL, backgroundmapshot, backgroundmapbg, backgroundmapname[0] ? backgroundmapname : NULL, backgroundmapinfo, true);
}

float lastrangle = 0;
void renderbackground(const char *caption, Texture *mapshot, Texture *bgshot, const char *mapname, const char *mapinfo, bool restore, bool force)
{
    if(!inbetweenframes && !force) return;

    stopsounds(); // stop sounds while loading
 
    int w = scr_w, h = scr_h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    defaultshader->set();
    glEnable(GL_TEXTURE_2D);

    static int lastupdate = -1, lastw = -1, lasth = -1;
    static float backgroundu = 0, backgroundv = 0, detailu = 0, detailv = 0;
    static int numdecals = 0;
    static struct decal { float x, y, size; int side; } decals[12];
    if((renderedframe && !mainmenu && lastupdate != lastmillis) || lastw != w || lasth != h)
    {
        lastupdate = lastmillis;
        lastw = w;
        lasth = h;

        backgroundu = rndscale(1);
        backgroundv = rndscale(1);
        detailu = rndscale(1);
        detailv = rndscale(1);
        numdecals = sizeof(decals)/sizeof(decals[0]);
        numdecals = numdecals/3 + rnd((numdecals*2)/3 + 1);
        float maxsize = min(w, h)/16.0f;
        loopi(numdecals)
        {
            decal d = { rndscale(w), rndscale(h), maxsize/2 + rndscale(maxsize/2), rnd(2) };
            decals[i] = d;
        }

        (void) detailu; (void) detailv; (void)backgroundu; (void)backgroundv; //TODO: fix this function
    }
    else if(lastupdate != lastmillis) lastupdate = lastmillis;

    loopi(restore ? 1 : 3)
    {
        glColor3f(1, 1, 1);
        if((bgshot && bgshot != notexture) || (mapshot && mapshot != notexture))
        {
            #define fit(a,b) (a > b ? a/float(b) : b/float(a))
            #define ratio(a,b,c,d) (c >= d ? fit(a, c) : fit(b, d))
            int tw = bgshot ? 1280 : 512, th = bgshot ? 800 : 512;
            float scale = w >= h ? ratio(w, h, tw, th) : ratio(h, w, th, tw), nx = tw*scale, ny = th*scale, xind = (w-nx)/2, yind = (h-ny)/2;
            if(bgshot && bgshot != notexture) glBindTexture(GL_TEXTURE_2D, bgshot->id);
            else glBindTexture(GL_TEXTURE_2D, mapshot->id);
            glColor4f(0.5f, 0.5f, 0.5f, 1.f);
            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0, 0); glVertex2f(int(xind), int(yind));
            glTexCoord2f(1, 0); glVertex2f(int(nx-xind), int(yind));
            glTexCoord2f(0, 1); glVertex2f(int(xind), int(ny+yind));
            glTexCoord2f(1, 1); glVertex2f(int(nx-xind), int(ny+yind));
            glEnd();
        }
        else
        {
            settexture("packages/ui/rpg/menu/loadscreen.png", 0); // Shattered Shield load screen (ITEM 5)
            glColor4f(1.f, 1.f, 1.f, 1.f);
            //float bu = w*0.67f/256.0f + backgroundu, bv = h*0.67f/256.0f + backgroundv;
            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0,  0);  glVertex2f(0, 0);
            glTexCoord2f(1, 0);  glVertex2f(w, 0);
            glTexCoord2f(0,  1); glVertex2f(0, h);
            glTexCoord2f(1, 1); glVertex2f(w, h);
            glEnd();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        settexture("data/gui/background_detail.png", 0);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        //float du = w*0.8f/512.0f + detailu, dv = h*0.8f/512.0f + detailv;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0,  0);  glVertex2f(0, 0);
        glTexCoord2f(1, 0);  glVertex2f(w, 0);
        glTexCoord2f(0,  1); glVertex2f(0, h);
        glTexCoord2f(1, 1); glVertex2f(w, h);
        glEnd();
        settexture("data/gui/background_decal.png", 3);
        glColor4f(1, 1, 1, 0.5f);
        glBegin(GL_QUADS);
        //glBlendFunc(GL_ZERO, GL_SRC_COLOR);
        glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
        loopj(numdecals)
        {
            float hsz = decals[j].size, hx = clamp(decals[j].x, hsz, w-hsz), hy = clamp(decals[j].y, hsz, h-hsz), side = decals[j].side;
            glTexCoord2f(side,   0); glVertex2f(hx-hsz, hy-hsz);
            glTexCoord2f(1-side, 0); glVertex2f(hx+hsz, hy-hsz);
            glTexCoord2f(1-side, 1); glVertex2f(hx+hsz, hy+hsz);
            glTexCoord2f(side,   1); glVertex2f(hx-hsz, hy+hsz);
        }
        glEnd();
        // Shattered Shield: the Revelade logo overlay is skipped - our
        // full-bleed backdrop/loadscreen art already carries the branding
        // (the menu GUI draws the logo as its title treatment).
        // CLEANUP item 3: the stock mapshot preview box that used to be drawn
        // below (framed box + "?" placeholder + map name/info when no mapshot
        // texture exists) is removed entirely - the load screen is the art,
        // the caption, and the progress bar, nothing else.

        if(caption)
        {
            int tw = text_width(caption);
            float tsz = 0.04f*min(w, h)/FONTH,
                  tx = 0.5f*(w - tw*tsz), ty = h - 0.075f*1.5f*min(w, h) - 1.25f*FONTH*tsz;
            glPushMatrix();
            glTranslatef(tx, ty, 0);
            glScalef(tsz, tsz, 1);
            draw_text(caption, 0, 0);
            glPopMatrix();
        }
        glDisable(GL_BLEND);
        if(!restore) swapbuffers(false);
    }
    glDisable(GL_TEXTURE_2D);

    if(!restore)
    {
        renderedframe = false;
        copystring(backgroundcaption, caption ? caption : "");
        backgroundmapshot = mapshot;
        backgroundmapbg = bgshot;
        copystring(backgroundmapname, mapname ? mapname : "");
        if(mapinfo != backgroundmapinfo)
        {
            DELETEA(backgroundmapinfo);
            if(mapinfo) backgroundmapinfo = newstring(mapinfo);
        }
    }
}

float loadprogress = 0;

void renderprogress(float bar, const char *text, GLuint tex, bool background)   // also used during loading
{
    if(!inbetweenframes || envmapping) return;

    uv_run(uv_default_loop(), UV_RUN_NOWAIT);

    clientkeepalive();      // make sure our connection doesn't time out while loading maps etc.

    #ifdef __APPLE__
    interceptkey(SDLK_UNKNOWN); // keep the event queue awake to avoid 'beachball' cursor
    #endif

    extern int sdl_backingstore_bug;
    // CLEANUP item 1: while still loading (no world frame rendered yet),
    // always restore the full background before drawing the bar. Callers
    // passing background=false used to draw successive progress strings
    // over a stale back buffer, so the boot splash appeared to garble and
    // restart partway through loading. restorebackground() no-ops once a
    // real frame exists, so in-game progress calls are unaffected.
    if(background || sdl_backingstore_bug > 0 || !renderedframe) restorebackground();

    int w = scr_w, h = scr_h;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    defaultshader->set();
    glColor3f(1, 1, 1);

    float fh = 0.075f*min(w, h), fw = fh*10,
          fx = renderedframe ? w - fw - fh/4 : 0.5f*(w - fw),
          fy = renderedframe ? fh/4 : h - fh*1.5f;
    // Shattered Shield loading bar (ITEM 5, Oct 19): procedural silver/stone
    // track with gold fill. No textures, no zombie art. Revelade keeps its own.
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // outer border (dark stone)
    glColor4f(0.16f, 0.16f, 0.18f, 0.92f);
    glBegin(GL_QUADS);
    glVertex2f(fx - 2, fy - 2); glVertex2f(fx + fw + 2, fy - 2);
    glVertex2f(fx + fw + 2, fy + fh + 2); glVertex2f(fx - 2, fy + fh + 2);
    glEnd();
    // track (silver stone)
    glColor4f(0.52f, 0.54f, 0.58f, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(fx, fy); glVertex2f(fx + fw, fy);
    glVertex2f(fx + fw, fy + fh); glVertex2f(fx, fy + fh);
    glEnd();
    // inner highlight line (top edge of track)
    glColor4f(0.72f, 0.74f, 0.78f, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f(fx + 1, fy + 1); glVertex2f(fx + fw - 1, fy + 1);
    glVertex2f(fx + fw - 1, fy + 3); glVertex2f(fx + 1, fy + 3);
    glEnd();

    // gold fill (ember-gold), inset within the track
    float bh = fh - 8, by = fy + 4;
    float bx = fx + 4, bw = fw - 8;
    // CLEANUP item 3: the per-file loading text is no longer drawn in the
    // bar (callers still pass it; it is simply not rendered).
    if(bar > 0)
    {
        float fillw = bw * (bar > 1 ? 1 : bar);
        glColor4f(0.98f, 0.70f, 0.26f, 1.0f);
        glBegin(GL_QUADS);
        glVertex2f(bx, by); glVertex2f(bx + fillw, by);
        glVertex2f(bx + fillw, by + bh); glVertex2f(bx, by + bh);
        glEnd();
        glColor4f(1.0f, 0.85f, 0.45f, 0.9f);
        glBegin(GL_QUADS);
        glVertex2f(bx, by); glVertex2f(bx + fillw, by);
        glVertex2f(bx + fillw, by + 2); glVertex2f(bx, by + 2);
        glEnd();
    }


    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);


    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    swapbuffers(false);
}

// TEST-ONLY: show the loading bar for screenshots
static float testloadbar_val = -1;



void keyrepeat(bool on)
{
    // SDL2: key repeat is always enabled via SDL_KEYDOWN events with repeat flag; no API needed
}

bool grabinput = false, minimized = false;

void inputgrab(bool on)
{
#ifndef WIN32
    if(!screen_fullscreen) SDL_SetWindowGrab(screen, SDL_FALSE);
    else
#endif
    if(screen) SDL_SetWindowGrab(screen, on ? SDL_TRUE : SDL_FALSE);
    // Relative mouse mode is what actually confines the pointer so motion
    // deltas keep flowing past the screen edge; SDL_SetWindowGrab alone does
    // not confine the cursor in SDL2. Without this, mouse look stalls when
    // the pointer reaches the edge of the screen (~180 degrees of yaw).
    if(SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) < 0)
        conoutf(CON_WARN, "failed to %s relative mouse mode: %s", on ? "enable" : "disable", SDL_GetError());
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

void setfullscreen(bool enable)
{
    if(!screen) return;
#if defined(WIN32) || defined(__APPLE__)
    initwarning(enable ? "fullscreen" : "windowed");
#else
    if(enable != screen_fullscreen)
    {
        SDL_SetWindowFullscreen(screen, enable ? SDL_WINDOW_FULLSCREEN : 0);
        screen_fullscreen = enable;
        inputgrab(grabinput);
    }
#endif
}

#ifdef _DEBUG
VARF(fullscreen, 0, 0, 1, setfullscreen(fullscreen!=0));
#else
VARF(fullscreen, 0, 1, 1, setfullscreen(fullscreen!=0));
#endif

void screenres(int *w, int *h)
{
#if !defined(WIN32) && !defined(__APPLE__)
    if(initing >= INIT_RESET)
    {
#endif
        scr_w = clamp(*w, SCR_MINW, SCR_MAXW);
        scr_h = clamp(*h, SCR_MINH, SCR_MAXH);
#if defined(WIN32) || defined(__APPLE__)
        initwarning("screen resolution");
#else
        return;
    }
    if(!screen) return;
    SDL_SetWindowSize(screen, clamp(*w, SCR_MINW, SCR_MAXW), clamp(*h, SCR_MINH, SCR_MAXH));
    SDL_GetWindowSize(screen, &scr_w, &scr_h);
    glViewport(0, 0, scr_w, scr_h);
#endif
}

COMMAND(screenres, "ii");

static int curgamma = 100;
VARFP(gamma, 30, 100, 300,
{
    if(gamma == curgamma) return;
    curgamma = gamma;
	float f = gamma/100.0f;
    { /* SDL2: SDL_SetGamma removed; gamma adjustment not supported */ }
});

void restoregamma()
{
    // SDL2: SDL_SetGamma removed; no-op
}

void cleargamma()
{
    // SDL2: SDL_SetGamma removed; no-op
}

VAR(dbgmodes, 0, 0, 1);

int desktopw = 0, desktoph = 0;

void setupscreen(int &usedcolorbits, int &useddepthbits, int &usedfsaa)
{
    Uint32 winflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    #if defined(WIN32) || defined(__APPLE__)
    winflags = SDL_WINDOW_OPENGL;
    #endif
    if(fullscreen) winflags |= SDL_WINDOW_FULLSCREEN;
    screen_fullscreen = fullscreen;
    int nummodes = SDL_GetNumDisplayModes(0);
    if(nummodes > 0)
    {
        int widest = -1, best = -1;
        SDL_DisplayMode mwidest, mbest;
        memset(&mwidest, 0, sizeof(mwidest));
        memset(&mbest, 0, sizeof(mbest));
        for(int i = 0; i < nummodes; i++)
        {
            SDL_DisplayMode dm;
            if(SDL_GetDisplayMode(0, i, &dm) != 0) continue;
            if(dbgmodes) conoutf(CON_DEBUG, "mode[%d]: %d x %d", i, dm.w, dm.h);
            if(widest < 0 || dm.w > mwidest.w || (dm.w == mwidest.w && dm.h > mwidest.h))
                { widest = i; mwidest = dm; }
        }
        if(scr_w < 0 || scr_h < 0)
        {
            int w = scr_w, h = scr_h, ratiow = desktopw, ratioh = desktoph;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            if(ratiow <= 0 || ratioh <= 0) { ratiow = mwidest.w; ratioh = mwidest.h; }
            for(int i = 0; i < nummodes; i++)
            {
                SDL_DisplayMode dm;
                if(SDL_GetDisplayMode(0, i, &dm) != 0) continue;
                if(dm.w*ratioh == dm.h*ratiow)
                {
                    if(w <= dm.w && h <= dm.h && (best < 0 || dm.w < mbest.w))
                        { best = i; mbest = dm; }
                }
            }
        }
        if(best < 0)
        {
            int w = scr_w, h = scr_h;
            if(w < 0 && h < 0) { w = SCR_DEFAULTW; h = SCR_DEFAULTH; }
            else if(w < 0) w = (h*SCR_DEFAULTW)/SCR_DEFAULTH;
            else if(h < 0) h = (w*SCR_DEFAULTH)/SCR_DEFAULTW;
            for(int i = 0; i < nummodes; i++)
            {
                SDL_DisplayMode dm;
                if(SDL_GetDisplayMode(0, i, &dm) != 0) continue;
                if(w <= dm.w && h <= dm.h && (best < 0 || dm.w < mbest.w || (dm.w == mbest.w && dm.h < mbest.h)))
                    { best = i; mbest = dm; }
            }
        }
        if(winflags&SDL_WINDOW_FULLSCREEN)
        {
            if(best >= 0) { scr_w = mbest.w; scr_h = mbest.h; }
            else if(desktopw > 0 && desktoph > 0) { scr_w = desktopw; scr_h = desktoph; }
            else if(widest >= 0) { scr_w = mwidest.w; scr_h = mwidest.h; }
        }
        else if(best < 0)
        {
            scr_w = min(scr_w >= 0 ? scr_w : (scr_h >= 0 ? (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH : SCR_DEFAULTW), mwidest.w);
            scr_h = min(scr_h >= 0 ? scr_h : (scr_w >= 0 ? (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW : SCR_DEFAULTH), mwidest.h);
        }
        if(dbgmodes) conoutf(CON_DEBUG, "selected %d x %d", scr_w, scr_h);
    }
    if(scr_w < 0 && scr_h < 0) { scr_w = SCR_DEFAULTW; scr_h = SCR_DEFAULTH; }
    else if(scr_w < 0) scr_w = (scr_h*SCR_DEFAULTW)/SCR_DEFAULTH;
    else if(scr_h < 0) scr_h = (scr_w*SCR_DEFAULTH)/SCR_DEFAULTW;

    bool hasbpp = true;
    // SDL2: no SDL_VideoModeOK equivalent; attempt creation and fall back on failure

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // SDL2: vsync via SDL_GL_SetSwapInterval after context creation
    static int configs[] =
    {
        0x7, /* try everything */
        0x6, 0x5, 0x3, /* try disabling one at a time */
        0x4, 0x2, 0x1, /* try disabling two at a time */
        0 /* try disabling everything */
    };
    int config = 0;
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if(!depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    if(!fsaa)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
    loopi(sizeof(configs)/sizeof(configs[0]))
    {
        config = configs[i];
        if(!depthbits && config&1) continue;
        if(!stencilbits && config&2) continue;
        if(fsaa<=0 && config&4) continue;
        if(depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, config&1 ? depthbits : 16);
        if(stencilbits)
        {
            hasstencil = config&2 ? stencilbits : 0;
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, hasstencil);
        }
        else hasstencil = 0;
        if(fsaa>0)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config&4 ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config&4 ? fsaa : 0);
        }
        if(screen) { SDL_DestroyWindow(screen); screen = NULL; }
        if(glcontext) { SDL_GL_DeleteContext(glcontext); glcontext = NULL; }
        screen = SDL_CreateWindow("Revelade Revolution", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, scr_w, scr_h, winflags);
        if(screen)
        {
            glcontext = SDL_GL_CreateContext(screen);
            if(!glcontext) { SDL_DestroyWindow(screen); screen = NULL; }
        }
        if(screen && glcontext) break;
    }
    if(!screen || !glcontext) fatal("Unable to create OpenGL screen: %s", SDL_GetError());
    else
    {
        if(!hasbpp) conoutf(CON_WARN, "%d bit color buffer not supported - disabling", colorbits);
        if(depthbits && (config&1)==0) conoutf(CON_WARN, "%d bit z-buffer not supported - disabling", depthbits);
        if(stencilbits && (config&2)==0) conoutf(CON_WARN, "Stencil buffer not supported - disabling");
        if(fsaa>0 && (config&4)==0) conoutf(CON_WARN, "%dx anti-aliasing not supported - disabling", fsaa);
    }

    SDL_GetWindowSize(screen, &scr_w, &scr_h);

    if(vsync>=0) SDL_GL_SetSwapInterval(vsync);

    usedcolorbits = hasbpp ? colorbits : 0;
    useddepthbits = config&1 ? depthbits : 0;
    usedfsaa = config&4 ? fsaa : 0;
}


void resetgl()
{
    clearchanges(CHANGE_GFX);

    renderbackground("resetting OpenGL");

    extern void cleanupva();
    extern void cleanupparticles();
    extern void cleanupsky();
    extern void cleanupmodels();
    extern void cleanuptextures();
    extern void cleanuplightmaps();
    extern void cleanupblendmap();
    extern void cleanshadowmap();
    extern void cleanreflections();
    extern void cleanupglare();
    extern void cleanupdepthfx();
    extern void cleanupshaders();
    extern void cleanupgl();
    recorder::cleanup();
    cleanupva();
    cleanupparticles();
    cleanupsky();
    cleanupmodels();
    cleanuptextures();
    cleanuplightmaps();
    cleanupblendmap();
    cleanshadowmap();
    cleanreflections();
    cleanupglare();
    cleanupdepthfx();
    cleanupshaders();
    cleanupgl();

    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);

    extern void reloadfonts();
    extern void reloadtextures();
    extern void reloadshaders();
    inbetweenframes = false;
    if(!reloadtexture(*notexture) ||
       !reloadtexture("data/gui/logo.png") ||
       !reloadtexture("data/gui/logo_1024.png") ||
       !reloadtexture("data/gui/background.png") ||
       !reloadtexture("data/gui/background_detail.png") ||
       !reloadtexture("data/gui/background_decal.png") ||
       !reloadtexture("data/gui/mapshot_frame.png") ||
       !reloadtexture("data/gui/loading_frame.png") ||
       !reloadtexture("data/gui/loading_bar.png"))
        fatal("failed to reload core texture");
    reloadfonts();
    inbetweenframes = true;
    renderbackground("initializing...");
	restoregamma();
    reloadshaders();
    reloadtextures();
    initlights();
    allchanged(true);
}

COMMAND(resetgl, "");

vector<SDL_Event> events;

void pushevent(const SDL_Event &e)
{
    events.add(e); 
}

static bool filterevent(const SDL_Event &event)
{
    switch(event.type)
    {
        case SDL_MOUSEMOTION:
            #ifndef WIN32
            if(grabinput && !screen_fullscreen)
            {
                int ww = 0, hh = 0;
                if(screen) SDL_GetWindowSize(screen, &ww, &hh);
                if(event.motion.x == ww / 2 && event.motion.y == hh / 2) 
                    return false;  // ignore any motion events generated by SDL_WarpMouse
                #ifdef __APPLE__
                if(event.motion.y == 0) 
                    return false;  // let mac users drag windows via the title bar
                #endif
            }
            #endif
            break;
    }
    return true;
}

static inline bool pollevent(SDL_Event &event)
{
    while(SDL_PollEvent(&event))
    {
        if(filterevent(event)) return true;
    }
    return false;
}

bool interceptkey(int sym)
{
    static int lastintercept = SDLK_UNKNOWN;
    int len = lastintercept == sym ? events.length() : 0;
    SDL_Event event;
    while(pollevent(event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION: break;
            default: pushevent(event); break;
        }
    }
    lastintercept = sym;
    if(sym != SDLK_UNKNOWN) for(int i = len; i < events.length(); i++)
    {
        if(events[i].type == SDL_KEYDOWN && events[i].key.keysym.sym == sym) { events.remove(i); return true; }
    }
    return false;
}

static void ignoremousemotion()
{
    SDL_Event e;
    SDL_PumpEvents();
    while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION) > 0);
}

static void resetmousemotion()
{
#ifndef WIN32
    if(grabinput && !screen_fullscreen && screen)
    {
        int ww = 0, hh = 0;
        SDL_GetWindowSize(screen, &ww, &hh);
        SDL_WarpMouseInWindow(screen, ww / 2, hh / 2);
    }
#endif
}

static void checkmousemotion(int &dx, int &dy)
{
    loopv(events)
    {
        SDL_Event &event = events[i];
        if(event.type != SDL_MOUSEMOTION)
        { 
            if(i > 0) events.remove(0, i); 
            return; 
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
    events.setsize(0);
    SDL_Event event;
    while(pollevent(event))
    {
        if(event.type != SDL_MOUSEMOTION)
        {
            events.add(event);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
}

void checkinput()
{
    SDL_Event event;
    int lasttype = 0, lastbut = 0;
    bool mousemoved = false; 
    while(events.length() || pollevent(event))
    {
        if(events.length()) event = events.remove(0);

        switch(event.type)
        {
            case SDL_QUIT:
                quit();
                return;

            case SDL_WINDOWEVENT:
                #if !defined(WIN32) && !defined(__APPLE__)
                if(event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    screenres(&event.window.data1, &event.window.data2);
                }
                #endif
                if(event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    inputgrab(grabinput = true);
                else if(event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    inputgrab(grabinput = false);
                else if(event.window.event == SDL_WINDOWEVENT_MINIMIZED)
                    minimized = true;
                else if(event.window.event == SDL_WINDOWEVENT_RESTORED)
                    minimized = false;
                break;

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                keypress(event.key.keysym.sym, event.key.state==SDL_PRESSED, 0);
                break;

            case SDL_TEXTINPUT:
            {
                // SDL2: decode UTF-8 text input to Unicode codepoints
                const char *t = event.text.text;
                while(*t)
                {
                    int uni = 0;
                    unsigned char c = (unsigned char)*t;
                    if(c < 0x80) { uni = c; t += 1; }
                    else if((c & 0xE0) == 0xC0) { uni = ((c & 0x1F) << 6) | (t[1] & 0x3F); t += 2; }
                    else if((c & 0xF0) == 0xE0) { uni = ((c & 0x0F) << 12) | ((t[1] & 0x3F) << 6) | (t[2] & 0x3F); t += 3; }
                    else { t += 1; continue; }
                    keypress(-6, true, uni2cube(uni));
                    keypress(-6, false, 0);
                }
                break;
            }

            case SDL_MOUSEMOTION:
                if(grabinput)
                {
                    int dx = event.motion.xrel, dy = event.motion.yrel;
                    checkmousemotion(dx, dy);
                    // RPG: the game gets first refusal on raw deltas (free-cursor
                    // maps) before the engine's mouse-look path. Menus still win
                    // via g3d_movecursor; mouse-look itself is gated by
                    // game::allowmouselook() inside mousemove().
                    if(!g3d_movecursor(dx, dy)) { game::cursormove(dx, dy); mousemove(dx, dy); }
                    mousemoved = true;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if(lasttype==event.type && lastbut==event.button.button) break; // why?? get event twice without it
                keypress(-event.button.button, event.button.state!=0, 0);
                lasttype = event.type;
                lastbut = event.button.button;
                break;
        }
        input::processEvent(event);
    }
    if(mousemoved) resetmousemotion();
    input::move();
}

void swapbuffers(bool overlay)
{
    recorder::capture(overlay);
    SDL_GL_SwapWindow(screen);
}
 
VAR(menufps, 0, 60, 1000);
VARP(maxfps, 0, 200, 1000);

void limitfps(int &millis, int curmillis)
{
    int limit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(!limit) return;
    static int fpserror = 0;
    int delay = 1000/limit - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%limit;
        if(fpserror >= limit)
        {
            ++delay;
            fpserror -= limit;
        }
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        }
    }
}

#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out)("Revelade Revolution Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#ifdef _AMD64_
    STACKFRAME64 sf = {{context->Rip, 0, AddrModeFlat}, {}, {context->Rbp, 0, AddrModeFlat}, {context->Rsp, 0, AddrModeFlat}, 0};
    while(::StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
    {
        union { IMAGEHLP_SYMBOL64 sym; char symext[sizeof(IMAGEHLP_SYMBOL64) + sizeof(string)]; };
        sym.SizeOfStruct = sizeof(sym);
        sym.MaxNameLength = sizeof(symext) - sizeof(sym);
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(line);
        DWORD64 symoff;
        DWORD lineoff;
        if(SymGetSymFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#else
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
    {
        union { IMAGEHLP_SYMBOL sym; char symext[sizeof(IMAGEHLP_SYMBOL) + sizeof(string)]; };
        sym.SizeOfStruct = sizeof(sym);
        sym.MaxNameLength = sizeof(symext) - sizeof(sym);
        IMAGEHLP_LINE line;
        line.SizeOfStruct = sizeof(line);
        DWORD symoff, lineoff;
        if(SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#endif
        {
            char *del = strrchr(line.FileName, '\\');
            formatstring(t)("%s - %s [%d]\n", sym.Name, del ? del + 1 : line.FileName, line.LineNumber);
            concatstring(out, t);
        }
    }
    fatal(out);
}
#endif

#define MAXFPSHISTORY 60

int fpspos = 0, fpshistory[MAXFPSHISTORY];

void resetfpshistory()
{
    loopi(MAXFPSHISTORY) fpshistory[i] = 1;
    fpspos = 0;
}

void updatefpshistory(int millis)
{
    fpshistory[fpspos++] = max(1, min(1000, millis));
    if(fpspos>=MAXFPSHISTORY) fpspos = 0;
}

void getfps(int &fps, int &bestdiff, int &worstdiff)
{
    int total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    loopi(MAXFPSHISTORY-1)
    {
        int millis = fpshistory[i];
        total += millis;
        if(millis < best) best = millis;
        if(millis > worst) worst = millis;
    }

    fps = (1000*MAXFPSHISTORY)/total;
    bestdiff = 1000/best-fps;
    worstdiff = fps-1000/worst;
}

void getfps_(int *raw)
{
    int fps, bestdiff, worstdiff;
    if(*raw) fps = 1000/fpshistory[(fpspos+MAXFPSHISTORY-1)%MAXFPSHISTORY];
    else getfps(fps, bestdiff, worstdiff);
    intret(fps);
}

COMMANDN(getfps, getfps_, "i");
CS_SCONSTANT(TIG_GAME, game::gameident());
CS_CONSTANT(VERSION,            version::getVersion());
CS_CONSTANT(VERSION_MAJOR,      version::getMajor());
CS_CONSTANT(VERSION_MINOR,      version::getMinor());
CS_CONSTANT(VERSION_PATCH,      version::getPatch());
CS_CONSTANT(VERSION_TAG,        version::getTag());
CS_SCONSTANT(VERSION_NAME,      version::getVersionString());

bool inbetweenframes = false, renderedframe = true;

static bool findarg(int argc, char **argv, const char *str)
{
    for(int i = 1; i<argc; i++) if(strstr(argv[i], str)==argv[i]) return true;
    return false;
}

static int clockrealbase = 0, clockvirtbase = 0;
static void clockreset() { clockrealbase = SDL_GetTicks(); clockvirtbase = totalmillis; }
VARFP(clockerror, 990000, 1000000, 1010000, clockreset());
VARFP(clockfix, 0, 0, 1, clockreset());

int getclockmillis()
{
    int millis = SDL_GetTicks() - clockrealbase;
    if(clockfix) millis = int(millis*(double(clockerror)/1000000));
    millis += clockvirtbase;
    return max(millis, totalmillis);
}

namespace console
{
    extern int verbosity;
}

VAR(numcpus, 1, 1, 16);

#ifdef __APPLE__
int real_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    #ifdef RR_NEED_AUTH
    ltc_mp = ltm_desc;

    {
        int status;
        status = register_prng(&sprng_desc);
        if(status != CRYPT_OK)
        {
            fatal("Error registering sprng: %s\n", error_to_string(status));
            return 1;
        }

        status = register_hash(&sha1_desc);
        if (status != CRYPT_OK)
        {
            fatal("Error registering sha1: %s", error_to_string(status));
            return 1;
        }
    }
    #endif

    #ifdef WIN32
    //atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    #ifndef _DEBUG
    #ifndef __GNUC__
    __try {
    #endif
    #endif
    #endif

    uv_loop_t *loop = uv_default_loop();
    char *load = NULL, *initscript = NULL;

    #define INIT_LOG(s) puts("Init: " s)

    initing = INIT_RESET;

    findRootDirectory("data");

    bool doinit = true;
    const char *homeDirectory = NULL;

    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'h':
                printf("No help available, please have a look at: http://theintercooler.com\n");
                return 0;
                break;

            case 'q':
                homeDirectory = sethomedir(&argv[i][2]);
                break;

            case 'r':
                doinit = false;
                break;
        }
    }

    if(!homeDirectory)
    {
        #ifdef WIN32
        homeDirectory = sethomedir("$APPDATA\\ReveladeRevolution");
        #else
            homeDirectory = sethomedir("$HOME/.revelade-revolution");
        #endif
    }

    if(homeDirectory)
    {
        conoutf("Using homeDirectory: %s", homeDirectory);
    }

    if (doinit) execfile("config/init.cfg", false);

    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': /* parsed first */ break;
            case 'k': printf("Adding package directory: %s\n", &argv[i][2]); addpackagedir(&argv[i][2]); break;
            case 'r': execfile(argv[i][2] ? &argv[i][2] : "config/init.cfg", false); restoredinits = true; break;
            case 'd':
                console::verbosity = atoi(&argv[i][2]);
                if(console::verbosity == -1)
                {
                    console::verbosity = ~0;
                }
                printf("Setting log mask to %i\n", console::verbosity);
                break;
            case 'w': scr_w = clamp(atoi(&argv[i][2]), SCR_MINW, SCR_MAXW); if(!findarg(argc, argv, "-h")) scr_h = -1; break;
            case 'h': scr_h = clamp(atoi(&argv[i][2]), SCR_MINH, SCR_MAXH); if(!findarg(argc, argv, "-w")) scr_w = -1; break;
            case 'z': depthbits = atoi(&argv[i][2]); break;
            case 'b': colorbits = atoi(&argv[i][2]); break;
            case 'a': fsaa = atoi(&argv[i][2]); break;
            case 'v': vsync = atoi(&argv[i][2]); break;
            case 't': fullscreen = atoi(&argv[i][2]); break;
            case 's': stencilbits = atoi(&argv[i][2]); break;
            case 'f':
            {
                extern int useshaders, shaderprecision, forceglsl;
                int sh = -1, prec = shaderprecision;
                for(int j = 2; argv[i][j]; j++) switch(argv[i][j])
                {
                    case 'a': case 'A': forceglsl = 0; sh = 1; break;
                    case 'g': case 'G': forceglsl = 1; sh = 1; break;
                    case 'f': case 'F': case '0': sh = 0; break;
                    case '1': case '2': case '3': if(sh < 0) sh = 1; prec = argv[i][j] - '1'; break;
                    default: break;
                }
                useshaders = sh > 0 ? 1 : 0;
                shaderprecision = prec;
                break;
            }
            case 'l':
            {
                char pkgdir[] = "packages/";
                load = strstr(path(&argv[i][2]), path(pkgdir));
                if(load) load += sizeof(pkgdir)-1;
                else load = &argv[i][2];
                break;
            }
            case 'x': initscript = &argv[i][2]; break;
            default: if(!serveroption(argv[i])) gameargs.add(argv[i]); break;
        }
        else gameargs.add(argv[i]);
    }
    initing = NOT_INITING;

    numcpus = clamp(guessnumcpus(), 1, 16);
    INIT_LOG("sdl");

    int par = 0;
    #ifdef _DEBUG
        par = SDL_INIT_NOPARACHUTE;

        #ifdef WIN32
        SetEnvironmentVariable("SDL_DEBUG", "1");
        #endif
    #endif

    if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|SDL_INIT_AUDIO|par)<0) fatal("Unable to initialize SDL: %s", SDL_GetError());
    SDL_StartTextInput();

    INIT_LOG("net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);

    INIT_LOG("game");
    game::parseoptions(gameargs);
    initserver(false, false);  // never returns if dedicated

    game::initclient();
    input::init();

    INIT_LOG("video: mode");
    SDL_DisplayMode dm;
    if(SDL_GetDesktopDisplayMode(0, &dm) == 0)
    {
        desktopw = dm.w;
        desktoph = dm.h;
    }
    int usedcolorbits = 0, useddepthbits = 0, usedfsaa = 0;
    setupscreen(usedcolorbits, useddepthbits, usedfsaa);

    INIT_LOG("video: misc");

    defformatstring(windowtitle)("Revelade Revolution %s", version::getVersionString());

    if(screen) SDL_SetWindowTitle(screen, windowtitle);
    keyrepeat(false);
    SDL_ShowCursor(0);

    conoutf(CON_INIT, "Revelade Revolution %s - %s", version::getVersionString(), version::getVersionDate());

    INIT_LOG("gl");
    gl_checkextensions();
    gl_init(scr_w, scr_h, usedcolorbits, useddepthbits, usedfsaa);
    notexture = textureload("packages/textures/notexture.png");
    if(!notexture) fatal("could not find core textures");

    INIT_LOG("console");
    if(!execfile("data/stdlib.cfg", false)) fatal("cannot find data files (data/stdlib.cfg)");   // this is the first file we load.
    if(!execfile("data/font.cfg", false)) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

    inbetweenframes = true;
    renderbackground("initializing...");

    INIT_LOG("gl: effects");
    loadshaders();
    particleinit();
    initdecals();

    INIT_LOG("world");
    #ifdef RR_USE_BULLET
        physics::init();
    #endif
    camera1 = player = game::iterdynents(0);
    emptymap(0, true, NULL, false);

    INIT_LOG("sound");
    initsound();

    INIT_LOG("cfg");
    execfile("data/keymap.cfg");
    execfile("data/stdedit.cfg");
    execfile("data/menus.cfg");
    execfile("data/sounds.cfg");
    execfile("data/brush.cfg");
    execfile("mybrushes.cfg", false);

    identflags |= IDF_PERSIST;

    initing = INIT_LOAD;
    if(!execfile(game::savedconfig(), false))
    {
        execfile(game::defaultconfig());
        writecfg(game::restoreconfig());
    }
    execfile(game::autoexec(), false);
    initing = NOT_INITING;

    identflags &= ~IDF_PERSIST;

    string gamecfgname;
    copystring(gamecfgname, "data/game_");
    concatstring(gamecfgname, game::gameident());
    concatstring(gamecfgname, ".cfg");
    execfile(gamecfgname);

    game::loadconfigs();

    identflags |= IDF_PERSIST;

    if(execfile("config/once.cfg", false)) remove(findfile("config/once.cfg", "rb"));

    if(load)
    {
        INIT_LOG("localconnect");
        //localconnect();
        game::changemap(load);
    }

    if(initscript) execute(initscript);

    INIT_LOG("mainloop");

    initmumble();
    resetfpshistory();

    inputgrab(grabinput = true);
    ignoremousemotion();

    for(;;)
    {
        static int frames = 0;
        int millis = getclockmillis();
        limitfps(millis, totalmillis);
        elapsedtime = millis - totalmillis;
        static int timeerr = 0;
        int scaledtime = game::scaletime(elapsedtime) + timeerr;
        curtime = scaledtime/100;
        timeerr = scaledtime%100;
        if(!multiplayer(false) && curtime>200) curtime = 200;
        if(game::ispaused()) curtime = 0;
        lastmillis += curtime;
        totalmillis = millis;
        updatetime();
 
        checkinput();
        menuprocess();
        tryedit();

        uv_run(loop, UV_RUN_NOWAIT);
        if(lastmillis) game::updateworld();

        checksleep(lastmillis);

        serverslice(false, 0);

        if(frames) updatefpshistory(elapsedtime);
        frames++;

        // miscellaneous general game effects
        recomputecamera();
        updateparticles();
        updatesounds();

        if(minimized) continue;

        inbetweenframes = false;
        if(mainmenu) gl_drawmainmenu(scr_w, scr_h);
        else gl_drawframe(scr_w, scr_h);
        swapbuffers();
        renderedframe = inbetweenframes = true;
    }

    ASSERT(0);
    return EXIT_FAILURE;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}
