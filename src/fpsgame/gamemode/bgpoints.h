// Battleground point modes (Nov 6): clientmode for KOTH (17) and TFF (18).
// Client-only: the server runs its point logic in server.cpp bg_ code and
// needs no servmode (spawn positions are picked client-side, team scores
// arrive via N_BG_POINT). Kept minimal on purpose:
//  - pickspawn: team-tagged playerstarts, same rule as CTF (ctfteamflag
//    equivalent via isteam). Without this, cmode is NULL and stock
//    findplayerspawn runs with tag 0, which matches NO teamed spawn on
//    these maps (all spawns carry attr2 1/2) and falls back to map center
//    — players would spawn falling from the sky over the middle.
//  - hidefrags + getteamscore(s): the Tab scoreboard shows the mode score
//    (KOTH hold seconds, TFF points) instead of frag sums. The scoreboard
//    already branches on hidefrags/getteamscore, so no scoreboard edit.
//  - getBotGameMode stays NULL (base default): bots get generic AI, the
//    same as any non-CTF mode; bot.cpp null-checks the pointer.
#ifndef BGPOINTS_H
#define BGPOINTS_H

struct bgpointclientmode : clientmode
{
    void pickspawn(fpsent *d)
    {
        int tag = isteam(d->team, TEAM_0) ? 1 : (isteam(d->team, TEAM_1) ? 2 : 0);
        // Battleground maps only: a teamed player with no matching spawn
        // (neutral/any spawns, attr2 0) would fall back to map center;
        // retry with any playerstart instead. Mirrors the CTF pickspawn
        // fallback (ctf.h, v0.1.40) so new maps stay playable.
        if(rpg::mapactive(getclientmap()))
        {
            bool anymatch = false;
            const vector<extentity *> &ents = entities::getents();
            loopv(ents) if(ents[i]->type == ET_PLAYERSTART && (tag < 0 || ents[i]->attr2 == tag)) { anymatch = true; break; }
            if(!anymatch) tag = -1;
        }
        findplayerspawn(d, -1, tag);
    }

    bool hidefrags() { return true; }
    int getteamscore(const char *team) { int s = bg_team_score_for(team); return s >= 0 ? s : 0; }
    void getteamscores(vector<teamscore> &scores)
    {
        scores.add(teamscore(TEAM_0, getteamscore(TEAM_0)));
        scores.add(teamscore(TEAM_1, getteamscore(TEAM_1)));
    }
    int getteamscoreflag(int team) { return team == 1 ? getteamscore(TEAM_0) : team == 2 ? getteamscore(TEAM_1) : 0; }
};

extern bgpointclientmode bgpointmode;

#endif
