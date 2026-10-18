#include "game.h"

#ifdef CLIENT
extern void aliasa(const char *name, char *action);
extern void pushident(ident &id, char *val);
extern void popident(ident &id);

ICOMMAND(loopGamemodes, "ss", (char *var, char *body), {
    ident *id = newident(var);
    loopstart(id, stack);


    string gamemodeInfo;

    RR_FOREACH_GAMEMODE
    {
        const GameMode &gm = gameModes[i];
        formatstring(gamemodeInfo)("%i %i \"%s\" \"%s\" \"%s\"", gm.id, gm.flags, gm.name, gm.shortName, gm.description ? gm.description : "");

        loopiter(id, stack, gamemodeInfo);
        execute(body);
    }

    loopend(id, stack);
});

#endif