// quests.cpp - quest + dialogue content for the quest engine v2.
//
// data/rpg/quests.cfg holds the quest definitions, staged objectives and NPC
// dialogue node graphs in the flat ss_* setter syntax (same parser as the
// item content files: quoted strings, // comments, strict, never executed).
// Parsed after NPCs load at map change on BOTH client and server, retried
// on each map change until success, then read-only afterwards. Dialogue
// itself is server-authoritative (see the talk engine in the game module);
// this file only loads, validates and answers queries. (Quest load fix,
// Oct 26, 2026: parsing at init always failed because the NPC table is
// empty until map load, and the old done guard then blocked every retry.)
#include "rpg.h"
#include "items.h"
#include "cube.h"
#include <vector>
#include <string>

namespace rpg
{
    static questdef qdefs[16];
    static int nqdefs = 0;
    static dlggraph qgraphs[8];
    static int nqgraphs = 0;

    // ---- parse state (one load per process) ----
    struct qparse
    {
        int curq, curgraph, curnode;
        queststage *curstage;
        int errors;
        qparse() : curq(-1), curgraph(-1), curnode(-1), curstage(NULL), errors(0) {}
    };
    static qparse QP;

    static int qindexbyid(int qid)
    {
        loopi(nqdefs) if(qdefs[i].qid == qid) return i;
        return -1;
    }
    static int gindexbyname(const char *name)
    {
        loopi(nqgraphs) if(!strcmp(qgraphs[i].name, name)) return i;
        return -1;
    }
    static int nindexbyid(const dlggraph &g, const char *id)
    {
        loopi(g.nnodes) if(!strcmp(g.nodes[i].id, id)) return i;
        return -1;
    }

    static int opfromname(const char *s)
    {
        if(!strcmp(s, "=") || !strcmp(s, "==")) return CMP_EQ;
        if(!strcmp(s, ">=")) return CMP_GE;
        if(!strcmp(s, "<=")) return CMP_LE;
        if(!strcmp(s, "<"))  return CMP_LT;
        if(!strcmp(s, ">"))  return CMP_GT;
        return -1;
    }

    // Parse one condition from tokens[t..e). Returns tokens consumed, or -1.
    static int parsecond(const char **a, int t, int e, dlgcond &c, std::string &err)
    {
        memset(&c, 0, sizeof(c));
        c.verb = -1;
        if(t >= e) { err = "empty condition"; return -1; }
        const char *v = a[t];
        if(!strcmp(v, "quest") && e - t == 5 && !strcmp(a[t+2], "stage"))
        {
            c.verb = CND_STAGE; c.qid = atoi(a[t+1]);
            c.op = opfromname(a[t+3]); c.n = atoi(a[t+4]);
            if(c.op < 0) { err = "bad stage operator"; return -1; }
            return 5;
        }
        if(!strcmp(v, "hasitem") && e - t == 4)
        {
            c.verb = CND_HASITEM; copystring(c.item, a[t+1], sizeof(c.item));
            c.op = opfromname(a[t+2]); c.n = atoi(a[t+3]);
            if(c.op != CMP_GE && c.op != CMP_LT) { err = "hasitem takes >= or <"; return -1; }
            return 4;
        }
        if(!strcmp(v, "level") && e - t == 3)
        {
            c.verb = CND_LEVEL; c.op = opfromname(a[t+1]); c.n = atoi(a[t+2]);
            if(c.op != CMP_GE) { err = "level takes >="; return -1; }
            return 3;
        }
        if(!strcmp(v, "questdone") && e - t == 2)   { c.verb = CND_QUESTDONE; c.qid = atoi(a[t+1]); return 2; }
        if(!strcmp(v, "objready") && e - t == 2)    { c.verb = CND_OBJREADY;  c.qid = atoi(a[t+1]); return 2; }
        if(!strcmp(v, "cooldownready") && e - t == 2){ c.verb = CND_COOLDOWNREADY; c.qid = atoi(a[t+1]); return 2; }
        err = "unknown condition";
        return -1;
    }

