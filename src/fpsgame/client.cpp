#include "game.h"
#include "engine.h"
#include "rpg/rpg.h"
#include "rpg/abilities.h"
#include "rpg/stats.h"
#include "rpg/items.h"
#include "auth.h"

// rendergl.cpp ground-truth camera basis (global scope)
extern vec camdir, camright, camup;

namespace game
{
    #include "gamemode/capture.h"
    #include "gamemode/ctf.h"
    #include "gamemode/infection.h"
    #include "gamemode/dmsp.h"

    clientmode *cmode = NULL;
    captureclientmode capturemode;
    ctfclientmode ctfmode;
    infectionclientmode infectionmode;
    dmspclientmode dmspmode;

    void setclientmode()
    {
        if(m_capture) cmode = &capturemode;
        else if(m_ctf) cmode = &ctfmode;
        else if(m_infection) cmode = &infectionmode;
        else if(m_dmsp) cmode = &dmspmode;
        else cmode = NULL;
    }

    struct ServerCommand
    {
        int req;
        const char *callback;
    };
    vector<ServerCommand> commands;
    static int reqid = 0;

    bool senditemstoserver = false, sendcrc = false; // after a map change, since server doesn't have map data
    int lastping = 0;

    bool remote = false, demoplayback = false, gamepaused = false;
    int sessionid = 0, mastermode = MM_OPEN, connectionState = CONNECTION_STATE_NONE, gamespeed = 100;
    string servinfo = "", connectpass = ""; ustring randomData = "";

    VARP(deadpush, 1, 2, 20);

    int otherclients(bool ai = false, bool nospec = true)
    {
        int n = 0; // ai don't count
        loopv(players) if(players[i] && (ai || players[i]->ai.type == ai::AI_TYPE_NONE) && (!nospec || players[i]->state != CS_SPECTATOR)) n++;
        return n;
    }
    ICOMMAND(otherclients, "", (), otherclients());

    // --- RPG: Shattered Shield ability layer (milestone step 3, additive) ---
    // The client only ASKS to cast: the server validates cooldown, mana and
    // target and applies damage. Yaw is client-advisory, used by the server
    // for the melee front-cone check only. Bindable, e.g. bind Q ability1.
    // Targeting v1: casts carry the client-selected target as ADVISORY data.
    // The server validates it (monster, alive, in range) and falls back to the
    // old nearest-target behavior when no selection is set (-1).
    static void rpg_facetarget(); // defined with the targeting helpers below
    static void rpg_castability(int ability)
    {
        if(!player1) return;
        rpg_facetarget(); // face the selected target; yaw is client-advisory
        addmsg(N_RPG_CAST, "ri3", ability, int(player1->yaw), rpg::selectedtarget());
    }
    ICOMMAND(ability1, "", (), rpg_castability(0));
    ICOMMAND(ability2, "", (), rpg_castability(1));

    // --- RPG: Shattered Shield inventory (milestone step 4, additive) ---
    // The client only ASKS (item id / slot); the server validates and pushes
    // fresh state (N_RPG_INV) after every mutation. The GUIs in
    // data/menus/rpg.cfg read the client cache live (re-executed every frame).
    ICOMMAND(isrpgmap, "", (), intret(rpg::mapactive(getclientmap()) ? 1 : 0));

    // --- RPG: Shattered Shield mouse & targeting v1 (additive) ---
    // Free cursor + click intent on RPG maps. Picking is a real raycast: the
    // cursor pixel is unprojected through the inverse MVP matrix into a world
    // ray from the camera, tested against monster bounding cylinders (the same
    // intersect() the weapon code uses), loot-drop interaction cylinders, and
    // the world itself (raycubepos) for occlusion. NPCs have no system yet:
    // the monster loop is the single place an NPC entity check slots in later.
    // Pick volume matches the RENDERED model, not the physics body. The engine's
    // intersect() cylinder (eyeheight/aboveeye, ~18 tall) is the FPS shooting
    // volume; the zombie models render ~65 tall, so a click on the visible body
    // must test a visual-size cylinder or real clicks miss (proven in testing).
    // Height comes from the model's own bounding box - the same data the
    // renderer culls with - with a humanoid fallback if the box is degenerate.
    static float rpg_visualheight(fpsent *d)
    {
        const playermodelinfo &mdl = getplayermodelinfo(d);
        if(mdl.ffa && mdl.ffa[0])
        {
            model *m = loadmodel(mdl.ffa);
            if(m)
            {
                vec center, br;
                m->boundbox(center, br);
                float h = (center.z + br.z) - (center.z - br.z);
                if(h > 8.0f) return h + 4.0f; // small margin
            }
        }
        return 70.0f; // fallback: humanoid zombie visual
    }
    static bool rpg_pickintersect(fpsent *d, const vec &from, const vec &to, float &dist)
    {
        vec feet = d->feetpos();
        vec bottom(feet.x, feet.y, feet.z), top(feet.x, feet.y, feet.z + rpg_visualheight(d));
        if(!linecylinderintersect(from, to, bottom, top, max(d->radius, 7.0f), dist)) return false;
        dist *= from.dist(to); // linecylinderintersect returns a fraction; we need world units // linecylinderintersect returns a fraction; we need world units
        return true;
    }
    struct rpgpickresult { int kind; fpsent *mon; int lootidx; int npci; }; // kind: 0 none, 1 loot, 2 monster, 3 npc
    static rpgpickresult rpg_pick(float cx, float cy, int w, int h)
    {
        rpgpickresult r; r.kind = 0; r.mon = NULL; r.lootidx = -1; r.npci = -1;
        if(!camera1 || !player1 || w <= 0 || h <= 0) return r;
        vec4 scr;
        scr.x = 2.0f*cx/w - 1.0f;
        scr.y = 1.0f - 2.0f*cy/h;
        scr.z = 0.0f; scr.w = 1.0f;
        vec wp = invmvpmatrix.perspectivetransform(scr);
        vec dir = vec(wp).sub(camera1->o);
        if(dir.magnitude() < 1e-6f) return r;
        dir.normalize();
        vec from = camera1->o;
        vec to = vec(dir).mul(1024.0f).add(from);
        vec hitpos;
        float worldd = raycubepos(from, dir, hitpos, 0, RAY_CLIPMAT|RAY_SKIPFIRST);
        if(worldd < 0) worldd = 1e16f;
        fpsent *best = NULL; float bestd = 1e16f;
        loopv(clients)
        {
            fpsent *d = clients[i];
            if(!d || d == player1) continue;
            if(d->ai.type != ai::AI_TYPE_MONSTER) continue;
            if(d->state != CS_ALIVE) continue;
            float dist;
            if(!rpg_pickintersect(d, from, to, dist)) continue;
            if(dist < bestd) { best = d; bestd = dist; }
        }
        // NPC quest givers (quest chain v1): static cylinders from the shared
        // NPC table; nearest pick wins against monsters/loot below.
        int bestnpc = -1;
        for(int ni = 0; ni < rpg::npccount(); ni++)
        {
            const rpg::npcdef *nd = rpg::npcat(ni);
            if(!nd) continue;
            vec bottom(nd->x, nd->y, nd->z), top(nd->x, nd->y, nd->z + 70.0f);
            float dist;
            if(!linecylinderintersect(from, to, bottom, top, 7.0f, dist)) continue;
            dist *= from.dist(to);
            if(dist < bestd) { best = NULL; bestnpc = ni; bestd = dist; }
        }
        rpg::lootdrop drops[64]; int nd = 0;
        int bestloot = -1; float bestld = 1e16f;
        if(rpg::getclientloot(drops, 64, nd)) loopi(nd)
        {
            vec bottom(drops[i].x, drops[i].y, drops[i].z), top(drops[i].x, drops[i].y, drops[i].z + 4.0f);
            float dist;
            if(!linecylinderintersect(from, to, bottom, top, 2.5f, dist)) continue;
            if(dist < bestld) { bestld = dist; bestloot = drops[i].idx; }
        }
        // nearest of monster/npc/loot wins; the world occludes everything behind it
        if(bestnpc >= 0 && bestd < worldd && bestd <= bestld) { r.kind = 3; r.npci = bestnpc; }
        else if(best && bestd < worldd && bestd <= bestld) { r.kind = 2; r.mon = best; }
        else if(bestloot >= 0 && bestld < worldd) { r.kind = 1; r.lootidx = bestloot; }
        return r;
    }

    // Resolve the selected clientnum to a live monster, NULL when gone/dead.
    static fpsent *rpg_selmon()
    {
        int cn = rpg::selectedtarget();
        if(cn < 0) return NULL;
        loopv(clients)
        {
            fpsent *d = clients[i];
            if(d && d->clientnum == cn && d->ai.type == ai::AI_TYPE_MONSTER && d->state == CS_ALIVE) return d;
        }
        return NULL;
    }

    static int rpg_facetill = 0; // face the target until this time (manual casts)
    // RPG follow camera (user design, Oct 19): when casting at a selected
    // target, the character faces the target so the strike visibly plays out
    // toward the mob on screen. Sets a 1s face window; the per-frame update
    // below keeps facing the target while auto-attacking.
    static void rpg_facetarget()
    {
        if(!player1 || !rpg::mapactive(getclientmap())) return;
        fpsent *sel = rpg_selmon();
        if(!sel) return;
        vec delta(sel->o); delta.sub(player1->o); delta.z = 0;
        if(delta.magnitude() < 0.01f) return;
        float yaw, pitch;
        vectoyawpitch(delta, yaw, pitch);
        player1->yaw = yaw;
        rpg_facetill = totalmillis + 1000;
    }

    static void rpg_screenwh(int &w, int &h)
    {
        w = h = 0;
        if(screen) SDL_GetWindowSize(screen, &w, &h);
    }

    // Click intent: button 0 = left (select / deselect), 1 = right (select + auto-attack).
    // Plain function (not inline in the ICOMMAND): the macro parser splits on bare
    // commas, so multi-declaration bodies live here instead.
    bool rpg_chattabclick(float vx, float vy); // fps.cpp: chat box tab clicks

