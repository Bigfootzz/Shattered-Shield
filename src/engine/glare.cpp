#include "engine.h"
#include "rendertarget.h"

static struct glaretexture : rendertarget
{
    bool dorender()
    {
        extern void drawglare();
        drawglare();
        return true;
    }
} glaretex;

void cleanupglare()
{
    glaretex.cleanup(true);
}

VARFP(glaresize, 6, 8, 10, cleanupglare());
VARP(glare, 0, 0, 1);
VARP(blurglare, 0, 4, 7);
VARP(blurglaresigma, 1, 50, 200);

VAR(debugglare, 0, 0, 1);

void viewglaretex()
{
    if(!glare) return;
    glaretex.debug();
}

bool glaring = false;

void drawglaretex()
{
    if(!glare || renderpath==R_FIXEDFUNCTION) return;

    glaretex.render(1<<glaresize, 1<<glaresize, blurglare, blurglaresigma/100.0f);
}

FVARP(glarescale, 0, 1, 8);

void addglare()
{
    if(!glare || renderpath==R_FIXEDFUNCTION) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    SETSHADER(glare);

    glBindTexture(GL_TEXTURE_2D, glaretex.rendertex);

    setlocalparamf("glarescale", SHPARAM_PIXEL, 0, glarescale, glarescale, glarescale);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0, 0); glVertex3f(-1, -1, 0);
    glTexCoord2f(1, 0); glVertex3f( 1, -1, 0);
    glTexCoord2f(0, 1); glVertex3f(-1,  1, 0);
    glTexCoord2f(1, 1); glVertex3f( 1,  1, 0);
    glEnd();

    glDisable(GL_BLEND);
}

rendertarget wgtg;
GLuint wgfb = 0;
GLuint wgtex = 0;
int wgw = 0, wgh = 0;

void addwaterglow()
{
    //if (!waterglow.initialized) waterglow.setup(scr_w, scr_h);
    //else waterglow.cleanup();

    if(!wgtex || wgw != scr_w || wgh != scr_h)
    {
        if(!wgtex) glGenTextures(1, &wgtex);
        wgw = scr_w;
        wgh = scr_h;
        createtexture(wgtex, scr_w, scr_h, NULL, 3, 0, GL_RGB, GL_TEXTURE_RECTANGLE_ARB);
    }

    //glEnable(GL_BLEND);
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFunc(GL_ONE, GL_ONE);

    //glDisable(GL_TEXTURE_2D);
    //glEnable(GL_TEXTURE_RECTANGLE_ARB);

    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, wgtex);
    glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 0, 0, scr_w, scr_h);

    if(hasFBO)
    {
        if(!wgfb) glGenFramebuffers_(1, &wgfb);
        glBindFramebuffer_(GL_FRAMEBUFFER_EXT, wgfb);
        glFramebufferTexture2D_(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, wgtex, 0);
    }


    //setenvparamf("waterscale", SHPARAM_PIXEL, 1-((lastmillis%1000)/1000));

    SETSHADER(hafterwater);

    glColor4f(1.f, 1.f, 1.f, 1.f);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0, 0); glVertex3f(-1, -1, 0);
    glTexCoord2f(1, 0); glVertex3f( 1, -1, 0);
    glTexCoord2f(0, 1); glVertex3f(-1,  1, 0);
    glTexCoord2f(1, 1); glVertex3f( 1,  1, 0);
    glEnd();

    if(hasFBO) glBindFramebuffer_(GL_FRAMEBUFFER_EXT, 0);

    //glActiveTexture_(GL_TEXTURE1);
    //glBindTexture(GL_TEXTURE_RECTANGLE_ARB, wgtex);
    //glActiveTexture_(GL_TEXTURE1);


    /*SETSHADER(vafterwater);

    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0, 0); glVertex3f(-1, -1, 0);
    glTexCoord2f(1, 0); glVertex3f( 1, -1, 0);
    glTexCoord2f(0, 1); glVertex3f(-1,  1, 0);
    glTexCoord2f(1, 1); glVertex3f( 1,  1, 0);
    glEnd();*/

    //glBindTexture(GL_TEXTURE_RECTANGLE_ARB, wgtex);

    //glDisable(GL_TEXTURE_RECTANGLE_ARB);
    //glEnable(GL_TEXTURE_2D);

    //glDisable(GL_BLEND);

    //glBindTexture(GL_TEXTURE_RECTANGLE_ARB, wgtex);
    //glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 0, 0, scr_w, scr_h);

    defaultshader->set();


    //glDeleteTextures(1, &wgtex);

    //waterglow.render(scr_w, scr_h, 0, 0.f);
}