    // Parse one action from tokens[t..e). Returns tokens consumed, or -1.
    // Flow keywords (goto/close) are handled by the response parser, not here.
    static int parseaction(const char **a, int t, int e, dlgaction &ac, std::string &err)
    {
        memset(&ac, 0, sizeof(ac));
        ac.verb = -1;
        if(t >= e) { err = "empty action"; return -1; }
        const char *v = a[t];
        if(!strcmp(v, "setstage") && e - t == 3)      { ac.verb = ACT_SETSTAGE; ac.qid = atoi(a[t+1]); ac.n = atoi(a[t+2]); return 3; }
        if(!strcmp(v, "completequest") && e - t == 2) { ac.verb = ACT_COMPLETEQUEST; ac.qid = atoi(a[t+1]); return 2; }
        if(!strcmp(v, "giveitem") && e - t == 3)      { ac.verb = ACT_GIVEITEM; copystring(ac.item, a[t+1], sizeof(ac.item)); ac.count = atoi(a[t+2]); return 3; }
        if(!strcmp(v, "takeitem") && e - t == 3)      { ac.verb = ACT_TAKEITEM; copystring(ac.item, a[t+1], sizeof(ac.item)); ac.count = atoi(a[t+2]); return 3; }
        if(!strcmp(v, "givexp") && e - t == 2)       { ac.verb = ACT_GIVEXP; ac.n = atoi(a[t+1]); return 2; }
        if(!strcmp(v, "givefetchxp") && e - t == 2)   { ac.verb = ACT_GIVEFETCHXP; ac.qid = atoi(a[t+1]); return 2; }
        if(!strcmp(v, "spawnchampion") && e - t == 1) { ac.verb = ACT_SPAWNCHAMPION; return 1; }
        if(!strcmp(v, "rollfetch") && e - t == 2)     { ac.verb = ACT_ROLLFETCH; ac.qid = atoi(a[t+1]); return 2; }
        err = "unknown action";
        return -1;
    }

    static bool qline(const char *cmd, int argc, const char *argv[], std::string &err)
    {
        #define QERR(...) do { \
            char _b[256]; formatstring(_b)(__VA_ARGS__); \
            err = _b; QP.errors++; return false; } while(0)
        #define NEEDQ() do { if(QP.curq < 0) QERR("'%s' outside any ss_quest", cmd); } while(0)
        #define NEEDSTAGE() do { NEEDQ(); if(!QP.curstage) QERR("'%s' outside any ss_stage", cmd); } while(0)
        #define NEEDNODE() do { if(QP.curnode < 0) QERR("'%s' outside any ss_node", cmd); } while(0)