    static void rpg_doclick(int button)
    {
        if(!rpg::mapactive(getclientmap())) return;
        if(!player1 || player1->state != CS_ALIVE) return;
        int w = 0; int h = 0; rpg_screenwh(w, h);
        float cx = 0; float cy = 0;
        rpg::cursorget(cx, cy); rpg::cursorclamp(cx, cy, w, h); rpg::cursorget(cx, cy);
        // chat box tabs eat their own clicks (the rest of the box is click-through)
        if(rpg_chattabclick(cx*1800.0f/h, cy*1800.0f/h)) return;
        rpgpickresult pk = rpg_pick(cx, cy, w, h);
        if(button == 1)
        {
            if(pk.kind == 2 && pk.mon)
            {
                rpg::selecttarget(pk.mon->clientnum);
                rpg::setautoattack(true);
                conoutf("rpg: auto-attacking '%s'", pk.mon->name);
            }
            else
            {
                if(rpg::autoattacking() || rpg::selectedtarget() >= 0 || rpg::selectednpc() >= 0) conoutf("rpg: auto-attack stopped");
                rpg::selecttarget(-1); rpg::selectnpc(-1); rpg::setautoattack(false);
            }
        }
        else
        {
            if(pk.kind == 2 && pk.mon)
            {
                rpg::selecttarget(pk.mon->clientnum);
                conoutf("rpg: selected '%s' (%d/%d hp)", pk.mon->name, pk.mon->health, pk.mon->maxhealth);
            }
            else if(pk.kind == 3 && pk.npci >= 0)
            {
                const rpg::npcdef *nd = rpg::npcat(pk.npci);
                rpg::selectnpc(pk.npci);
                rpg::setautoattack(false);
                conoutf("rpg: selected '%s'", nd ? nd->name : "npc");
            }
            else if(pk.kind == 1)
            {
                conoutf("rpg: loot here - walk over it to pick it up");
            }
            else
            {
                if(rpg::selectedtarget() >= 0 || rpg::selectednpc() >= 0) conoutf("rpg: deselected");
                rpg::selecttarget(-1); rpg::selectnpc(-1); rpg::setautoattack(false);
            }
        }
    }
    // "D" signature: acts on press only; the release re-fire is a no-op by design.
    ICOMMAND(rpgclick, "Di", (int *down, int *button), { if(*down) rpg_doclick(*button); });
    // Quest chain v1: talk to the selected NPC (F key on RPG maps). The server
    // runs the dialogue state machine; right-click stays auto-attack (never talk).
    ICOMMAND(rpgtalk, "", (),
    {
        if(!rpg::mapactive(getclientmap()) || !player1 || player1->state != CS_ALIVE) return;
        int ni = rpg::selectednpc();
        if(ni < 0) { conoutf("rpg: select someone to talk to first (left-click)"); return; }
        addmsg(N_RPG_TALK, "ri", ni);
    });
    ICOMMAND(rpgcleartarget, "", (), rpg::cleartargeting());
    // --- TEST-ONLY scaffolding (mouse & targeting v1 harness) ---
    // The headless harness cannot move the OS pointer, so these drive the same
    // code paths the real input takes: rpgtestdelta exercises game::cursormove
    // (the exact function the engine event loop calls), rpgtestsnap projects
    // the nearest live monster to screen space and parks the cursor on it so
    // rpgclick runs the genuine raycast pick, and rpgtestmouselook exposes the
    // mouse-look gate for both map kinds.
    ICOMMAND(rpgtestdelta, "ii", (int *dx, int *dy), cursormove(*dx, *dy));
    ICOMMAND(rpgcursorpos, "", (),
    {
        float cx = 0; float cy = 0; rpg::cursorget(cx, cy);
        conoutf("rpgtest: cursorpos %.0f %.0f", cx, cy);
    });
    ICOMMAND(rpgtestmouselook, "", (), conoutf("rpgtest: allowmouselook=%d (map %s)", allowmouselook() ? 1 : 0, getclientmap()));
    ICOMMAND(rpgtestbinds, "", (),
    {
        char *b1 = executestr("getbind MOUSE1"); char *b2 = executestr("getbind MOUSE2");
        conoutf("rpgtest: MOUSE1=[%s] MOUSE2=[%s]", b1 ? b1 : "(none)", b2 ? b2 : "(none)");
        delete[] b1; delete[] b2;
    });
    static int rpg_snapok = 0;
    static int rpg_snapwait_until = 0;
    static bool rpg_dosnap()
    {
        rpg_snapok = 0;
        if(!rpg::mapactive(getclientmap()) || !player1 || !camera1) return false;
        fpsent *best = NULL; float bestd = 1e16f;
        loopv(clients)
        {
            fpsent *d = clients[i];
            if(!d || d == player1 || d->ai.type != ai::AI_TYPE_MONSTER || d->state != CS_ALIVE) continue;
            float dist = d->o.dist(player1->o);
            if(dist < bestd) { best = d; bestd = dist; }
        }
        if(!best) { conoutf("rpgtestsnap: no live monster"); return false; }
        vec p = best->feetpos(); p.z += rpg_visualheight(best)*0.55f;
        vec4 in; in.x = p.x; in.y = p.y; in.z = p.z; in.w = 1.0f;
        vec4 v; mvmatrix.transform(in, v);
        if(v.z >= -2.0f) { conoutf("rpgtestsnap: '%s' behind camera, not snapping", best->name); return false; }
        vec4 c; mvpmatrix.transform(in, c);
        if(c.w == 0) return false;
        int w = 0; int h = 0; rpg_screenwh(w, h);
        float nx = c.x/c.w; float ny = c.y/c.w;
        float sx = (nx*0.5f + 0.5f)*w; float sy = (0.5f - ny*0.5f)*h;
        rpg::cursorset(sx, sy);
        conoutf("rpgtestsnap: cursor -> %.0f,%.0f on '%s' (mon o=%.0f,%.0f,%.0f cam=%.0f,%.0f,%.0f yaw=%.0f player=%.0f,%.0f,%.0f pyaw=%.0f)",
            sx, sy, best->name, best->o.x, best->o.y, best->o.z, camera1->o.x, camera1->o.y, camera1->o.z,
            camera1->yaw, player1->o.x, player1->o.y, player1->o.z, player1->yaw);
        // deep pick diagnostic: rebuild the ray and test the monster + world explicitly
        {
            vec4 dscr; dscr.x = 2.0f*sx/w - 1.0f; dscr.y = 1.0f - 2.0f*sy/h; dscr.z = 0.0f; dscr.w = 1.0f;
            vec dwp = invmvpmatrix.perspectivetransform(dscr);
            vec ddir = vec(dwp).sub(camera1->o); ddir.normalize();
            vec dhit; float dworld = raycubepos(camera1->o, ddir, dhit, 0, RAY_CLIPMAT|RAY_SKIPFIRST);
            float ddist = -2; bool dh = rpg_pickintersect(best, camera1->o, vec(ddir).mul(1024.0f).add(camera1->o), ddist);
            conoutf("rpgtestsnap: diag dir=%.2f,%.2f,%.2f worldd=%.1f inthit=%d intdist=%.1f cyl r=%.1f eye=%.1f above=%.1f",
                ddir.x, ddir.y, ddir.z, dworld, dh ? 1 : 0, ddist, best->radius, best->eyeheight, best->aboveeye);
        }
        {
            vec off = vec(best->o).sub(camera1->o);
            conoutf("rpgtestsnap: basis camdir=%.2f,%.2f,%.2f camright=%.2f,%.2f,%.2f off=%.1f,%.1f,%.1f fwd=%.1f right=%.1f",
                camdir.x, camdir.y, camdir.z, camright.x, camright.y, camright.z,
                off.x, off.y, off.z, off.dot(camdir), off.dot(camright));
        }
        rpgpickresult verify = rpg_pick(sx, sy, w, h);
        conoutf("rpgtestsnap: verify pick kind=%d %s", verify.kind, verify.mon ? verify.mon->name : "(none)");
        rpg_snapok = (verify.kind == 2) ? 1 : 0;
        return verify.kind == 2;
    }
    ICOMMAND(rpgtestsnap, "", (), rpg_dosnap(););
    ICOMMAND(rpgtestsnapok, "", (), intret(rpg_snapok));
    static int rpg_snapclick_btn = -1;
    ICOMMAND(rpgtestsnapwait, "", (), { rpg_snapok = 0; rpg_snapclick_btn = -1; rpg_snapwait_until = totalmillis + 15000; conoutf("rpgtestsnapwait: tracking monster each frame for 15s"); });
    ICOMMAND(rpgtestsnapstop, "", (), { rpg_snapwait_until = 0; rpg_snapclick_btn = -1; conoutf("rpgtestsnapwait: stopped"); });
    static string rpg_shotarm;
    ICOMMAND(rpgtestsnapshot, "s", (char *n), { copystring(rpg_shotarm, n); conoutf("rpgtestsnapshot: armed '%s'", n); });
    ICOMMAND(rpgtestshottaken, "", (), intret(rpg_shotarm[0] ? 0 : 1));
    ICOMMAND(rpgtestsnapclick, "i", (int *b), { rpg_snapok = 0; rpg_snapclick_btn = *b; rpg_snapwait_until = totalmillis + 15000; conoutf("rpgtestsnapclick: snap+click(%d)", *b); });
    ICOMMAND(rpgtestcursorset, "ii", (int *x, int *y), rpg::cursorset(float(*x), float(*y)););
    ICOMMAND(rpgteststats, "", (), { int l = 0; int x = 0; rpg::getclientstats(l, x); conoutf("rpgteststats: level=%d xp=%d", l, x); });
    // Target-frame queries for the HUD (never authoritative)
    ICOMMAND(rpgtargetname, "", (), { fpsent *d = rpg_selmon(); result(d ? d->name : ""); });
    ICOMMAND(rpgtargethp, "", (), { fpsent *d = rpg_selmon(); intret(d ? d->health : 0); });
    ICOMMAND(rpgtargetmaxhp, "", (), { fpsent *d = rpg_selmon(); intret(d ? d->maxhealth : 0); });