        if(!strcmp(cmd, "ss_quest"))
        {
            if(argc != 1) QERR("ss_quest takes 1 arg");
            int qid = atoi(argv[0]);
            if(qid <= 0) QERR("bad quest id '%s'", argv[0]);
            if(qindexbyid(qid) >= 0) QERR("duplicate quest id %d", qid);
            if(nqdefs >= (int)(sizeof(qdefs)/sizeof(qdefs[0]))) QERR("too many quests");
            QP.curq = nqdefs++; QP.curstage = NULL; QP.curnode = -1;
            memset(&qdefs[QP.curq], 0, sizeof(qdefs[QP.curq]));
            qdefs[QP.curq].qid = qid;
            qdefs[QP.curq].giver = -1;
            return true;
        }
        if(!strcmp(cmd, "ss_quest_key"))   { NEEDQ(); if(argc != 1) QERR("ss_quest_key takes 1 arg");
            loopi(nqdefs) if(i != QP.curq && !strcmp(qdefs[i].key, argv[0])) QERR("duplicate quest key '%s'", argv[0]);
            copystring(qdefs[QP.curq].key, argv[0], sizeof(qdefs[QP.curq].key)); return true; }
        if(!strcmp(cmd, "ss_quest_name"))  { NEEDQ(); if(argc != 1) QERR("ss_quest_name takes 1 arg");
            copystring(qdefs[QP.curq].name, argv[0], sizeof(qdefs[QP.curq].name)); return true; }
        if(!strcmp(cmd, "ss_quest_giver")){ NEEDQ(); if(argc != 1) QERR("ss_quest_giver takes 1 arg");
            int ni = npcindexbyid(argv[0]);
            if(ni < 0) QERR("unknown npc id '%s'", argv[0]);
            qdefs[QP.curq].giver = ni; return true; }
        if(!strcmp(cmd, "ss_quest_final")){ NEEDQ(); if(argc != 1) QERR("ss_quest_final takes 1 arg");
            qdefs[QP.curq].finalstage = atoi(argv[0]); return true; }
        if(!strcmp(cmd, "ss_quest_repeat")){ NEEDQ(); if(argc != 1) QERR("ss_quest_repeat takes 1 arg");
            qdefs[QP.curq].repeatsec = atoi(argv[0]); return true; }
        if(!strcmp(cmd, "ss_quest_done")) { NEEDQ(); if(argc != 1) QERR("ss_quest_done takes 1 arg");
            copystring(qdefs[QP.curq].donetext, argv[0], sizeof(qdefs[QP.curq].donetext)); return true; }
        if(!strcmp(cmd, "ss_stage"))
        {
            NEEDQ(); if(argc != 1) QERR("ss_stage takes 1 arg");
            questdef &qd = qdefs[QP.curq];
            int s = atoi(argv[0]);
            if(s < 1 || s > 7) QERR("bad stage %d", s);
            if(qd.nstages >= (int)(sizeof(qd.stages)/sizeof(qd.stages[0]))) QERR("too many stages");
            loopi(qd.nstages) if(qd.stages[i].stage == s) QERR("duplicate stage %d", s);
            QP.curstage = &qd.stages[qd.nstages++];
            memset(QP.curstage, 0, sizeof(*QP.curstage));
            QP.curstage->stage = s;
            return true;
        }
        if(!strcmp(cmd, "ss_objective"))
        {
            NEEDSTAGE(); if(argc < 1) QERR("ss_objective takes args");
            queststage &st = *QP.curstage;
            if(!strcmp(argv[0], "kill") && argc == 3)
            {
                st.objtype = QOBJ_KILL; copystring(st.objarg, argv[1], sizeof(st.objarg));
                st.objcount = atoi(argv[2]);
                if(st.objcount < 1) QERR("kill count must be >= 1");
                return true;
            }
            if(!strcmp(argv[0], "fetch") && argc == 2 && !strcmp(argv[1], "rolled"))
            {
                st.objtype = QOBJ_FETCH; return true;
            }
            if(!strcmp(argv[0], "talk") && argc == 2)
            {
                if(npcindexbyid(argv[1]) < 0) QERR("unknown npc id '%s'", argv[1]);
                st.objtype = QOBJ_TALK; copystring(st.objarg, argv[1], sizeof(st.objarg));
                return true;
            }
            if(!strcmp(argv[0], "none") && argc == 1) { st.objtype = QOBJ_NONE; return true; }
            QERR("bad ss_objective");
        }
        if(!strcmp(cmd, "ss_journal"))
        {
            NEEDSTAGE(); if(argc != 1) QERR("ss_journal takes 1 arg");
            copystring(QP.curstage->journal, argv[0], sizeof(QP.curstage->journal));
            return true;
        }
        if(!strcmp(cmd, "ss_fetch_pool"))
        {
            NEEDQ(); if(argc < 1 || argc > 8) QERR("ss_fetch_pool takes 1..8 items");
            questdef &qd = qdefs[QP.curq];
            qd.npool = 0;
            loopi(argc)
            {
                // Oct 21: one unknown pool item used to fail the WHOLE quest
                // content load (nqdefs/nqgraphs zeroed), leaving every NPC
                // with "nothing to say" at talk time. Skip it loudly instead;
                // quest 6 rolls from whatever is left. An emptied pool still
                // fails validation ("repeatable with empty fetch pool").
                if(finditem(argv[i]) < 0)
                {
                    conoutf("rpg: quests: warning: quest %d: unknown pool item '%s' skipped", qd.qid, argv[i]);
                    continue;
                }
                copystring(qd.pool[qd.npool++], argv[i], sizeof(qd.pool[0]));
            }
            return true;
        }
        if(!strcmp(cmd, "ss_fetch_amount"))
        {
            NEEDQ(); if(argc != 2) QERR("ss_fetch_amount takes 2 args");
            qdefs[QP.curq].poolmin = atoi(argv[0]);
            qdefs[QP.curq].poolmax = atoi(argv[1]);
            if(qdefs[QP.curq].poolmin < 1 || qdefs[QP.curq].poolmax < qdefs[QP.curq].poolmin)
                QERR("bad fetch amount range");
            return true;
        }
        if(!strcmp(cmd, "ss_dialogue"))
        {
            if(argc != 1) QERR("ss_dialogue takes 1 arg");
            if(gindexbyname(argv[0]) >= 0) QERR("duplicate dialogue '%s'", argv[0]);
            if(nqgraphs >= (int)(sizeof(qgraphs)/sizeof(qgraphs[0]))) QERR("too many dialogues");
            QP.curgraph = nqgraphs++; QP.curnode = -1; QP.curq = -1; QP.curstage = NULL;
            memset(&qgraphs[QP.curgraph], 0, sizeof(qgraphs[QP.curgraph]));
            copystring(qgraphs[QP.curgraph].name, argv[0], sizeof(qgraphs[QP.curgraph].name));
            return true;
        }
        if(!strcmp(cmd, "ss_node"))
        {
            if(QP.curgraph < 0) QERR("ss_node outside any ss_dialogue");
            if(argc != 1) QERR("ss_node takes 1 arg");
            dlggraph &g = qgraphs[QP.curgraph];
            if(nindexbyid(g, argv[0]) >= 0) QERR("duplicate node '%s'", argv[0]);
            if(g.nnodes >= (int)(sizeof(g.nodes)/sizeof(g.nodes[0]))) QERR("too many nodes");
            QP.curnode = g.nnodes++;
            memset(&g.nodes[QP.curnode], 0, sizeof(g.nodes[QP.curnode]));
            copystring(g.nodes[QP.curnode].id, argv[0], sizeof(g.nodes[QP.curnode].id));
            return true;
        }
        if(!strcmp(cmd, "ss_nodewhen"))
        {
            NEEDNODE();
            dlgnode &nd = qgraphs[QP.curgraph].nodes[QP.curnode];
            if(nd.nwhen >= (int)(sizeof(nd.when)/sizeof(nd.when[0]))) QERR("too many when clauses");
            std::vector<const char*> a;
            loopi(argc) a.push_back(argv[i]);
            loopi(argc) a.push_back(argv[i]);
            if(parsecond(&a[0], 0, argc, nd.when[nd.nwhen], err) < 0) { QP.errors++; return false; }
            nd.nwhen++;
            return true;
        }
        if(!strcmp(cmd, "ss_nodequest"))
        {
            NEEDNODE(); if(argc != 1) QERR("ss_nodequest takes 1 arg");
            int qid = atoi(argv[0]);
            if(!validquest(qid)) QERR("ss_nodequest: unknown quest %d", qid);
            qgraphs[QP.curgraph].nodes[QP.curnode].quest = qid;
            return true;
        }
        if(!strcmp(cmd, "ss_say"))
        {
            NEEDNODE(); if(argc != 1) QERR("ss_say takes 1 arg");
            dlgnode &nd = qgraphs[QP.curgraph].nodes[QP.curnode];
            if(nd.nsay >= (int)(sizeof(nd.say)/sizeof(nd.say[0]))) QERR("too many say lines");
            copystring(nd.say[nd.nsay++], argv[0], sizeof(nd.say[0]));
            return true;
        }
        if(!strcmp(cmd, "ss_response"))
        {
            NEEDNODE(); if(argc < 3) QERR("ss_response needs text, ->, and an action/flow");
            dlgnode &nd = qgraphs[QP.curgraph].nodes[QP.curnode];
            if(nd.nresp >= (int)(sizeof(nd.resp)/sizeof(nd.resp[0]))) QERR("too many responses (max 4)");
            dlgresponse &r = nd.resp[nd.nresp];
            memset(&r, 0, sizeof(r));
            copystring(r.text, argv[0], sizeof(r.text));
            if(!r.text[0]) QERR("empty response text");
            // [? cond [; cond]*] -> action [; action]* [; goto "node" | ; close]
            std::vector<const char*> a;
            loopi(argc) a.push_back(argv[i]);
            int t = 1;
            if(t < argc && !strcmp(a[t], "?"))
            {
                t++;
                int seg = t;
                while(t < argc && strcmp(a[t], "->"))
                {
                    if(!strcmp(a[t], ";"))
                    {
                        if(r.ncond >= (int)(sizeof(r.cond)/sizeof(r.cond[0]))) QERR("too many response conditions");
                        if(parsecond(&a[0], seg, t, r.cond[r.ncond], err) < 0) { QP.errors++; return false; }
                        r.ncond++; seg = t + 1;
                    }
                    t++;
                }
                if(seg < t)
                {
                    if(r.ncond >= (int)(sizeof(r.cond)/sizeof(r.cond[0]))) QERR("too many response conditions");
                    if(parsecond(&a[0], seg, t, r.cond[r.ncond], err) < 0) { QP.errors++; return false; }
                    r.ncond++;
                }
            }
            if(t >= argc || strcmp(a[t], "->")) QERR("ss_response missing '->'");
            t++;
            int seg = t;
            bool closed = false;
            while(true)
            {
                while(t < argc && strcmp(a[t], ";")) t++;
                // segment [seg, t)
                if(seg < t)
                {
                    if(!strcmp(a[seg], "goto"))
                    {
                        if(t - seg != 2) QERR("goto takes a node id");
                        if(r.gotonode[0] || r.close) QERR("response has two flows");
                        copystring(r.gotonode, a[seg+1], sizeof(r.gotonode));
                    }
                    else if(!strcmp(a[seg], "close") && t - seg == 1)
                    {
                        if(r.gotonode[0] || r.close) QERR("response has two flows");
                        r.close = true;
                    }
                    else
                    {
                        if(r.nact >= (int)(sizeof(r.act)/sizeof(r.act[0]))) QERR("too many actions");
                        if(parseaction(&a[0], seg, t, r.act[r.nact], err) < 0) { QP.errors++; return false; }
                        r.nact++;
                    }
                }
                if(t >= argc) break;
                seg = t + 1; t++;
                closed = true;
            }
            if(!r.gotonode[0] && !r.close) QERR("response needs a flow (goto or close)");
            (void)closed;
            nd.nresp++;
            return true;
        }
        QERR("unknown command '%s'", cmd);
        #undef QERR
        #undef NEEDQ
        #undef NEEDSTAGE
        #undef NEEDNODE
    }

    // Cross-reference validation after the parse. Returns true when clean.
    static bool validatequests()
    {
        int errs = 0;
        #define VERR(...) do { conoutf("rpg: quests: " __VA_ARGS__); errs++; } while(0)
        loopi(nqdefs)
        {
            questdef &qd = qdefs[i];
            if(!qd.name[0]) { VERR("quest %d: missing ss_quest_name", qd.qid); }
            if(!qd.key[0]) { VERR("quest %d: missing ss_quest_key", qd.qid); }
            if(qd.giver < 0) { VERR("quest %d: missing ss_quest_giver", qd.qid); }
            if(qd.finalstage < 1) { VERR("quest %d: bad ss_quest_final", qd.qid); }
            if(qd.nstages < 1) { VERR("quest %d: no stages", qd.qid); }
            if(!qd.donetext[0]) { VERR("quest %d: missing ss_quest_done", qd.qid); }
            // the giver's dialogue graph must exist
            if(qd.giver >= 0)
            {
                const npcdef *npc = npcat(qd.giver);
                if(!npc || gindexbyname(npc->dialogue) < 0)
                    VERR("quest %d: giver npc has no dialogue graph '%s'", qd.qid, npc ? npc->dialogue : "?");
            }
            loopj(qd.nstages)
            {
                queststage &st = qd.stages[j];
                if(!st.journal[0]) VERR("quest %d stage %d: missing ss_journal", qd.qid, st.stage);
                if(st.objtype == QOBJ_FETCH && qd.repeatsec <= 0)
                    VERR("quest %d stage %d: fetch objective needs ss_quest_repeat", qd.qid, st.stage);
            }
            if(qd.repeatsec > 0)
            {
                if(qd.npool < 1) VERR("quest %d: repeatable with empty fetch pool", qd.qid);
                if(qd.poolmin < 1 || qd.poolmax < qd.poolmin) VERR("quest %d: bad fetch amount", qd.qid);
            }
        }
        loopi(nqgraphs)
        {
            dlggraph &g = qgraphs[i];
            if(g.nnodes < 1) { VERR("dialogue '%s': no nodes", g.name); continue; }
            // goto targets (referenced by responses); no-when nodes that are
            // NOT goto targets are entry fallbacks and must come last.
            bool isgototarget[48];
            loopj(g.nnodes) isgototarget[j] = false;
            loopj(g.nnodes) loopk(g.nodes[j].nresp)
                if(g.nodes[j].resp[k].gotonode[0])
                {
                    int t = nindexbyid(g, g.nodes[j].resp[k].gotonode);
                    if(t >= 0) isgototarget[t] = true;
                }
            bool seenfallback = false;
            loopj(g.nnodes)
            {
                dlgnode &nd = g.nodes[j];
                if(nd.nsay < 1) VERR("dialogue '%s' node '%s': no ss_say", g.name, nd.id);
                if(nd.nresp < 1) VERR("dialogue '%s' node '%s': no ss_response", g.name, nd.id);
                if(nd.nwhen == 0 && !isgototarget[j])
                {
                    if(seenfallback) VERR("dialogue '%s': only one fallback node allowed ('%s')", g.name, nd.id);
                    if(j != g.nnodes - 1) VERR("dialogue '%s': fallback node '%s' must be last", g.name, nd.id);
                    seenfallback = true;
                }
                // condition/action cross-refs
                loopk(nd.nwhen)
                {
                    const dlgcond &c = nd.when[k];
                    if((c.verb == CND_STAGE || c.verb == CND_QUESTDONE || c.verb == CND_OBJREADY || c.verb == CND_COOLDOWNREADY) && !validquest(c.qid))
                        VERR("dialogue '%s' node '%s': unknown quest %d", g.name, nd.id, c.qid);
                    if(c.verb == CND_HASITEM && finditem(c.item) < 0)
                        VERR("dialogue '%s' node '%s': unknown item '%s'", g.name, nd.id, c.item);
                }
                loopk(nd.nresp)
                {
                    const dlgresponse &r = nd.resp[k];
                    if(r.gotonode[0] && nindexbyid(g, r.gotonode) < 0)
                        VERR("dialogue '%s' node '%s': goto unknown node '%s'", g.name, nd.id, r.gotonode);
                    loopl(r.ncond)
                    {
                        const dlgcond &c = r.cond[l];
                        if((c.verb == CND_STAGE || c.verb == CND_QUESTDONE || c.verb == CND_OBJREADY || c.verb == CND_COOLDOWNREADY) && !validquest(c.qid))
                            VERR("dialogue '%s' node '%s' resp %d: unknown quest %d", g.name, nd.id, k, c.qid);
                        if(c.verb == CND_HASITEM && finditem(c.item) < 0)
                            VERR("dialogue '%s' node '%s' resp %d: unknown item '%s'", g.name, nd.id, k, c.item);
                    }
                    loopl(r.nact)
                    {
                        const dlgaction &a = r.act[l];
                        if((a.verb == ACT_SETSTAGE || a.verb == ACT_COMPLETEQUEST || a.verb == ACT_GIVEFETCHXP || a.verb == ACT_ROLLFETCH) && !validquest(a.qid))
                            VERR("dialogue '%s' node '%s' resp %d: unknown quest %d", g.name, nd.id, k, a.qid);
                        if(a.verb == ACT_SETSTAGE)
                        {
                            const questdef *qd = questdefbyid(a.qid);
                            if(qd && (a.n < 0 || a.n > qd->finalstage))
                                VERR("dialogue '%s' node '%s' resp %d: stage %d out of range for quest %d", g.name, nd.id, k, a.n, a.qid);
                        }
                        if((a.verb == ACT_GIVEITEM || a.verb == ACT_TAKEITEM) && strcmp(a.item, "rolled") && finditem(a.item) < 0)
                            VERR("dialogue '%s' node '%s' resp %d: unknown item '%s'", g.name, nd.id, k, a.item);
                        if((a.verb == ACT_GIVEITEM || a.verb == ACT_TAKEITEM) && a.count < 1)
                            VERR("dialogue '%s' node '%s' resp %d: bad count", g.name, nd.id, k);
                        if(a.verb == ACT_GIVEXP && a.n < 1)
                            VERR("dialogue '%s' node '%s' resp %d: bad xp", g.name, nd.id, k);
                    }
                }
            }
        }
        #undef VERR
        return errs == 0;
    }

    bool loadquests()
    {
        // Already loaded: do not re-parse (giver indices must keep matching
        // the NPC table quests were resolved against; later non-quest maps
        // must not zero the tables). Not yet loaded: start clean and parse,
        // so a failed attempt retries on the next map change instead of
        // disabling quests for the whole session.
        if(nqdefs > 0 && nqgraphs > 0) return true;
        if(!contentfileexists("data/rpg/quests.cfg"))
        {
            conoutf("rpg: quests: data/rpg/quests.cfg missing - quest content disabled");
            return false;
        }
        std::string err;
        nqdefs = 0; nqgraphs = 0;
        QP = qparse();
        if(!parsecontentfile("data/rpg/quests.cfg", qline, err))
        {
            conoutf("rpg: quests: parse failed: %s", err.c_str());
            nqdefs = 0; nqgraphs = 0;
            return false;
        }
        if(!validatequests())
        {
            conoutf("rpg: quests: validation failed - quest content disabled");
            nqdefs = 0; nqgraphs = 0;
            return false;
        }
        int nnodes = 0;
        loopi(nqgraphs) nnodes += qgraphs[i].nnodes;
        conoutf("rpg: quests: %d quests, %d dialogue graphs (%d nodes) loaded from data/rpg", nqdefs, nqgraphs, nnodes);
        return true;
    }

    int questdefcount() { return nqdefs; }
    int dlgnode_selectentry(const dlggraph *g, const char *name)
    {
        if(!g || g->nnodes < 1) return -1;
        loopi(g->nnodes)
        {
            const dlgnode &nd = g->nodes[i];
            if(nd.nwhen == 0)
            {
                // Goto-only nodes never match at entry; the fallback (last
                // node) always does. (Validation guarantees the shape.)
                if(i == g->nnodes - 1) return i;
                continue;
            }
            bool ok = true;
            loopj(nd.nwhen) if(!dlgcondmet(name, nd.when[j])) { ok = false; break; }
            if(ok) return i;
        }
        return -1;
    }
    const questdef *questdefbyid(int qid) { int i = qindexbyid(qid); return i < 0 ? NULL : &qdefs[i]; }
    const questdef *questdefbyindex(int i) { return (i >= 0 && i < nqdefs) ? &qdefs[i] : NULL; }
    const dlggraph *questgraph(const char *name) { int i = name ? gindexbyname(name) : -1; return i < 0 ? NULL : &qgraphs[i]; }
    const queststage *queststageof(const questdef *qd, int stage)
    {
        if(!qd) return NULL;
        loopi(qd->nstages) if(qd->stages[i].stage == stage) return &qd->stages[i];
        return NULL;
    }

    // Server-side condition evaluation (the client never evaluates these).
    static bool cmpapply(int op, int a, int b)
    {
        switch(op)
        {
            case CMP_EQ: return a == b;
            case CMP_GE: return a >= b;
            case CMP_LE: return a <= b;
            case CMP_LT: return a < b;
            case CMP_GT: return a > b;
        }
        return false;
    }
    bool dlgcondmet(const char *name, const dlgcond &c)
    {
        switch(c.verb)
        {
            case CND_STAGE:      return cmpapply(c.op, queststate(name, c.qid), c.n);
            case CND_HASITEM:    return cmpapply(c.op, itemcount(name, c.item), c.n);
            case CND_LEVEL:
            {
                int level = 1, xp = 0;
                getstats(name, level, xp);
                return cmpapply(c.op, level, c.n);
            }
            case CND_QUESTDONE:  return questiscomplete(name, c.qid);
            case CND_OBJREADY:
            {
                int have = 0, need = 0;
                const questdef *qd = questdefbyid(c.qid);
                if(!qd) return false;
                int st = queststate(name, c.qid);
                const queststage *qs = queststageof(qd, st);
                if(!qs || st <= 0 || st == qd->finalstage) return false;
                if(qs->objtype == QOBJ_KILL)
                {
                    questprogress(name, c.qid, have, need);
                    return have >= need && need > 0;
                }
                if(qs->objtype == QOBJ_FETCH)
                {
                    char item[64]; int amount = 0;
                    if(!repeatableget(name, c.qid, item, sizeof(item), &amount)) return false;
                    return itemcount(name, item) >= amount && amount > 0;
                }
                if(qs->objtype == QOBJ_TALK) return true; // talking to the npc IS the turn-in
                return false;
            }
            case CND_COOLDOWNREADY: return repeatableready(name, c.qid);
        }
        return false;
    }

    // $var substitution for node text. $progress/$target (kill stages),
    // $amount/$item (fetch roll), $name (character).
    void questtextsubst(const char *name, int qid, const char *in, char *out, int outlen)
    {
        if(!in || !out || outlen <= 0) return;
        char amount[32] = "", item[96] = "", progress[32] = "", target[32] = "";
        int have = 0, need = 0;
        const questdef *qd = questdefbyid(qid);
        if(qd)
        {
            const queststage *qs = queststageof(qd, queststate(name, qid));
            if(qs && qs->objtype == QOBJ_FETCH)
            {
                char ri[64]; int ra = 0;
                if(repeatableget(name, qid, ri, sizeof(ri), &ra))
                {
                    formatstring(amount)("%d", ra);
                    int iid = finditem(ri);
                    copystring(item, iid >= 0 ? itemname(iid) : ri, sizeof(item));
                    have = itemcount(name, ri);
                    if(have > ra) have = ra;
                    formatstring(progress)("%d", have);
                    formatstring(target)("%d", ra);
                }
            }
            else if(qs && qs->objtype == QOBJ_KILL)
            {
                questprogress(name, qid, have, need);
                formatstring(progress)("%d", have);
                formatstring(target)("%d", need);
            }
        }
        const char *p = in;
        char *o = out;
        int left = outlen - 1;
        while(*p && left > 0)
        {
            const char *rep = NULL;
            int replen = 0;
            if(*p == '$')
            {
                if(!strncmp(p, "$progress", 9))      { rep = progress; replen = 9; }
                else if(!strncmp(p, "$target", 7))   { rep = target;   replen = 7; }
                else if(!strncmp(p, "$amount", 7))   { rep = amount;   replen = 7; }
                else if(!strncmp(p, "$item", 5))     { rep = item;     replen = 5; }
                else if(!strncmp(p, "$name", 5))     { rep = name;     replen = 5; }
            }
            if(rep)
            {
                int rl = (int)strlen(rep);
                if(rl > left) rl = left;
                memcpy(o, rep, rl); o += rl; left -= rl;
                p += replen;
            }
            else { *o++ = *p++; left--; }
        }
        *o = '\0';
    }
}