    static int rpg_testforcerange = 0; // TEST-ONLY: bypass auto-attack client range check
    ICOMMAND(rpgtestsetxp, "i", (int *v), { rpg::setclientxp(*v); conoutf("rpgtestsetxp: %d", *v); });
    ICOMMAND(rpgtestforcerange, "i", (int *v), { rpg_testforcerange = *v ? 1 : 0; conoutf("rpgtestforcerange: %s", rpg_testforcerange ? "ON" : "OFF"); });
    // Per-frame targeting maintenance. Called from updateworld() on RPG maps.
    void updatetargeting()
    {
        if(rpg_snapwait_until && rpg::mapactive(getclientmap()))
        {
            if(totalmillis >= rpg_snapwait_until) { rpg_snapwait_until = 0; rpg_snapclick_btn = -1; conoutf("rpgtestsnapwait: timed out"); }
            else if(rpg_dosnap())
            {
                if(rpg_shotarm[0]) { conoutf("rpgtestsnapshot: taking '%s'", rpg_shotarm); defformatstring(cmd)("screenshot %s", rpg_shotarm); rpg_shotarm[0] = '\0'; executestr(cmd); }
                if(rpg_snapclick_btn >= 0)
                {
                    int b = rpg_snapclick_btn;
                    rpg_snapclick_btn = -1; rpg_snapwait_until = 0;
                    conoutf("rpgtestsnapclick: snapped, clicking %d", b);
                    rpg_doclick(b);
                }
                else if(!rpg_snapok) { rpg_snapok = 1; conoutf("rpgtestsnapwait: tracking"); }
            }
        }
        if(!rpg::mapactive(getclientmap()) || !player1) return;
        int w, h; rpg_screenwh(w, h);
        if(w <= 0 || h <= 0) return;
        float cx, cy; rpg::cursorget(cx, cy); rpg::cursorclamp(cx, cy, w, h); rpg::cursorget(cx, cy);

        // hover state (real pick every frame; logs transitions only)
        rpgpickresult pk = rpg_pick(cx, cy, w, h);
        int hov = rpg::HOVER_DEFAULT;
        if(pk.kind == 2) hov = rpg::HOVER_HOSTILE;
        else if(pk.kind == 1) hov = rpg::HOVER_LOOT;
        if(hov != rpg::hoverstate())
        {
            rpg::sethoverstate(hov);
            conoutf("rpg: hover %s", hov == rpg::HOVER_HOSTILE ? "hostile" : (hov == rpg::HOVER_LOOT ? "loot" : "default"));
        }

        // selection validity: target died or left -> deselect, stop auto-attack
        fpsent *sel = rpg_selmon();
        if(rpg::selectedtarget() >= 0 && !sel)
        {
            conoutf("rpg: target lost, deselecting");
            rpg::selecttarget(-1); rpg::setautoattack(false);
            return;
        }
        if(player1->state != CS_ALIVE)
        {
            if(rpg::autoattacking()) { rpg::setautoattack(false); conoutf("rpg: auto-attack stopped (you died)"); }
            return;
        }

        // Follow-camera facing (user design, Oct 19): the character faces the
        // camera's forward direction; while fighting a selected target (auto-
        // attack, or 1s after a manual cast) it faces the target instead, so
        // strikes visibly play out toward the mob on screen.
        {
            fpsent *facesel = (sel && (rpg::autoattacking() || totalmillis < rpg_facetill)) ? sel : NULL;
            if(facesel)
            {
                vec delta(facesel->o); delta.sub(player1->o); delta.z = 0;
                if(delta.magnitude() > 0.01f)
                {
                    float fyaw, fpitch;
                    vectoyawpitch(delta, fyaw, fpitch);
                    player1->yaw = fyaw;
                }
            }
            else player1->yaw = camera1->yaw;
        }

        // auto-attack: repeated Power Strike (ability 0) through the real,
        // server-validated cast path. Client-side pre-checks only avoid chat
        // spam; cooldown/mana/range/line-of-sight are enforced by the server.
        // Out of range: HOLD in place (no teleport, no auto-path) and say so.
        if(rpg::autoattacking() && sel)
        {
            static int lasttry = 0, lastholdlog = 0;
            static bool washolding = false;
            if(totalmillis - lasttry >= 250)
            {
                lasttry = totalmillis;
                const rpg::abilitydef &ab = rpg::ability(0);
                vec delta(sel->o); delta.sub(player1->o);
                bool inrange = rpg_testforcerange || delta.magnitude() <= ab.range; // TEST-ONLY
                if(!inrange)
                {
                    if(!washolding || totalmillis - lastholdlog > 5000)
                    {
                        conoutf("rpg: auto-attack holding - target out of range (%.0f > %.0f)", delta.magnitude(), ab.range);
                        lastholdlog = totalmillis;
                    }
                    washolding = true;
                }
                else if(rpg::cooldownleft(0, totalmillis) > 0) washolding = false;
                else
                {
                    int mana = 0, maxmana = 0;
                    rpg::getclientmana(mana, maxmana);
                    if(mana >= ab.manacost)
                    {
                        if(washolding) conoutf("rpg: auto-attack resumed");
                        washolding = false;
                        conoutf("rpg: auto-attack tick -> '%s' (hp %d/%d)", sel->name, sel->health, sel->maxhealth);
                        rpg_castability(0);
                    }
                    else if(!washolding || totalmillis - lastholdlog > 5000)
                    {
                        conoutf("rpg: auto-attack holding - waiting for mana");
                        lastholdlog = totalmillis; washolding = true;
                    }
                }
            }
        }
    }
    ICOMMAND(rpginventory, "", (), { addmsg(N_RPG_INVL, "r"); showgui("rpginv"); });
    ICOMMAND(rpgcharacter, "", (), { addmsg(N_RPG_INVL, "r"); showgui("rpgchar"); });
    ICOMMAND(rpgequip, "s", (char *s), addmsg(N_RPG_EQUIP, "rs", s));
    ICOMMAND(rpgunequip, "s", (char *s), addmsg(N_RPG_UNEQUIP, "ri", rpg::slotid(s)));
    ICOMMAND(rpguse, "s", (char *s), addmsg(N_RPG_USE, "rs", s));
    ICOMMAND(rpgdrop, "s", (char *s), addmsg(N_RPG_DROP, "rs", s));
    // client cache queries for the GUIs (never authoritative)
    static int rpg_cln = 0;
    static rpg::itemstack rpg_clinv[64];
    static int rpg_cleq[rpg::SLOT_COUNT];
    static void rpg_refreshcl() { rpg::getclientinv(rpg_clinv, 64, rpg_cln, rpg_cleq); }
    ICOMMAND(rpginvcount, "", (), { rpg_refreshcl(); intret(rpg_cln); });
    ICOMMAND(rpginvid, "i", (int *i), { rpg_refreshcl(); result(*i >= 0 && *i < rpg_cln ? rpg::item(rpg_clinv[*i].item).id : ""); });
    ICOMMAND(rpginvname, "i", (int *i), { rpg_refreshcl(); result(*i >= 0 && *i < rpg_cln ? rpg::itemname(rpg_clinv[*i].item) : ""); });
    ICOMMAND(rpginvicon, "i", (int *i), { rpg_refreshcl(); result(*i >= 0 && *i < rpg_cln ? rpg::item(rpg_clinv[*i].item).icon : ""); });
    ICOMMAND(rpginvnum, "i", (int *i), { rpg_refreshcl(); intret(*i >= 0 && *i < rpg_cln ? rpg_clinv[*i].count : 0); });
    ICOMMAND(rpginvtype, "i", (int *i), { rpg_refreshcl(); intret(*i >= 0 && *i < rpg_cln ? rpg::item(rpg_clinv[*i].item).type : -1); });
    ICOMMAND(rpgeqid, "s", (char *s), { rpg_refreshcl(); int id = rpg::slotid(s); result(id >= 0 && rpg_cleq[id] >= 0 ? rpg::item(rpg_cleq[id]).id : ""); });
    ICOMMAND(rpgeqname, "s", (char *s), { rpg_refreshcl(); int id = rpg::slotid(s); result(id >= 0 && rpg_cleq[id] >= 0 ? rpg::itemname(rpg_cleq[id]) : "(empty)"); });
    ICOMMAND(rpgeqicon, "s", (char *s), { rpg_refreshcl(); int id = rpg::slotid(s); result(id >= 0 && rpg_cleq[id] >= 0 ? rpg::item(rpg_cleq[id]).icon : "packages/ui/rpg/icons/icon_empty.png"); });
    ICOMMAND(rpgcharline, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); defformatstring(s)("Level %d   XP %d", l, x); result(s); });
    ICOMMAND(rpgcdleft, "i", (int *i), intret(rpg::cooldownleft(*i, totalmillis)));
    ICOMMAND(rpgcell, "i", (int *i), {
        rpg_refreshcl();
        static string p;
        if(*i >= 0 && *i < rpg_cln)
        {
            const char *id = rpg::item(rpg_clinv[*i].item).id;
            const char *base = strchr(id, '_');
            formatstring(p)("packages/ui/rpg/icons/cell_%s.png", base ? base + 1 : id);
            result(p);
        }
        else result("packages/ui/rpg/slots/slot.png");
    });
    ICOMMAND(rpgeqcell, "s", (char *s), {
        rpg_refreshcl();
        int id = rpg::slotid(s);
        static string p;
        if(id >= 0 && rpg_cleq[id] >= 0)
        {
            const char *iid = rpg::item(rpg_cleq[id]).id;
            const char *base = strchr(iid, '_');
            formatstring(p)("packages/ui/rpg/icons/cell_%s.png", base ? base + 1 : iid);
            result(p);
        }
        else result("packages/ui/rpg/slots/slot.png");
    });
    ICOMMAND(rpginvstat, "i", (int *i), {
        rpg_refreshcl();
        static string s; s[0] = 0;
        if(*i >= 0 && *i < rpg_cln)
        {
            const rpg::itemdef &d = rpg::item(rpg_clinv[*i].item);
            if(d.type == rpg::ITEM_WEAPON) formatstring(s)("+%d damage", d.damage);
            else if(d.type == rpg::ITEM_ARMOR) formatstring(s)("Armor %d", d.armor);
            else if(d.type == rpg::ITEM_CONSUMABLE) formatstring(s)("Heals %d", d.heal);
        }
        result(s);
    });
    ICOMMAND(rpgeqstat, "s", (char *s), {
        rpg_refreshcl();
        int id = rpg::slotid(s);
        static string r; r[0] = 0;
        if(id >= 0 && rpg_cleq[id] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[id]);
            if(d.type == rpg::ITEM_WEAPON) formatstring(r)("+%d damage", d.damage);
            else if(d.type == rpg::ITEM_ARMOR) formatstring(r)("Armor %d", d.armor);
        }
        result(r);
    });
    ICOMMAND(rpgeqtotal, "", (), {
        rpg_refreshcl();
        int wd = 0; int ar = 0;
        loopi(rpg::SLOT_COUNT) if(rpg_cleq[i] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[i]);
            if(d.type == rpg::ITEM_WEAPON) wd += d.damage;
            else if(d.type == rpg::ITEM_ARMOR) ar += d.armor;
        }
        defformatstring(s)("Weapon +%d dmg   Armor %d", wd, ar);
        result(s);
    });
    // --- RPG: Shattered Shield character-screen data (window confinement pass).
    // Read-only client-cache queries so the K screen can show real stats/tabs.
    ICOMMAND(rpglevel, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); intret(l); });
    ICOMMAND(rpgxp, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); intret(x); });
    ICOMMAND(rpgweapondmg, "", (), {
        rpg_refreshcl(); int wd = 0;
        loopi(rpg::SLOT_COUNT) if(rpg_cleq[i] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[i]);
            if(d.type == rpg::ITEM_WEAPON) wd += d.damage;
        }
        intret(wd);
    });
    ICOMMAND(rpgarmortotal, "", (), {
        rpg_refreshcl(); int ar = 0;
        loopi(rpg::SLOT_COUNT) if(rpg_cleq[i] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[i]);
            if(d.type == rpg::ITEM_ARMOR) ar += d.armor;
        }
        intret(ar);
    });
    ICOMMAND(rpgeqarmor, "s", (char *s), {
        rpg_refreshcl(); int id = rpg::slotid(s);
        intret(id >= 0 && rpg_cleq[id] >= 0 ? rpg::item(rpg_cleq[id]).armor : 0);
    });
    ICOMMAND(rpgabname, "i", (int *i), { result(rpg::validability(*i) ? rpg::ability(*i).name : ""); });
    ICOMMAND(rpgabdmg, "i", (int *i), { intret(rpg::validability(*i) ? rpg::ability(*i).damage : 0); });
    ICOMMAND(rpgabmana, "i", (int *i), { intret(rpg::validability(*i) ? rpg::ability(*i).manacost : 0); });
    ICOMMAND(rpgabcdstr, "i", (int *i), {
        if(rpg::validability(*i))
        {
            int ms = rpg::ability(*i).cooldownms;
            defformatstring(s)("%d.%ds", ms/1000, (ms%1000)/100);
            result(s);
        }
        else result("");
    });
    ICOMMAND(rpgmana, "", (), { int m = 0; int mm = 0; rpg::getclientmana(m, mm); intret(m); });
    ICOMMAND(rpgmaxmana, "", (), { int m = 0; int mm = 0; rpg::getclientmana(m, mm); intret(mm); });
    // --- stat system (milestone step 5): read-only client-cache queries for the K screen ---
    // Attributes derive from the client-known level (automatic growth); no protocol change.
    ICOMMAND(rpgmaxhp, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); intret(rpg::stat_maxhp(l)); });
    ICOMMAND(rpgattrname, "i", (int *i), { result(rpg::attrname(*i)); });
    ICOMMAND(rpgattr, "i", (int *i), { int l = 1; int x = 0; rpg::getclientstats(l, x); intret(rpg::attrscore(l, *i)); });
    ICOMMAND(rpgcritchance, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); defformatstring(s)("%.1f%%", rpg::stat_critchance(l)); result(s); });
    ICOMMAND(rpgcritmult, "", (), { int l = 1; int x = 0; rpg::getclientstats(l, x); defformatstring(s)("x%.2f", rpg::stat_critmult(l)); result(s); });
    ICOMMAND(rpgarmorred, "", (), {
        rpg_refreshcl(); int ar = 0;
        loopi(rpg::SLOT_COUNT) if(rpg_cleq[i] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[i]);
            if(d.type == rpg::ITEM_ARMOR) ar += d.armor;
        }
        defformatstring(s)("%d%%", rpg::stat_armorreductionpct(ar)); result(s);
    });
    ICOMMAND(rpgresistname, "i", (int *i), { result(rpg::resistname(*i)); });
    // data only: all resists are 0 until typed monster damage arrives (no behavior).
    ICOMMAND(rpgresist, "i", (int *i), { intret(0); });
    ICOMMAND(rpgabtype, "i", (int *i), { result(rpg::validability(*i) ? rpg::ability(*i).damagetype : ""); });
    // Attribute-derived ability damage for the Offense tab (Power Strike adds weapon + STR).
    ICOMMAND(rpgabstatdmg, "i", (int *i), {
        if(!rpg::validability(*i)) { intret(0); return; }
        int l = 1; int x = 0; rpg::getclientstats(l, x);
        int wd = 0; rpg_refreshcl();
        loopi(rpg::SLOT_COUNT) if(rpg_cleq[i] >= 0)
        {
            const rpg::itemdef &d = rpg::item(rpg_cleq[i]);
            if(d.type == rpg::ITEM_WEAPON) wd += d.damage;
        }
        intret(rpg::ability(*i).melee ? rpg::stat_powerstrike(l, wd) : rpg::stat_sparkbolt(l));
    });

    void switchname(const char *name)
    {
        filtertext(player1->name, name, NAMEALLOWSPACES, MAXNAMELEN);
        if(!player1->name[0]) copystring(player1->name, "unnamed");
        if (connectionState == CONNECTION_STATE_CONNECTED) addmsg(N_SWITCHNAME, "rcs", player1, player1->name);
    }
    void printname()
    {
        conoutf("your name is: %s", colorname(player1));
    }
    ICOMMAND(name, "sN", (char *s, int *numargs),
    {
        if(*numargs > 0) switchname(s);
        else if(!*numargs) printname();
        else result(colorname(player1));
    });
    ICOMMAND(getname, "", (), result(player1->name));

    void switchteam(const char *team)
    {
        if(player1->clientnum < 0) filtertext(player1->team, team, false, MAXTEAMLEN);
        else addmsg(N_SWITCHTEAM, "rs", team);
    }
    void printteam()
    {
        conoutf("your team is: %s", player1->team);
    }
    ICOMMAND(team, "sN", (char *s, int *numargs),
    {
        if(*numargs > 0) switchteam(s);
        else if(!*numargs) printteam();
        else result(player1->team);
    });
    ICOMMAND(getteam, "", (), result(player1->team));

    void sendmapinfo()
    {
        if(connectionState != CONNECTION_STATE_CONNECTED) { DEBUG_ERROR("Trying to send map info while not connected."); return; }
        sendcrc = true;
        if(player1->state!=CS_SPECTATOR || player1->privilege) senditemstoserver = true;
    }

    void writeclientinfo(stream *f)
    {
        f->printf("name %s\n", escapestring(player1->name));
    }

    bool allowedittoggle()
    {
        if(editmode) return true;
        if(isconnected() && multiplayer(false) && !m_edit)
        {
            conoutf(CON_ERROR, "editing in multiplayer requires coop edit mode (1)");
            return false;
        }
        if(identexists("allowedittoggle") && !execute("allowedittoggle"))
            return false;
        return true;
    }

    void edittoggled(bool on)
    {
        addmsg(N_EDITMODE, "ri", on ? 1 : 0);
        if(player1->state==CS_DEAD) deathstate(player1, true);
        else if(player1->state==CS_EDITING && player1->editstate==CS_DEAD) showscores(false);
        disablezoom();
        player1->suicided = player1->respawned = -2;
    }

    const char *getclientname(int cn)
    {
        fpsent *d = getclient(cn);
        return d ? d->name : "";
    }
    ICOMMAND(getclientname, "i", (int *cn), result(getclientname(*cn)));

    const char *getclientteam(int cn)
    {
        fpsent *d = getclient(cn);
        return d ? d->team : "";
    }
    ICOMMAND(getclientteam, "i", (int *cn), result(getclientteam(*cn)));

    int getclientmodel(int cn)
    {
        fpsent *d = getclient(cn);
        return d ? d->playermodel : -1;
    }
    ICOMMAND(getclientmodel, "i", (int *cn), intret(getclientmodel(*cn)));

    const char *getclienticon(int cn)
    {
        fpsent *d = getclient(cn);
        if(!d || d->state==CS_SPECTATOR) return "spectator";

        const playermodelinfo &mdl = getplayermodelinfo(d);

        const char *icon = isteam(player1->team, d->team) ? mdl.blueicon : mdl.redicon;
        return (!m_teammode || d->isInfected() || icon == NULL) ? mdl.ffaicon : icon;
    }
    ICOMMAND(getclienticon, "i", (int *cn), result(getclienticon(*cn)));

    bool ismaster(int cn)
    {
        fpsent *d = getclient(cn);
        return d && d->privilege >= PRIV_MASTER;
    }
    ICOMMAND(ismaster, "i", (int *cn), intret(ismaster(*cn) ? 1 : 0));

    bool isadmin(int cn)
    {
        fpsent *d = getclient(cn);
        return d && d->privilege >= PRIV_ADMIN;
    }
    ICOMMAND(isadmin, "i", (int *cn), intret(isadmin(*cn) ? 1 : 0));

    ICOMMAND(getmastermode, "", (), intret(mastermode));
    ICOMMAND(mastermodename, "i", (int *mm), result(server::mastermodename(*mm, "")));

    bool isspectator(int cn)
    {
        fpsent *d = getclient(cn);
        return d && d->state==CS_SPECTATOR;
    }
    ICOMMAND(isspectator, "i", (int *cn), intret(isspectator(*cn) ? 1 : 0));

    bool isai(int cn, int type)
    {
        fpsent *d = getclient(cn);
        int aitype = type > 0 && type < ai::AI_TYPE_NUM ? type : ai::AI_TYPE_BOT;
        return d && d->ai.type==aitype;
    }
    ICOMMAND(isai, "ii", (int *cn, int *type), intret(isai(*cn, *type) ? 1 : 0));

    int parseplayer(const char *arg)
    {
        char *end;
        int n = strtol(arg, &end, 10);
        if(*arg && !*end)
        {
            if(n!=player1->clientnum && !clients.inrange(n)) return -1;
            return n;
        }
        // try case sensitive first
        loopv(players)
        {
            fpsent *o = players[i];
            if(!strcmp(arg, o->name)) return o->clientnum;
        }
        // nothing found, try case insensitive
        loopv(players)
        {
            fpsent *o = players[i];
            if(!strcasecmp(arg, o->name)) return o->clientnum;
        }
        return -1;
    }
    ICOMMAND(getclientnum, "s", (char *name), intret(name[0] ? parseplayer(name) : player1->clientnum));

    void listclients(bool local, bool bots)
    {
        vector<char> buf;
        string cn;
        int numclients = 0;
        if(local && connectionState == CONNECTION_STATE_CONNECTED)
        {
            formatstring(cn)("%d", player1->clientnum);
            buf.put(cn, strlen(cn));
            numclients++;
        }
        loopv(clients) if(clients[i] && (bots || clients[i]->ai.type == ai::AI_TYPE_NONE))
        {
            formatstring(cn)("%d", clients[i]->clientnum);
            if(numclients++) buf.add(' ');
            buf.put(cn, strlen(cn));
        }
        buf.add('\0');
        result(buf.getbuf());
    }
    ICOMMAND(listclients, "bb", (int *local, int *bots), listclients(*local>0, *bots!=0));

    void clearbans()
    {
        addmsg(N_CLEARBANS, "r");
    }
    COMMAND(clearbans, "");

    void kick(const char *arg)
    {
        int i = parseplayer(arg);
        if(i>=0 && i!=player1->clientnum) addmsg(N_KICK, "ri", i);
    }
    COMMAND(kick, "s");

    void setteam(const char *arg1, const char *arg2)
    {
        int i = parseplayer(arg1);
        if(i>=0 && i!=player1->clientnum) addmsg(N_SETTEAM, "ris", i, arg2);
    }
    COMMAND(setteam, "ss");
    ICOMMAND(switchteamcn, "i", (int *cn), defformatstring(who)("%d", *cn); if (!strcmp(getclientteam(*cn), TEAM_0)) { setteam(who, TEAM_1); } else { setteam(who, TEAM_0); } );

    void hashpwd(const char *pwd)
    {
        if(player1->clientnum<0) return;
        string hash;
        server::hashpassword(player1->clientnum, sessionid, pwd, hash);
        result(hash);
    }
    COMMAND(hashpwd, "s");

    void setmaster(const char *arg, const char *who)
    {
        if(!arg[0]) return;
        int val = 1, target = -1;
        if(who[0])
        {
            target = parseplayer(who);
            if(target < 0) return;
        }
        string hash = "";
        if(!arg[1] && isdigit(arg[0])) val = parseint(arg);
        else
        {
            if(target != player1->clientnum && target >= 0) return;
            server::hashpassword(player1->clientnum, sessionid, arg, hash);
        }
        addmsg(N_SETMASTER, "riis", target == -1 ? player1->clientnum : target, val, hash);
    }
    COMMAND(setmaster, "ss");
    ICOMMAND(mastermode, "i", (int *val), addmsg(N_MASTERMODE, "ri", *val));

    void togglespectator(int val, const char *who)
    {
        int i = who[0] ? parseplayer(who) : player1->clientnum;
        if(i>=0) addmsg(N_SPECTATOR, "rii", i, val);
    }
    ICOMMAND(spectator, "is", (int *val, char *who), togglespectator(*val, who));

    ICOMMAND(checkmaps, "", (), addmsg(N_CHECKMAPS, "r"));

    VARP(localmode, MIN_GAMEMODE, DEFAULT_MODE, MAX_GAMEMODE);
    SVARP(localmap, DEFAULT_MAP);

    VARP(lobbymode, MIN_GAMEMODE, DEFAULT_MODE, MAX_GAMEMODE);
    SVARP(lobbymap, DEFAULT_MAP);

    int gamemode = INT_MAX, nextmode = INT_MAX;
    string clientmap = "";

    void changemapserv(const char *name, int mode)        // forced map change from the server
    {
        if(multiplayer(false) && !m_mp(mode))
        {
            conoutf(CON_ERROR, "mode %s (%d) not supported in multiplayer", server::modename(gamemode), gamemode);
            RR_FOREACH_GAMEMODE
            {
                if(m_mp(i))
                {
                    mode = gameModes[i].id;
                    break;
                }
            }
        }

        gamemode = mode;
        nextmode = mode;
        if(editmode) toggleedit();
        if(m_demo) { entities::resetspawns(); return; }
        if((m_edit && !name[0]) || !load_world(name))
        {
            emptymap(0, true, name);
            senditemstoserver = false;
        }
        startgame();
    }

    int getmode() { return gamemode; };
    void setmode(int mode)
    {
        if(multiplayer(false) && !m_mp(mode))
        {
            conoutf(CON_ERROR, "mode %s (%d) not supported in multiplayer",  server::modename(mode), mode);
            intret(0);
            return;
        }
        nextmode = mode;
        intret(1);
    }
    ICOMMAND(mode, "i", (int *val), setmode(*val));
    ICOMMAND(getmode, "", (), intret(gamemode));
    ICOMMAND(getmodename, "i", (int *i), result(server::modename(*i)));
    ICOMMAND(timeremaining, "i", (int *formatted),
    {
        int val = max(maplimit - lastmillis, 0)/1000;
        if(*formatted)
        {
            defformatstring(str)("%d:%02d", val/60, val%60);
            result(str);
        }
        else intret(val);
    });
    ICOMMANDS("m_noitems", "i", (int *mode), { int gamemode = *mode; intret(m_noitems); });
    ICOMMANDS("m_noammo", "i", (int *mode), { int gamemode = *mode; intret(m_noammo); });
    ICOMMANDS("m_insta", "i", (int *mode), { int gamemode = *mode; intret(m_insta); });
    ICOMMANDS("m_tactics", "i", (int *mode), { int gamemode = *mode; intret(m_tactics); });
    ICOMMANDS("m_efficiency", "i", (int *mode), { int gamemode = *mode; intret(m_efficiency); });
    ICOMMANDS("m_capture", "i", (int *mode), { int gamemode = *mode; intret(m_capture); });
    ICOMMANDS("m_regencapture", "i", (int *mode), { int gamemode = *mode; intret(m_regencapture); });
    ICOMMANDS("m_ctf", "i", (int *mode), { int gamemode = *mode; intret(m_ctf); });
    ICOMMANDS("m_protect", "i", (int *mode), { int gamemode = *mode; intret(m_protect); });
    ICOMMANDS("m_hold", "i", (int *mode), { int gamemode = *mode; intret(m_hold); });
    ICOMMANDS("m_infection", "i", (int *mode), { int gamemode = *mode; intret(m_infection); });
    ICOMMANDS("m_juggernaut", "i", (int *mode), { int gamemode = *mode; intret(m_juggernaut); });
    ICOMMANDS("m_teammode", "i", (int *mode), { int gamemode = *mode; intret(m_teammode); });
    ICOMMANDS("m_demo", "i", (int *mode), { int gamemode = *mode; intret(m_demo); });
    ICOMMANDS("m_edit", "i", (int *mode), { int gamemode = *mode; intret(m_edit); });
    ICOMMANDS("m_lobby", "i", (int *mode), { int gamemode = *mode; intret(m_lobby); });
    ICOMMANDS("m_sp", "i", (int *mode), { int gamemode = *mode; intret(m_sp); });
    ICOMMANDS("m_dmsp", "i", (int *mode), { int gamemode = *mode; intret(m_dmsp); });
    ICOMMANDS("m_classicsp", "i", (int *mode), { int gamemode = *mode; intret(m_classicsp); });
    ICOMMANDS("m_survival", "i", (int *mode), { int gamemode = *mode; intret(m_survival); });
    ICOMMANDS("m_survivalb", "i", (int *mode), { int gamemode = *mode; intret(m_survivalb); });
    ICOMMANDS("m_oneteam", "i", (int *mode), { int gamemode = *mode; intret(m_oneteam); });

    extern int sortvotes;
    struct mapvote
    {
        vector<fpsent *> players;
        string map;
        int millis, mode, muts;

        mapvote() {}
        ~mapvote() { players.shrink(0); }

        static bool compare(const mapvote &a, const mapvote &b)
        {
            if(sortvotes)
            {
                return a.players.length() < b.players.length();
            }
            else
            {
                return a.millis < b.millis;
            }
        }
    };
    vector<mapvote> mapvotes;

    VARFP(sortvotes, 0, 0, 1, mapvotes.sort(mapvote::compare));
    VARFP(cleanvotes, 0, 0, 1, {
        if(cleanvotes && !mapvotes.empty()) loopvrev(mapvotes) if(mapvotes[i].players.empty()) mapvotes.remove(i);
    });

    void clearvotes()
    {
        mapvotes.shrink(0);
    }
    
    void clearvotes(fpsent *d, bool msg)
    {
        int found = 0;
        loopvrev(mapvotes) if(mapvotes[i].players.find(d) >= 0)
        {
            found++;
            mapvotes[i].players.removeobj(d);
            if(cleanvotes && mapvotes[i].players.empty()) mapvotes.remove(i);
        }
        if(found)
        {
            if(!mapvotes.empty()) mapvotes.sort(mapvote::compare);
            if(msg && d == game::player1) conoutf(CON_INFO, "%s cleared their previous vote", game::colorname(d));
        }
        if(d == player1)
        {
            addmsg(N_CLEARVOTE, "r");
        }
    }

    void vote(fpsent *d, const char *text, int mode)
    {
        mapvote *m = NULL;
        if(!mapvotes.empty()) loopvrev(mapvotes)
        {
            if(mapvotes[i].players.find(d) >= 0)
            {
                if(!strcmp(text, mapvotes[i].map) && mode == mapvotes[i].mode) return;
                mapvotes[i].players.removeobj(d);
                if(cleanvotes && mapvotes[i].players.empty()) mapvotes.remove(i);
            }
            if(!strcmp(text, mapvotes[i].map) && mode == mapvotes[i].mode) m = &mapvotes[i];
        }
        if(!m)
        {
            m = &mapvotes.add();
            copystring(m->map, text);
            m->mode = mode;
            m->millis = totalmillis;
        }
        m->players.add(d);
        mapvotes.sort(mapvote::compare);
    }

    void getvotes(int vote, int prop, int idx)
    {
        if(vote < 0) intret(mapvotes.length());
        else if(mapvotes.inrange(vote))
        {
            mapvote &v = mapvotes[vote];
            if(prop < 0) intret(4);
            else switch(prop)
            {
                case 0:
                    if(idx < 0) intret(v.players.length());
                    else if(v.players.inrange(idx)) intret(v.players[idx]->clientnum);
                    break;
                case 1: intret(v.mode); break;
                case 2: result(v.map); break;
            }
        }
    }
    
    ICOMMAND(clearvote, "", (), {
        clearvotes(player1, true);
    });
    ICOMMAND(getvote, "iiiN", (int *vote, int *prop, int *idx, int *numargs), {
        getvotes(*numargs >= 1 ? *vote : -1, *numargs >= 2 ? *prop : -1, *numargs >= 3 ? *idx : -1);
    });

    void changemap(const char *name, int mode) // request map change, server may ignore
    {
        if(m_checknot(mode, M_EDIT) && !name[0])
            name = clientmap[0] ? clientmap : (remote ? lobbymap : localmap);
        if(!remote)
        {
            server::forcemap(name, mode);
            if(!isconnected()) localconnect();
        }
        else if(player1->state!=CS_SPECTATOR || player1->privilege >= PRIV_MASTER) addmsg(N_MAPVOTE, "rsi", name, mode);
    }
    void changemap(const char *name)
    {
        changemap(name, m_valid(nextmode) ? nextmode : (remote ? lobbymode : localmode));
    }

    ICOMMAND(map, "s", (char *name), changemap(name));

    void forceedit(const char *name)
    {
        changemap(name, 1);
    }

    void newmap(int size)
    {
        addmsg(N_NEWMAP, "ri", size);
    }

    int needclipboard = -1;

    void sendclipboard()
    {
        uchar *outbuf = NULL;
        int inlen = 0, outlen = 0;
        if(!packeditinfo(localedit, inlen, outbuf, outlen))
        {
            outbuf = NULL;
            inlen = outlen = 0;
        }
        packetbuf p(16 + outlen, ENET_PACKET_FLAG_RELIABLE);
        putint(p, N_CLIPBOARD);
        putint(p, inlen);
        putint(p, outlen);
        if(outlen > 0) p.put(outbuf, outlen);
        sendclientpacket(p.finalize(), 1);
        needclipboard = -1;
    }

    void edittrigger(const selinfo &sel, int op, int arg1, int arg2, int arg3)
    {
        if(m_edit) switch(op)
        {
            case EDIT_FLIP:
            case EDIT_COPY:
            case EDIT_PASTE:
            case EDIT_DELCUBE:
            {
                switch(op)
                {
                    case EDIT_COPY: needclipboard = 0; break;
                    case EDIT_PASTE:
                        if(needclipboard > 0)
                        {
                            c2sinfo(true);
                            sendclipboard();
                        }
                        break;
                }
                addmsg(N_EDITF + op, "ri9i4",
                   sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                   sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner);
                break;
            }
            case EDIT_ROTATE:
            {
                addmsg(N_EDITF + op, "ri9i5",
                   sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                   sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                   arg1);
                break;
            }
            case EDIT_MAT:
            case EDIT_FACE:
            case EDIT_TEX:
            {
                addmsg(N_EDITF + op, "ri9i6",
                   sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                   sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                   arg1, arg2);
                break;
            }
            case EDIT_REPLACE:
            {
                addmsg(N_EDITF + op, "ri9i7",
                   sel.o.x, sel.o.y, sel.o.z, sel.s.x, sel.s.y, sel.s.z, sel.grid, sel.orient,
                   sel.cx, sel.cxs, sel.cy, sel.cys, sel.corner,
                   arg1, arg2, arg3);
                break;
            }
            case EDIT_REMIP:
            {
                addmsg(N_EDITF + op, "r");
                break;
            }
        }
    }

    void printvar(fpsent *d, ident *id)
    {
        if(id) switch(id->type)
        {
            case ID_VAR:
            {
                int val = *id->storage.i;
                string str;
                if(val < 0)
                    formatstring(str)("%d", val); 
                else if(id->flags&IDF_HEX && id->maxval==0xFFFFFF)
                    formatstring(str)("0x%.6X (%d, %d, %d)", val, (val>>16)&0xFF, (val>>8)&0xFF, val&0xFF);
                else
                    formatstring(str)(id->flags&IDF_HEX ? "0x%X" : "%d", val);
                conoutf("%s set map var \"%s\" to %s", colorname(d), id->name, str);
                break;
            }
            case ID_FVAR:
                conoutf("%s set map var \"%s\" to %s", colorname(d), id->name, floatstr(*id->storage.f));
                break;
            case ID_SVAR:
                conoutf("%s set map var \"%s\" to \"%s\"", colorname(d), id->name, *id->storage.s);
                break;
        }
    }

    void vartrigger(ident *id)
    {
        if(!m_edit) return;
        switch(id->type)
        {
            case ID_VAR:
                addmsg(N_EDITVAR, "risi", ID_VAR, id->name, *id->storage.i);
                break;

            case ID_FVAR:
                addmsg(N_EDITVAR, "risf", ID_FVAR, id->name, *id->storage.f);
                break;

            case ID_SVAR:
                addmsg(N_EDITVAR, "riss", ID_SVAR, id->name, *id->storage.s);
                break;
            default: return;
        }
        printvar(player1, id);
    }

    void pausegame(bool val)
    {
        if(connectionState != CONNECTION_STATE_CONNECTED) return;
        if(!remote) server::forcepaused(val);
        else addmsg(N_PAUSEGAME, "ri", val ? 1 : 0);
    }
    ICOMMAND(pausegame, "i", (int *val), pausegame(*val > 0));
    ICOMMAND(paused, "iN$", (int *val, int *numargs, ident *id),
    { 
        if(*numargs > 0) pausegame(clampvar(id, *val, 0, 1) > 0); 
        else if(*numargs < 0) intret(gamepaused ? 1 : 0);
        else printvar(id, gamepaused ? 1 : 0); 
    });

    bool ispaused() { return gamepaused; }

    bool allowmouselook()
    {
        // RPG: on RPG maps the mouse is a free cursor, not a camera control.
        // FPS/battleground maps are untouched.
        return (!gamepaused || !remote || m_edit) && !rpg::mapactive(getclientmap());
    }

    // RPG: raw mouse deltas for the free cursor. The engine calls this even
    // when mouse-look is gated off (see main.cpp event loop). Menus consume
    // deltas first via g3d_movecursor, so the world cursor freezes while a
    // menu is open and resumes when it closes.
    void cursormove(int dx, int dy)
    {
        if(rpg::mapactive(getclientmap())) rpg::cursoraccum(dx, dy);
    }

    void changegamespeed(int val)
    {
        if(connectionState != CONNECTION_STATE_CONNECTED) return;
        if(!remote) server::forcegamespeed(val);
        else addmsg(N_GAMESPEED, "ri", val);
    }
    ICOMMAND(gamespeed, "iN$", (int *val, int *numargs, ident *id),
    {
        if(*numargs > 0) changegamespeed(clampvar(id, *val, 10, 1000));
        else if(*numargs < 0) intret(gamespeed);
        else printvar(id, gamespeed);
    });

    int scaletime(int t) { return t*gamespeed; }

    // collect c2s messages conveniently
    vector<uchar> messages;
    int messagecn = -1, messagereliable = false;

    void addmsg(int type, const char *fmt, ...)
    {
        if(connectionState == CONNECTION_STATE_NONE) { DEBUG_ERROR("premature message: %i(%s)", type, fmt); return;}
        static uchar buf[MAXTRANS];
        ucharbuf p(buf, sizeof(buf));
        putint(p, type);
        int numi = 1, numf = 0, nums = 0, mcn = -1;
        bool reliable = false;
        if(fmt)
        {
            va_list args;
            va_start(args, fmt);
            while(*fmt) switch(*fmt++)
            {
                case 'r': reliable = true; break;
                case 'c':
                {
                    fpsent *d = va_arg(args, fpsent *);
                    mcn = !d || d == player1 ? -1 : d->clientnum;
                    break;
                }
                case 'v':
                {
                    int n = va_arg(args, int);
                    int *v = va_arg(args, int *);
                    loopi(n) putint(p, v[i]);
                    numi += n;
                    break;
                }

                case 'i':
                {
                    int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                    loopi(n) putint(p, va_arg(args, int));
                    numi += n;
                    break;
                }
                case 'f':
                {
                    int n = isdigit(*fmt) ? *fmt++-'0' : 1;
                    loopi(n) putfloat(p, (float)va_arg(args, double));
                    numf += n;
                    break;
                }
                case 's': sendstring(va_arg(args, const char *), p); nums++; break;
                case 'm':
                {
                    int n = va_arg(args, int);
                    if(*fmt == '+') putint(p, n);
                    p.put(va_arg(args, uchar *), n);
                    break;
                }
            }
            va_end(args);
        }
        int num = nums || numf ? 0 : numi, msgsize = server::msgsizelookup(type);
        if(msgsize && num!=msgsize) { fatal("inconsistent msg size for %d (is: %d != should be:%d)", type, num, msgsize); }
        if(reliable) messagereliable = true;
        if(mcn != messagecn)
        {
            static uchar mbuf[16];
            ucharbuf m(mbuf, sizeof(mbuf));
            putint(m, N_FROMAI);
            putint(m, mcn);
            messages.put(mbuf, m.length());
            messagecn = mcn;
        }
        messages.put(buf, p.length());
    }

    void connectattempt(const char *name, const char *password, const ENetAddress &address)
    {
        copystring(connectpass, password);
    }

    void connectfail()
    {
        memset(connectpass, 0, sizeof(connectpass));
    }

    void gameconnect(bool _remote)
    {
        remote = _remote;
        if(editmode) toggleedit();
        auth::init();
    }

    void gamedisconnect(bool cleanup)
    {
        if(remote) stopfollowing();
        connectionState = CONNECTION_STATE_NONE;
        remote = false;
        player1->clientnum = -1;
        sessionid = 0;
        mastermode = MM_OPEN;
        messages.setsize(0);
        messagereliable = false;
        messagecn = -1;
        player1->respawn(gamemode);
        player1->lifesequence = 0;
        player1->state = CS_ALIVE;
        player1->privilege = PRIV_NONE;
        sendcrc = senditemstoserver = false;
        demoplayback = false;
        gamepaused = false;
        gamespeed = 100;
        clearclients(false);
        if(cleanup)
        {
            nextmode = gamemode = INT_MAX;
            clientmap[0] = '\0';
        }
    }

    void toserver(char *text) { conoutf(CON_CHAT, "%s:\f0 %s", colorname(player1), text); addmsg(N_TEXT, "rcs", player1, text); }
    COMMANDN(say, toserver, "C");

    void sayteam(char *text) { conoutf(CON_TEAMCHAT, "%s:\f1 %s", colorname(player1), text); addmsg(N_SAYTEAM, "rcs", player1, text); }
    COMMAND(sayteam, "C");

    void sendposition(fpsent *d, packetbuf &q)
    {
        putint(q, N_POS);
        putuint(q, d->clientnum);
        // 3 bits phys state, 1 bit life sequence, 2 bits move, 2 bits strafe
        uchar physstate = d->physstate | ((d->lifesequence&1)<<3) | ((d->move&3)<<4) | ((d->strafe&3)<<6);
        q.put(physstate);
        ivec o = ivec(vec(d->o.x, d->o.y, d->o.z-d->eyeheight).mul(DMF));
        uint vel = min(int(d->vel.magnitude()*DVELF), 0xFFFF), fall = min(int(d->falling.magnitude()*DVELF), 0xFFFF);
        // 3 bits position, 1 bit velocity, 3 bits falling, 1 bit material
        uint flags = 0;
        if(o.x < 0 || o.x > 0xFFFF) flags |= 1<<0;
        if(o.y < 0 || o.y > 0xFFFF) flags |= 1<<1;
        if(o.z < 0 || o.z > 0xFFFF) flags |= 1<<2;
        if(vel > 0xFF) flags |= 1<<3;
        if(fall > 0)
        {
            flags |= 1<<4;
            if(fall > 0xFF) flags |= 1<<5;
            if(d->falling.x || d->falling.y || d->falling.z > 0) flags |= 1<<6;
        }
        if((lookupmaterial(d->feetpos())&MATF_CLIP) == MAT_GAMECLIP) flags |= 1<<7;
        putuint(q, flags);
        loopk(3)
        {
            q.put(o[k]&0xFF);
            q.put((o[k]>>8)&0xFF);
            if(o[k] < 0 || o[k] > 0xFFFF) q.put((o[k]>>16)&0xFF);
        }
        uint dir = (d->yaw < 0 ? 360 + int(d->yaw)%360 : int(d->yaw)%360) + clamp(int(d->pitch+90), 0, 180)*360;
        q.put(dir&0xFF);
        q.put((dir>>8)&0xFF);
        q.put(clamp(int(d->roll+90), 0, 180));
        q.put(vel&0xFF);
        if(vel > 0xFF) q.put((vel>>8)&0xFF);
        float velyaw, velpitch;
        vectoyawpitch(d->vel, velyaw, velpitch);
        uint veldir = (velyaw < 0 ? 360 + int(velyaw)%360 : int(velyaw)%360) + clamp(int(velpitch+90), 0, 180)*360;
        q.put(veldir&0xFF);
        q.put((veldir>>8)&0xFF);
        if(fall > 0)
        {
            q.put(fall&0xFF);
            if(fall > 0xFF) q.put((fall>>8)&0xFF);
            if(d->falling.x || d->falling.y || d->falling.z > 0)
            {
                float fallyaw, fallpitch;
                vectoyawpitch(d->falling, fallyaw, fallpitch);
                uint falldir = (fallyaw < 0 ? 360 + int(fallyaw)%360 : int(fallyaw)%360) + clamp(int(fallpitch+90), 0, 180)*360;
                q.put(falldir&0xFF);
                q.put((falldir>>8)&0xFF);
            }
        }
    }

    void sendposition(fpsent *d, bool reliable)
    {
        if(d->state != CS_ALIVE && d->state != CS_EDITING) return;
        packetbuf q(100, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
        sendposition(d, q);
        sendclientpacket(q.finalize(), 0);
    }

    void sendpositions()
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if((d == player1 || d->ai.local) && (d->state == CS_ALIVE || d->state == CS_EDITING))
            {
                packetbuf q(100);
                sendposition(d, q);
                for(int j = i+1; j < players.length(); j++)
                {
                    fpsent *d = players[j];
                    if((d == player1 || d->ai.local) && (d->state == CS_ALIVE || d->state == CS_EDITING))
                        sendposition(d, q);
                }
                sendclientpacket(q.finalize(), 0);
                break;
            }
        }
    }

    void sendmessages()
    {
        packetbuf p(MAXTRANS);
        if(sendcrc)
        {
            p.reliable();
            sendcrc = false;
            const char *mname = getclientmap();
            putint(p, N_MAPCRC);
            sendstring(mname, p);
            putint(p, mname[0] ? getmapcrc() : 0);
            conoutf("Server requests map integrity check.");
        }
        if(senditemstoserver)
        {
            if(!m_noitems || cmode!=NULL) p.reliable();
            if(!m_noitems) entities::putitems(p);
            if(cmode) cmode->senditems(p);
            senditemstoserver = false;
            conoutf("Server requests our items.");
        }
        if(messages.length())
        {
            p.put(messages.getbuf(), messages.length());
            messages.setsize(0);
            if(messagereliable) p.reliable();
            messagereliable = false;
            messagecn = -1;
        }
        if(totalmillis-lastping>250)
        {
            putint(p, N_PING);
            putint(p, totalmillis);
            lastping = totalmillis;
        }

        if(p.length())
        {
            sendclientpacket(p.finalize(), 1);
        }
    }

    void c2sinfo(bool force) // send update to the server
    {
        static int lastupdate = -1000;
        if(totalmillis - lastupdate < 33 && !force) return; // don't update faster than 30fps
        lastupdate = totalmillis;
        sendpositions();
        sendmessages();
        flushclient();
    }

    void sendintro()
    {
        packetbuf p(MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
        putint(p, N_CONNECT);
        sendstring(player1->name, p);
        string hash = "";
        if(connectpass[0])
        {
            server::hashpassword(player1->clientnum, sessionid, connectpass, hash);
            memset(connectpass, 0, sizeof(connectpass));
        }
        putint(p, player1->playerclass = playerclass);
        putint(p, player1->playermodel = playermodel);
        sendstring(hash, p);
        sendclientpacket(p.finalize(), 1);
    }

    void updatepos(fpsent *d)
    {
        // update the position of other clients in the game in our world
        // don't care if he's in the scenery or other players,
        // just don't overlap with our client

        const float r = player1->radius+d->radius;
        const float dx = player1->o.x-d->o.x;
        const float dy = player1->o.y-d->o.y;
        const float dz = player1->o.z-d->o.z;
        const float rz = player1->aboveeye+d->eyeheight;
        const float fx = (float)fabs(dx), fy = (float)fabs(dy), fz = (float)fabs(dz);
        if(fx<r && fy<r && fz<rz && player1->state!=CS_SPECTATOR && d->state!=CS_DEAD)
        {
            if(fx<fy) d->o.y += dy<0 ? r-fy : -(r-fy);  // push aside
            else      d->o.x += dx<0 ? r-fx : -(r-fx);
        }
        int lagtime = totalmillis-d->lastupdate;
        if(lagtime)
        {
            if(d->state!=CS_SPAWNING && d->lastupdate) d->plag = (d->plag*5+lagtime)/6;
            d->lastupdate = totalmillis;
        }
    }

    void parsepositions(ucharbuf &p)
    {
        int type;
        while(p.remaining()) switch(type = getint(p))
        {
            case N_DEMOPACKET: break;
            case N_POS:                        // position of another client
            {
                int cn = getuint(p), physstate = p.get(), flags = getuint(p);
                vec o, vel, falling;
                float yaw, pitch, roll;
                loopk(3)
                {
                    int n = p.get(); n |= p.get()<<8; if(flags&(1<<k)) { n |= p.get()<<16; if(n&0x800000) n |= -1<<24; }
                    o[k] = n/DMF;
                }
                int dir = p.get(); dir |= p.get()<<8;
                yaw = dir%360;
                pitch = clamp(dir/360, 0, 180)-90;
                roll = clamp(int(p.get()), 0, 180)-90;
                int mag = p.get(); if(flags&(1<<3)) mag |= p.get()<<8;
                dir = p.get(); dir |= p.get()<<8;
                vecfromyawpitch(dir%360, clamp(dir/360, 0, 180)-90, 1, 0, vel);
                vel.mul(mag/DVELF);
                if(flags&(1<<4))
                {
                    mag = p.get(); if(flags&(1<<5)) mag |= p.get()<<8;
                    if(flags&(1<<6))
                    {
                        dir = p.get(); dir |= p.get()<<8;
                        vecfromyawpitch(dir%360, clamp(dir/360, 0, 180)-90, 1, 0, falling);
                    }
                    else falling = vec(0, 0, -1);
                    falling.mul(mag/DVELF);
                }
                else falling = vec(0, 0, 0);
                int seqcolor = (physstate>>3)&1;
                fpsent *d = getclient(cn);
                if(!d || d->lifesequence < 0 || seqcolor!=(d->lifesequence&1) || d->state==CS_DEAD) continue;
                float oldyaw = d->yaw, oldpitch = d->pitch, oldroll = d->roll;
                d->yaw = yaw;
                d->pitch = pitch;
                d->roll = roll;
                d->move = (physstate>>4)&2 ? -1 : (physstate>>4)&1;
                d->strafe = (physstate>>6)&2 ? -1 : (physstate>>6)&1;
                vec oldpos(d->o);
                if(allowmove(d))
                {
                    d->o = o;
                    d->o.z += d->eyeheight;
                    d->vel = vel;
                    d->falling = falling;
                    d->physstate = physstate&7;
                }
                updatephysstate(d);
                updatepos(d);
                if(smoothmove && d->smoothmillis>=0 && oldpos.dist(d->o) < smoothdist)
                {
                    d->newpos = d->o;
                    d->newyaw = d->yaw;
                    d->newpitch = d->pitch;
                    d->newroll = d->roll;
                    d->o = oldpos;
                    d->yaw = oldyaw;
                    d->pitch = oldpitch;
                    d->roll = oldroll;
                    (d->deltapos = oldpos).sub(d->newpos);
                    d->deltayaw = oldyaw - d->newyaw;
                    if(d->deltayaw > 180) d->deltayaw -= 360;
                    else if(d->deltayaw < -180) d->deltayaw += 360;
                    d->deltapitch = oldpitch - d->newpitch;
                    d->deltaroll = oldroll - d->newroll;
                    d->smoothmillis = lastmillis;
                }
                else d->smoothmillis = 0;
                if(d->state==CS_LAGGED || d->state==CS_SPAWNING) d->state = CS_ALIVE;
                break;
            }

            case N_TELEPORT:
            {
                int cn = getint(p), tp = getint(p), td = getint(p);
                fpsent *d = getclient(cn);
                if(!d || d->lifesequence < 0 || d->state==CS_DEAD) continue;
                entities::teleporteffects(d, tp, td, false);
                break;
            }

            case N_JUMPPAD:
            {
                int cn = getint(p), jp = getint(p);
                fpsent *d = getclient(cn);
                if(!d || d->lifesequence < 0 || d->state==CS_DEAD) continue;
                entities::jumppadeffects(d, jp, false);
                break;
            }

            default:
                neterr("type");
                return;
        }
    }

    void parsestate(fpsent *d, ucharbuf &p, bool resume = false)
    {
        if(!d) { static fpsent dummy; d = &dummy; }
        if(resume)
        {
            if(d==player1) getint(p);
            else d->state = getint(p);
            d->frags = getint(p);
            d->flags = getint(p);
            if(d==player1) getint(p);
            else d->quadmillis = getint(p);
        }
        d->lifesequence = getint(p);
        d->guts = getint(p);
        d->health = getint(p);
        d->maxhealth = getint(p);
        d->armour = getint(p);
        d->armourtype = getint(p);
        int i;
        d->playerclass = (i = getint(p)) == -1 ? d->playerclass : i;
        d->playermodel = (i = getint(p)) == -1 ? d->playermodel : i;
        if(resume && d==player1)
        {
            getint(p);
            loopi(NUMWEAPS) getint(p);
        }
        else
        {
            d->gunselect = getint(p); //TODO: clamp
            loopi(NUMWEAPS) d->ammo[i] = getint(p);
        }
    }

    extern int deathscore;

    void parsemessages(int cn, fpsent *d, ucharbuf &p)
    {
        static char text[MAXTRANS];
        int type;
        bool mapchanged = false, initmap = false, demopacket = false;

        while(p.remaining()) switch(type = getint(p))
        {
            case N_DEMOPACKET: demopacket = true; break;
            case N_SERVINFO:                   // welcome messsage from the server
            {
                int mycn = getint(p), prot = getint(p);
                if(prot!=PROTOCOL_VERSION)
                {
                    conoutf(CON_ERROR, "you are using a different game protocol (you: %d, server: %d)", PROTOCOL_VERSION, prot);
                    if (PROTOCOL_VERSION < prot) conoutf(CON_INFO, "\fbplease download the latest Revelade Revolution release from http://sourceforge.net/projects/rrevolution");
                    disconnect();
                    return;
                }
                sessionid = getint(p);
                player1->clientnum = mycn;      // we are now connected
                int authMode = getint(p);
                if(authMode & (1 << 0)) conoutf("this server is password protected");
                if(authMode & (1 << 1))
                {
                    //We'll be waiting for the server's certificate
                    conoutf("this server uses certificate based authentication");
                    connectionState = CONNECTION_STATE_SERVER_HELLO;
                }
                getstring(text, p);
                copystring(servinfo, text);
                sendintro();
                break;
            }

            case N_WELCOME:
            {
                allowedweaps = getint(p);
                connectionState = CONNECTION_STATE_CONNECTED;
                break;
            }

            case N_PAUSEGAME:
            {
                bool val = getint(p) > 0;
                int cn = getint(p);
                fpsent *a = cn >= 0 ? getclient(cn) : NULL;
                if(!demopacket)
                {
                    gamepaused = val;
                    player1->attacking = false;
                }
                if(a) conoutf("%s %s the game", colorname(a), val ? "paused" : "resumed"); 
                else conoutf("game is %s", val ? "paused" : "resumed");
                if(identexists("map_paused_cutscene")) execute("map_paused_cutscene");
                else if(identexists("paused_cutscene")) execute("paused_cutscene");
                break;
            }

            case N_GAMESPEED:
            {
                int val = clamp(getint(p), 10, 1000), cn = getint(p);
                fpsent *a = cn >= 0 ? getclient(cn) : NULL;
                if(!demopacket) gamespeed = val;
                extern int slowmosp;
                if(m_sp && slowmosp) break;
                if(a) conoutf("%s set gamespeed to %d", colorname(a), val);
                else conoutf("gamespeed is %d", val);
                break;
            }

            case N_CLIENT:
            {
                int cn = getint(p), len = getuint(p);
                ucharbuf q = p.subbuf(len);
                parsemessages(cn, getclient(cn), q);
                break;
            }

            case N_SOUND:
                if(!d) return;
                playsound(getint(p), &d->o);
                break;

            case N_TEXT:
            {
                if(!d) return;
                getstring(text, p);
                filtertext(text, text);
                if(d->state!=CS_DEAD && d->state!=CS_SPECTATOR)
                    particle_textcopy(d->abovehead(), text, PART_TEXT, 2000, 0x32FF64, 4.0f, -8);
                conoutf(CON_CHAT, "%s:\f0 %s", colorname(d), text);
                break;
            }

            case N_SAYTEAM:
            {
                int tcn = getint(p);
                fpsent *t = getclient(tcn);
                getstring(text, p);
                filtertext(text, text);
                if(!t) break;
                if(t->state!=CS_DEAD && t->state!=CS_SPECTATOR)
                    particle_textcopy(t->abovehead(), text, PART_TEXT, 2000, 0x6496FF, 4.0f, -8);
                conoutf(CON_TEAMCHAT, "%s:\f1 %s", colorname(t), text);
                break;
            }

            case N_MAPCHANGE:
                getstring(text, p);
                changemapserv(text, getint(p));
                mapchanged = true;
                if(getint(p)) entities::spawnitems();
                else
                {
                    conoutf(CON_DEBUG, "Server has map items.");
                    senditemstoserver = false;
                }
                clearvotes();
                break;

            case N_FORCEDEATH:
            {
                int cn = getint(p);
                fpsent *d = cn==player1->clientnum ? player1 : newclient(cn);
                if(!d) break;
                if(d==player1)
                {
                    if(editmode) toggleedit();
                    stopfollowing();
                    if(deathscore) showscores(true);
                }
                else d->resetinterp();
                d->state = CS_DEAD;
                break;
            }

            case N_ITEMLIST:
            {
                int n;
                while((n = getint(p))>=0 && !p.overread())
                {
                    if(mapchanged) entities::setspawn(n, true);
                    getint(p); // type
                }
                break;
            }

            case N_MAPRELOAD:          // server requests next map
            {
                defformatstring(nextmapalias)("nextmap_%s%s", (cmode ? cmode->prefixnextmap() : ""), getclientmap());
                const char *map = getalias(nextmapalias);     // look up map in the cycle
                addmsg(N_MAPCHANGE, "rsi", *map ? map : getclientmap(), nextmode);
                break;
            }

            case N_INITCLIENT:            // another client either connected or changed name/team
            {
                int cn = getint(p);
                fpsent *d = newclient(cn);
                if(!d)
                {
                    getstring(text, p);
                    getstring(text, p);
                    getint(p);
                    break;
                }
                getstring(text, p);
                filtertext(text, text, NAMEALLOWSPACES, MAXNAMELEN);
                if(!text[0]) copystring(text, "unnamed");
                if(d->name[0])          // already connected
                {
                    if(strcmp(d->name, text))
                        conoutf("%s is now known as %s", colorname(d), colorname(d, text));
                }
                else                    // new client
                {
                    conoutf("connected: %s", colorname(d, text));
                    if(needclipboard >= 0) needclipboard++;
                }
                copystring(d->name, text, MAXNAMELEN+1);
                getstring(text, p);
                filtertext(d->team, text, false, MAXTEAMLEN);

                d->playerclass = getint(p);
                d->playermodel = getint(p);
                break;
            }

            case N_SWITCHNAME:
                getstring(text, p);
                if(d)
                {
                    filtertext(text, text, NAMEALLOWSPACES, MAXNAMELEN);
                    if(!text[0]) copystring(text, "unnamed");
                    if(strcmp(text, d->name))
                    {
                        conoutf("%s is now known as %s", colorname(d), colorname(d, text));
                        copystring(d->name, text, MAXNAMELEN+1);
                    }
                }
                break;

            case N_CDIS:
                clientdisconnected(getint(p));
                break;

            case N_SPAWN:
            {
                if(d)
                {
                    if(d->state==CS_DEAD && d->lastpain) saveragdoll(d);
                    d->respawn(gamemode);
                }
                parsestate(d, p);
                if(!d) break;
                d->state = CS_SPAWNING;
                if(player1->state==CS_SPECTATOR && following==d->clientnum)
                    lasthit = 0;
                break;
            }

            case N_SPAWNSTATE:
            {
                int scn = getint(p);
                fpsent *s = getclient(scn);
                if(!s) { parsestate(NULL, p); break; }
                if(s->state==CS_DEAD && s->lastpain) saveragdoll(s);
                if(s==player1)
                {
                    if(editmode) toggleedit();
                    stopfollowing();
                }
                s->respawn(gamemode);
                parsestate(s, p);
                s->state = CS_ALIVE;
                if(cmode) cmode->pickspawn(s);
                else findplayerspawn(s);
                if(s == player1)
                {
                    showscores(false);
                    lasthit = 0;
                }
                if(cmode) cmode->respawned(s);
                ai::spawned(s);
                addmsg(N_SPAWN, "rcii", s, s->lifesequence, s->gunselect);
                break;
            }

            case N_SHOTFX:
            {
                int scn = getint(p), gun = getint(p), id = getint(p);
                vec from, to;
                loopk(3) from[k] = getint(p)/DMF;
                loopk(3) to[k] = getint(p)/DMF;
                int rays = getint(p);
                loopj(rays) loopk(3) shotrays[j][k] = getint(p)/DNF;
                fpsent *s = getclient(scn);
                if(!s || s==player1 || s->ai.local) break;
                if(WEAPONI(gun)>WEAP_FIST && WEAPONI(gun)<=WEAP_PISTOL && s->ammo[WEAPONI(gun)]) s->ammo[WEAPONI(gun)]--;
                s->gunselect = clamp(WEAPONI(gun), (int)WEAP_FIST, (int)WEAP_PISTOL);
                s->gunwait = WEAP(gun,attackdelay);
                int prevaction = s->lastaction;
                s->lastaction = lastmillis;
                s->lastattackgun = s->gunselect;
                shoteffects(gun, from, to, s, false, id, prevaction);
                break;
            }

            case N_EXPLODEFX:
            {
                int ecn = getint(p), gun = getint(p), id = getint(p);
                fpsent *e = getclient(ecn);
                if(!e) break;
                explodeeffects(gun, e, false, id);
                break;
            }
            case N_DAMAGE:
            {
                int tcn = getint(p),
                    acn = getint(p),
                    damage = getint(p),
                    armour = getint(p),
                    health = getint(p),
                    gun = getint(p);
                fpsent *target = getclient(tcn),
                       *actor = getclient(acn);
                if(!target || !actor) break;
                if (damage > 0) damage = min(damage, target->health+target->armour);
                else damage = max(damage, target->health-target->maxhealth);
                target->armour = armour;
                target->health = health;
                if(target->state == CS_ALIVE && actor != player1) target->lastpain = lastmillis;
                damaged(damage, target, actor, false, gun);
                break;
            }

            case N_HITPUSH:
            {
                int tcn = getint(p), gun = getint(p), damage = getint(p);
                fpsent *target = getclient(tcn);
                vec dir;
                loopk(3) dir[k] = getint(p)/DNF;
                if(target) target->hitpush(damage * (target->health<=0 ? deadpush : 1), dir, NULL, gun);
                break;
            }

            case N_DIED:
            {
                int vcn = getint(p), acn = getint(p), frags = getint(p), gun = getint(p), special = getint(p);
                fpsent *victim = getclient(vcn),
                       *actor = getclient(acn);
                if(!actor) break;
                actor->frags = frags;
                if(actor!=player1 && (!cmode || !cmode->hidefrags()))
                {
                    defformatstring(ds)("%d", actor->frags);
                    particle_textcopy(actor->abovehead(), ds, PART_TEXT, 2000, 0x32FF64, 4.0f, -8);
                }
                if(!victim) break;
                killed(victim, actor, gun, special);
                break;
            }

            case N_EFFECT:
            {
                int vcn = getint(p), effects = getint(p);
                fpsent *victim = getclient(vcn);
                if(!victim) break;
                victim->effects = effects;
                victim->updateEffects();
                break;
            }
            
            case N_GUNSELECT:
            {
                if(!d) return;
                int gun = getint(p);
                d->gunselect = max(gun, 0);
                playsound(S_WEAPLOAD, &d->o);
                break;
            }

            case N_TAUNT:
            {
                if(!d) return;
                d->lasttaunt = lastmillis;
                break;
            }

            case N_RESUME:
            {
                for(;;)
                {
                    int cn = getint(p);
                    if(p.overread() || cn<0) break;
                    fpsent *d = (cn == player1->clientnum ? player1 : newclient(cn));
                    parsestate(d, p, true);
                }
                break;
            }

            case N_ITEMSPAWN:
            {
                int i = getint(p);
                if(!entities::ents.inrange(i)) break;
                entities::setspawn(i, true);
                ai::itemspawned(i);
                playsound(S_ITEMSPAWN, &entities::ents[i]->o, NULL, 0, 0, 0, -1, 0, 1500);
                #if 0
                const char *name = entities::itemname(i);
                if(name) particle_text(entities::ents[i]->o, name, PART_TEXT, 2000, 0x32FF64, 4.0f, -8);
                #endif
                int icon = entities::itemicon(i);
                if(icon >= 0) particle_icon(vec(0.0f, 0.0f, 4.0f).add(entities::ents[i]->o), icon%8, icon/8, PART_HUD_ICON, 2000, 0xFFFFFF, 2.0f, -8);
                break;
            }

            case N_ITEMACC:            // server acknowledges that I picked up this item
            {
                int i = getint(p), cn = getint(p);
                fpsent *d = getclient(cn);
                entities::pickupeffects(i, d);
                break;
            }

            case N_CLIPBOARD:
            {
                int cn = getint(p), unpacklen = getint(p), packlen = getint(p);
                fpsent *d = getclient(cn);
                ucharbuf q = p.subbuf(max(packlen, 0));
                if(d) unpackeditinfo(d->edit, q.buf, q.maxlen, unpacklen);
                break;
            }

            case N_EDITF:              // coop editing messages
            case N_EDITT:
            case N_EDITM:
            case N_FLIP:
            case N_COPY:
            case N_PASTE:
            case N_ROTATE:
            case N_REPLACE:
            case N_DELCUBE:
            {
                if(!d) return;
                selinfo sel;
                sel.o.x = getint(p); sel.o.y = getint(p); sel.o.z = getint(p);
                sel.s.x = getint(p); sel.s.y = getint(p); sel.s.z = getint(p);
                sel.grid = getint(p); sel.orient = getint(p);
                sel.cx = getint(p); sel.cxs = getint(p); sel.cy = getint(p), sel.cys = getint(p);
                sel.corner = getint(p);
                int dir, mode, tex, newtex, mat, filter, allfaces, insel;
                ivec moveo;
                switch(type)
                {
                    case N_EDITF: dir = getint(p); mode = getint(p); if(sel.validate()) mpeditface(dir, mode, sel, false); break;
                    case N_EDITT: tex = getint(p); allfaces = getint(p); if(sel.validate()) mpedittex(tex, allfaces, sel, false); break;
                    case N_EDITM: mat = getint(p); filter = getint(p); if(sel.validate()) mpeditmat(mat, filter, sel, false); break;
                    case N_FLIP: if(sel.validate()) mpflip(sel, false); break;
                    case N_COPY: if(d && sel.validate()) mpcopy(d->edit, sel, false); break;
                    case N_PASTE: if(d && sel.validate()) mppaste(d->edit, sel, false); break;
                    case N_ROTATE: dir = getint(p); if(sel.validate()) mprotate(dir, sel, false); break;
                    case N_REPLACE: tex = getint(p); newtex = getint(p); insel = getint(p); if(sel.validate()) mpreplacetex(tex, newtex, insel>0, sel, false); break;
                    case N_DELCUBE: if(sel.validate()) mpdelcube(sel, false); break;
                }
                break;
            }
            case N_REMIP:
            {
                if(!d) return;
                conoutf("%s remipped", colorname(d));
                mpremip(false);
                break;
            }
            case N_EDITENT:            // coop edit of ent
            {
                if(!d) return;
                int i = getint(p);
                float x = getint(p)/DMF, y = getint(p)/DMF, z = getint(p)/DMF;
                int type = getint(p);
                int attr1 = getint(p), attr2 = getint(p), attr3 = getint(p), attr4 = getint(p), attr5 = getint(p);

                mpeditent(i, vec(x, y, z), type, attr1, attr2, attr3, attr4, attr5, false);
                break;
            }
            case N_EDITVAR:
            {
                if(!d) return;
                int type = getint(p);
                getstring(text, p);
                string name;
                filtertext(name, text, false, MAXSTRLEN-1);
                ident *id = getident(name);
                switch(type)
                {
                    case ID_VAR:
                    {
                        int val = getint(p);
                        if(id && id->flags&IDF_OVERRIDE && !(id->flags&IDF_READONLY)) setvar(name, val);
                        break;
                    }
                    case ID_FVAR:
                    {
                        float val = getfloat(p);
                        if(id && id->flags&IDF_OVERRIDE && !(id->flags&IDF_READONLY)) setfvar(name, val);
                        break;
                    }
                    case ID_SVAR:
                    {
                        getstring(text, p);
                        if(id && id->flags&IDF_OVERRIDE && !(id->flags&IDF_READONLY)) setsvar(name, text);
                        break;
                    }
                }
                printvar(d, id);
                break;
            }

            case N_PONG:
                addmsg(N_CLIENTPING, "i", player1->ping = (player1->ping*5+totalmillis-getint(p))/6);
                break;

            case N_CLIENTPING:
                if(!d) return;
                d->ping = getint(p);
                break;

            case N_TIMEUP:
                timeupdate(getint(p));
                break;

            case N_SERVMSG:
                getstring(text, p);
                conoutf("%s", text);
                break;

            case N_SENDDEMOLIST:
            {
                int demos = getint(p);
                if(demos <= 0) conoutf("no demos available");
                else loopi(demos)
                {
                    getstring(text, p);
                    if(p.overread()) break;
                    conoutf("%d. %s", i+1, text);
                }
                break;
            }

            case N_DEMOPLAYBACK:
            {
                int on = getint(p);
                if(on) player1->state = CS_SPECTATOR;
                else clearclients();
                demoplayback = on!=0;
                player1->clientnum = getint(p);
                gamepaused = false;
                const char *alias = on ? "demostart" : "demoend";
                if(identexists(alias)) execute(alias);
                break;
            }

            case N_CURRENTMASTER:
            {
                int mm = getint(p);

                if(mm != mastermode)
                {
                    mastermode = mm;
                    conoutf("mastermode is %s (%d)", server::mastermodename(mastermode), mastermode);
                }

                loopv(players) players[i]->privilege = PRIV_NONE;

                int player;
                while((player = getint(p)) >= 0 && !p.overread())
                {
                    int priv = getint(p);

                    fpsent *m = player==player1->clientnum ? player1 : newclient(player);
                    if(m) m->privilege = priv;
                }
                break;
            }

            case N_MASTERMODE:
            {
                mastermode = getint(p);
                conoutf("mastermode is %s (%d)", server::mastermodename(mastermode), mastermode);
                break;
            }

            case N_EDITMODE:
            {
                int val = getint(p);
                if(!d) break;
                if(val)
                {
                    d->editstate = d->state;
                    d->state = CS_EDITING;
                }
                else
                {
                    d->state = d->editstate;
                    if(d->state==CS_DEAD) deathstate(d, true);
                }
                break;
            }

            case N_SPECTATOR:
            {
                int sn = getint(p), val = getint(p);
                fpsent *s;
                if(sn==player1->clientnum)
                {
                    s = player1;
                    if(val && remote && player1->privilege < PRIV_MASTER) senditemstoserver = false;
                }
                else s = newclient(sn);
                if(!s) return;
                if(val)
                {
                    if(s==player1)
                    {
                        if(editmode) toggleedit();
                        if(s->state==CS_DEAD) showscores(false);
                        disablezoom();
                    }
                    s->state = CS_SPECTATOR;
                }
                else if(s->state==CS_SPECTATOR)
                {
                    if(s==player1) stopfollowing();
                    deathstate(s, true);
                }
                break;
            }

            case N_SETTEAM:
            {
                int wn = getint(p);
                getstring(text, p);
                int reason = getint(p);
                fpsent *w = getclient(wn);
                if(!w) return;
                filtertext(w->team, text, false, MAXTEAMLEN);
                static const char * const fmt[2] = { "%s switched to team %s", "%s forced to team %s"};
                if(reason >= 0 && size_t(reason) < sizeof(fmt)/sizeof(fmt[0]))
                    conoutf(fmt[reason], colorname(w), w->team);

                break;
            }

            case N_SETONFIRE:
            {
                int cattacker = getint(p);
                int cvictim = getint(p);
                int gun = getint(p);
                int on = getint(p);
                fpsent *fatt = getclient(cattacker);
                fpsent *fvic = getclient(cvictim);
                if (on) setonfire(fvic, fatt, gun, false);
                else fvic->onfire = false;
                break;
            }

            case N_INFECT:
            {
                int target = getint(p);
                int type = getint(p);
                fpsent *t = getclient(target);
                if(t)
                {
                    t->infectedType = type;
                }
                break;
            }

            case N_RADIOALL:
            case N_RADIOTEAM:
            {
                fpsent *sr = getclient(getint(p));
                getstring(text, p);
                filtertext(text, text, false);

                extern void sayradio(char *s, bool teamonly, fpsent *speaker);
                sayradio(text, false, sr);
                break;
            }

            case N_GUTS:
            {
                fpsent *cn = getclient(getint(p));
                int guts = getint(p);
                if (cn) printf("N_GUTS %i\n", cn->guts = guts);
                break;
            }

            // --- RPG: Shattered Shield inventory (milestone step 4, additive; see src/rpg) ---
            case N_RPG_INV:
            {
                int level = getint(p), xp = getint(p);
                string invs, eqs;
                getstring(invs, p);
                getstring(eqs, p);
                rpg::setinv(invs, eqs, level, xp);
                break;
            }
            case N_RPG_LOOT:
            {
                int idx = getint(p), item = getint(p);
                float x = getint(p)/DMF, y = getint(p)/DMF, z = getint(p)/DMF;
                rpg::setloot(idx, item, x, y, z);
                break;
            }
            case N_RPG_UNLOOT:
            {
                rpg::removeloot(getint(p));
                break;
            }
            case N_RPG_VWEP:
            {
                int cn = getint(p);
                string path; getstring(path, p);
                rpg::setvwep(cn, path);
                break;
            }
            case N_RPG_MANA:
            {
                int mana = getint(p), maxmana = getint(p);
                rpg::setmana(mana, maxmana);
                break;
            }
            case N_RPG_CD:
            {
                int ability = getint(p), waitms = getint(p);
                rpg::setcooldown(ability, waitms, totalmillis);
                break;
            }
            case N_RPG_HPMAX:
            {
                // RPG: engine max HP follows the stat formula (server-authoritative).
                // Same keep-current-HP rule as the server: full heal only when the
                // character was already full, clamp down otherwise.
                int maxhp = getint(p);
                if(player1 && maxhp > 0)
                {
                    if(player1->health >= player1->maxhealth) player1->health = maxhp;
                    else if(player1->health > maxhp) player1->health = maxhp;
                    player1->maxhealth = maxhp;
                }
                break;
            }
            // --- RPG: Shattered Shield quest chain v1 (additive) ---
            case N_RPG_QMSG:
            {
                string msg;
                getstring(msg, p);
                // quest dialogue / event lines print as "rpg: " lines, so the
                // chat box System tab picks them up via the existing tap.
                conoutf("rpg: %s", msg);
                break;
            }
            case N_RPG_QUEST:
            {
                int qid = getint(p), st = getint(p), kills = getint(p), need = getint(p);
                rpg::setclientquest(qid, st, kills, need);
                break;
            }
            case N_RPG_CLANCHAT:
            {
                // clan channel: [TAG] Sender: text -> Clan tab (green)
                int scn = getint(p);
                string tag, text;
                getstring(tag, p);
                getstring(text, p);
                filtertext(text, text);
                fpsent *d = getclient(scn);
                if(d)
                {
                    if(tag[0]) conoutf(CON_TEAMCHAT, "[%s] %s:\\f1 %s", tag, colorname(d), text);
                    else conoutf(CON_TEAMCHAT, "%s:\\f1 %s", colorname(d), text);
                }
                break;
            }
            case N_RPG_CLAN:
            {
                int id = getint(p), rank = getint(p);
                string cname, ctag;
                getstring(cname, p);
                getstring(ctag, p);
                rpg::setclientclan(id, rank, cname, ctag);
                break;
            }

            case N_BUY:
            {
                fpsent *cn = getclient(getint(p));
                int item = getint(p);
                int guts = getint(p);

                if (cn)
                {
                    printf("BUY %p %i %i\n", cn, item, guts);
                    cn->guts -= guts;
                    applyeffect(cn, item);
                }
                break;
            }

            case N_SHOPOPEN:
            {
                // intermission shop: open the buy menu automatically
                game::openshop(getint(p));
                break;
            }

            #define PARSEMESSAGES 1
            #include "gamemode/capture.h"
            #include "gamemode/ctf.h"
            #include "gamemode/infection.h"
            #include "gamemode/dmsp.h"
            #undef PARSEMESSAGES

            case N_ANNOUNCE:
            {
                int t = getint(p);
                if     (t==I_QUAD)  { playsound(S_V_QUAD10, NULL, NULL, 0, 0, 0, -1, 0, 3000);  conoutf(CON_GAMEINFO, "\f2quad damage will spawn in 10 seconds!"); }
                break;
            }

            case N_MAPVOTE:
            {
                int vn = getint(p);
                fpsent *v = game::getclient(vn);
                getstring(text, p);
                filtertext(text, text);
                int reqmode = getint(p);
                if(!v) break;
                vote(v, text, reqmode);
                break;
            }

            case N_CLEARVOTE:
            {
                int vn = getint(p);
                fpsent *v = game::getclient(vn);
                if(!v) break;
                clearvotes(v, true);
                break;
            }

            case N_NEWMAP:
            {
                int size = getint(p);
                if(size>=0) emptymap(size, true, NULL);
                else enlargemap(true);
                mapvotes.shrink(0);
                if(d && d!=player1)
                {
                    int newsize = 0;
                    while(1<<newsize < getworldsize()) newsize++;
                    conoutf(size>=0 ? "%s started a new map of size %d" : "%s enlarged the map to size %d", colorname(d), newsize);
                }
                break;
            }

            case N_INITAI:
            {
                int bn = getint(p), on = getint(p), type = getint(p), sk = clamp(getint(p), 1, 101), pm = getint(p), pc = getint(p);
                string name, team;
                getstring(text, p);
                filtertext(name, text, NAMEALLOWSPACES, MAXNAMELEN);
                getstring(text, p);
                filtertext(team, text, false, MAXTEAMLEN);
                fpsent *b = newclient(bn);
                if(!b) break;
                ai::init(b, (ai::AiType)type, on, sk, bn, pm, pc, name, team);
                break;
            }

            case N_SERVER_COMMAND:
            {
                int req = getint(p);
                getstring(text, p);
                loopv(commands)
                {
                    if(commands[i].req == req)
                    {
                        ident *id = newident("server_result");
                        identstack stack;

                        loopiter(id, stack, text);
                            execute(commands[i].callback);
                        loopend(id,     stack);

                        DELETEA(commands[i].callback);
                        commands.remove(i);
                        break;
                    }
                }
            }
            break;

            default:
                conoutf(CON_ERROR, "Invalid network type: %i from %i\n", type, cn);
                neterr("type", cn < 0);
                return;
        }
        if(initmap)
        {
            int mode = gamemode;
            const char *map = getclientmap();
            if((multiplayer(false) && !m_mp(mode)) || (mode!=1 && !map[0]))
            {
                mode = remote ? lobbymode : localmode;
                map = remote ? lobbymap : localmap;
            }
            changemap(map, mode);
        }
    }

    void receivefile(packetbuf &p)
    {
        int type;
        while(p.remaining()) switch(type = getint(p))
        {
            case N_DEMOPACKET: return;
            case N_SENDDEMO:
            {
                string mname;
                getstring(mname, p, MAXSTRLEN);

                static string buf;
                copystring(buf, demodir);
                if(demodir[0])
                {
                    int len = strlen(buf);
                    if(buf[len] != '/' && buf[len] != '\\' && len+1 < (int)sizeof(buf)) { buf[len] = '/'; buf[len+1] = '\0'; }
                    const char *dir = findfile(buf, "w");
                    if(!fileexists(dir, "w")) createdir(dir);
                }

                defformatstring(fname)("%s/%d_%s.dmo", demodir, lastmillis, mname);
                stream *demo = openrawfile(fname, "wb");
                if(!demo) return;
                conoutf("received demo \"%s\"", fname);
                ucharbuf b = p.subbuf(p.remaining());
                demo->write(b.buf, b.maxlen);
                delete demo;
                break;
            }

            case N_SENDMAP:
            {
                if(!m_edit) return;
                string oldname;
                copystring(oldname, getclientmap());
                defformatstring(mname)("getmap_%d", lastmillis);
                defformatstring(fname)("packages/base/%s.ogz", mname);
                stream *map = openrawfile(path(fname), "wb");
                if(!map) return;
                conoutf("received map");
                ucharbuf b = p.subbuf(p.remaining());
                map->write(b.buf, b.maxlen);
                delete map;
                if(load_world(mname, oldname[0] ? oldname : NULL))
                    entities::spawnitems(true);
                remove(findfile(fname, "rb"));
                break;
            }

            case N_AUTH_SERVER_HELLO:
            {
                if(connectionState != CONNECTION_STATE_SERVER_HELLO) return neterr("Unexpected N_AUTH_SERVER_HELLO");
                defformatstring(fname)("data/auth/current-server-%d.cert", lastmillis);
                stream *serverCertificate = openrawfile(fname, "w+b");
                if(serverCertificate)
                {
                    conoutf("received server certificate");
                    ucharbuf b = p.subbuf(p.remaining());
                    serverCertificate->write(b.buf, b.maxlen);

                    if(!auth::verifyCertificateWithCA(serverCertificate))
                    {
                        conoutf("\frserver is not correctly signed by CA");
                        return disconnect();
                    }

                    if(!auth::verifyNotInCRL(serverCertificate))
                    {
                        conoutf("\frserver's certificate is in the CRL");
                        return disconnect();
                    }
                }

                if(serverCertificate && auth::clientCertificate != NULL)
                {
                    auth::getRandom(&randomData);

                    uchar *out = NULL; int outSize = 0;
                    if(auth::encryptWithPublicCert(serverCertificate, randomData, sizeof(string), &out, &outSize))
                    {
                        conoutf("creating crypto challenge for the server. (%i %s)", outSize, out);
                        sendfile(-1, 2, auth::clientCertificate, "im+", N_CLIENT_AUTH, outSize, out);
                        delete [] out;
                    }
                    else
                    {
                        neterr("cannot encrypt data with server's public certificate");
                    }
                }
                else
                {
                    // We don't have a cert ourselves, ask the server for forgiveness
                    conoutf("Trying to connect without authentication");
                    addmsg(N_CLIENT_AUTH, "r");
                }

                connectionState = CONNECTION_STATE_CLIENT_AUTH;
                DELETEP(serverCertificate);
                remove(findfile(fname, "rb"));
            }
            break;

            case N_SERVER_AUTH:
            {
                if(connectionState != CONNECTION_STATE_CLIENT_AUTH) return neterr("Unexpected N_SERVER_AUTH");

                //Verify the challenge result
                {
                    int challengeResultLength = getint(p);
                    ucharbuf challengeResult = p.subbuf(challengeResultLength);
                    bool passed = false;

                    conoutf("Verifying server's result to our challenge (%i %lu)", challengeResultLength, sizeof(randomData));

                    if(challengeResultLength == sizeof(randomData))
                    {
                        passed = true;
                        for(int i = 0; i < sizeof(string); i++)
                        {
                            if(challengeResult.buf[i] != randomData[i])
                            {
                                passed = false;
                                break;
                            }
                        }
                    }

                    if(!passed)
                    {
                        defformatstring(errString)("Server did not pass our challenge (%i %lu).", challengeResultLength, sizeof(string));
                        return neterr(errString);
                    }
                }

                int encryptedRandomnessLength = getint(p);
                ucharbuf encryptedRandomness = p.subbuf(encryptedRandomnessLength);

                conoutf("Decrypting server's challenge (%i)", encryptedRandomnessLength);

                uchar *out = NULL; int outSize = 0;
                if(!auth::decryptWithPrivateCert(auth::clientCertificatePrivate, encryptedRandomness.buf, encryptedRandomness.maxlen, &out, &outSize))
                {
                    return neterr("Failed to decrypt the random data.");
                }
                else
                {
                    conoutf("Sending challenge result (%i)", outSize);
                    addmsg(N_AUTH_FINISH, "rm+", outSize, out);
                    delete[] out;
                }

                connectionState = CONNECTION_STATE_SERVER_AUTH;
            }
            break;
        }
        #undef FINISH_PACKAGE
    }

    VAR(dbgnetworkclient, 0, 0, 1);
    VAR(dbgnetworkclientignorepos, 0, 1, 1);
    void dumpReceivedPackage(int realSender, int chan, const packetbuf &p_)
    {
        if(dbgnetworkclientignorepos && chan == 0) return; //ignore N_POS
        ucharbuf p = p_;
        int sender = realSender;
        fpsent *ci = sender>=0 ? getclient(sender) : NULL, *cq = ci, *cm = ci;
        printf("%i(%i)[%i]->", sender, chan, connectionState);
        if(p_.packet->flags&ENET_PACKET_FLAG_UNSEQUENCED)
        {
            printf("<ignord: not sequenced>");
        }
        else if(chan > 2)
        {
            printf("<ignored: invalid channel>");
        }
        else
        {
            string str;
            int curmsg, type;
            while((curmsg = p.length()) < p.maxlen) switch(type = getint(p))
            {
                case N_SERVINFO:
                    printf("<N_CONNECT[%i], (cn:%i protocolVersion:%i sessionid:%i authmode:%i ", N_CONNECT, getint(p), getint(p), getint(p), getint(p));
                    getstring(str, p);
                    printf(" description:%s)>", str);
                    break;
                case N_PONG:
                    printf("<N_PONG[%i], %i>", N_PONG, getint(p));
                    break;
                case N_SERVMSG:
                    getstring(str, p);
                    filtertext(str, str);
                    printf("<N_SERVMSG[%i], %s>", N_SERVMSG, str);
                    break;
                case N_PAUSEGAME:
                    printf("<N_PAUSEGAME[%i] %i (by:%i)>", N_PAUSEGAME, getint(p), getint(p));
                    break;
                case N_INITAI:
                    printf("<N_INITAI[%i] %i (owner:%i type:%i skill:%i class:%i model:%i ",
                             N_INITAI, getint(p), getint(p), getint(p), getint(p), getint(p), getint(p));
                    getstring(str, p);
                    printf("name:%s ", str);
                    getstring(str, p);
                    printf("team:%s)>", str);
                    break;
                case N_INFECT:
                    printf("<N_INFECT[%i] (cn:%i type:%i)>", N_INFECT, getint(p), getint(p));
                    break;
                case N_SPAWNSTATE:
                    printf("<N_SPAWNSTATE[%i] %i (ls:%i guts:%i health:%i maxhealth:%i armour:%i armourtype:%i playerclass:%i playermodel:%i gunselect:%i weapons[%i]:[",
                           N_SPAWNSTATE, getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), getint(p), NUMWEAPS);
                    loopi(NUMWEAPS)
                    {
                        printf("%i,", getint(p));
                    }
                    printf("]>");

                    break;
                case N_DAMAGE:
                    printf("<N_DAMAGE[%i] (target:%i actor:%i damage:%i armour:%i health:%i gun:%i)>", N_DAMAGE, getint(p), getint(p), getint(p), getint(p), getint(p), getint(p));
                    break;
                case N_HITPUSH:
                    printf("<N_HITPUSH[%i] (target:%i gun:%i damage:%i v:(%i %i %i)>", N_HITPUSH, getint(p), getint(p), getint(p), getint(p), getint(p), getint(p));
                    break;
                case N_FROMAI:
                    printf("<N_FROMAI[%i] %i>", N_FROMAI, sender = getint(p));
                    break;
                case N_WELCOME:
                    printf("<N_WELCOME[%i] (allowedweaps:%i)>", N_WELCOME, getint(p));
                    break;
                case N_MAPCHANGE:
                    getstring(str, p);
                    printf("<N_MAPCHANGE[%i] %s (gamemode:%i notgotitems:%i)>", N_MAPCHANGE, str, getint(p), getint(p));
                    break;
                default:
                    printf("<unkown packet %i (n:%i)>", type, curmsg);
                    break;
            }
        }
        ucharbuf q = p_;
        while(q.remaining()) printf("%i|", getint(q));
        printf("\n");
    }
    
    void parsepacketclient(int chan, packetbuf &p)   // processes any updates from the server
    {
        if(dbgnetworkclient) dumpReceivedPackage(-1, chan, p);
        if(p.packet->flags&ENET_PACKET_FLAG_UNSEQUENCED) return;
        switch(chan)
        {
            case 0:
                parsepositions(p);
                break;

            case 1:
                parsemessages(-1, NULL, p);
                break;

            case 2:
                receivefile(p);
                break;
        }
    }

    void serverCommand(const char *callback, const char *arg1, const char *arg2, const char *arg3, const char *arg4, const char *arg5, const char *arg6, const char *arg7)
    {
        int argc;
        #define argif(i) if(arg##i != NULL && arg##i [0]) argc = i;
        argif(7)
        else argif(6)
        else argif(5)
        else argif(4)
        else argif(3)
        else argif(2)
        else argif(1)
        #undef argif
        else
        {
            conoutf(CON_ERROR, "calling server command with no arguments.");
            intret(0);
            return;
        }

        reqid++;

        ServerCommand *cmd = &commands.add();
        cmd->req = reqid;
        cmd->callback = newstring(callback);

        uchar buf [MAXTRANS];
        ucharbuf p = ucharbuf(buf, sizeof(buf));

        putint(p, N_SERVER_COMMAND);
        putint(p, reqid);
        putint(p, argc);
        if(argc > 0) sendstring(arg1, p);
        if(argc > 1) sendstring(arg2, p);
        if(argc > 2) sendstring(arg3, p);
        if(argc > 3) sendstring(arg4, p);
        if(argc > 4) sendstring(arg5, p);
        if(argc > 5) sendstring(arg6, p);
        if(argc > 6) sendstring(arg7, p);

        messages.put(buf, p.length());
    }
    COMMAND(serverCommand, "ssssssss");
    
    void getmap()
    {
        if(!m_edit) { conoutf(CON_ERROR, "\"getmap\" only works in coop edit mode"); return; }
        conoutf("getting map...");
        addmsg(N_GETMAP, "r");
    }
    COMMAND(getmap, "");

    void stopdemo()
    {
        if(remote)
        {
            if(player1->privilege<PRIV_ADMIN) return;
            addmsg(N_STOPDEMO, "r");
        }
        else server::stopdemo();
    }
    COMMAND(stopdemo, "");

    void recorddemo(int val)
    {
        if(remote && player1->privilege<PRIV_ADMIN) return;
        addmsg(N_RECORDDEMO, "ri", val);
    }
    ICOMMAND(recorddemo, "i", (int *val), recorddemo(*val));

    void cleardemos(int val)
    {
        if(remote && player1->privilege<PRIV_ADMIN) return;
        addmsg(N_CLEARDEMOS, "ri", val);
    }
    ICOMMAND(cleardemos, "i", (int *val), cleardemos(*val));

    void getdemo(int i)
    {
        if(i<=0) conoutf("getting demo...");
        else conoutf("getting demo %d...", i);
        addmsg(N_GETDEMO, "ri", i);
    }
    ICOMMAND(getdemo, "i", (int *val), getdemo(*val));

    void listdemos()
    {
        conoutf("listing demos...");
        addmsg(N_LISTDEMOS, "r");
    }
    COMMAND(listdemos, "");

    void sendmap()
    {
        if(!m_edit || (player1->state==CS_SPECTATOR && remote && player1->privilege < PRIV_MASTER)) { conoutf(CON_ERROR, "\"sendmap\" only works in coop edit mode"); return; }
        conoutf("sending map...");
        defformatstring(mname)("sendmap_%d", lastmillis);
        save_world(mname, true);
        defformatstring(fname)("packages/base/%s.ogz", mname);
        stream *map = openrawfile(path(fname), "rb");
        if(map)
        {
            int len = map->size();
            if(len > 1024*1024) conoutf(CON_ERROR, "map is too large");
            else if(len <= 0) conoutf(CON_ERROR, "could not read map");
            else
            {
                sendfile(-1, 2, map);
                if(needclipboard >= 0) needclipboard++;
            }
            delete map;
        }
        else conoutf(CON_ERROR, "could not read map");
        remove(findfile(fname, "rb"));
    }
    COMMAND(sendmap, "");

    void gotoplayer(const char *arg)
    {
        if(player1->state!=CS_SPECTATOR && player1->state!=CS_EDITING) return;
        int i = parseplayer(arg);
        if(i>=0)
        {
            fpsent *d = getclient(i);
            if(!d || d==player1) return;
            player1->o = d->o;
            vec dir;
            vecfromyawpitch(player1->yaw, player1->pitch, 1, 0, dir);
            player1->o.add(dir.mul(-32));
            player1->resetinterp();
        }
    }
    COMMANDN(goto, gotoplayer, "s");

    void gotosel()
    {
        if(player1->state!=CS_EDITING) return;
        player1->o = getselpos();
        vec dir;
        vecfromyawpitch(player1->yaw, player1->pitch, 1, 0, dir);
        player1->o.add(dir.mul(-32));
        player1->resetinterp();
    }
    COMMAND(gotosel, "");
}

