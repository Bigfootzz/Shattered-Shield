// the interface the engine uses to run the gameplay module

namespace entities
{
    extern void editent(int i, bool local);
    extern const char *entnameinfo(entity &e);
    extern const char *entname(int i);
    extern int extraentinfosize();
    extern void writeent(entity &e, char *buf);
    extern void readent(entity &e, char *buf, int ver);
    extern float dropheight(entity &e);
    extern void fixentity(extentity &e);
    extern void entradius(extentity &e, bool color);
    extern bool mayattach(extentity &e);
    extern bool attachent(extentity &e, extentity &a);
    extern bool printent(extentity &e, char *buf);
    extern extentity *newentity();
    extern void deleteentity(extentity *e);
    extern void clearents();
    extern vector<extentity *> &getents();
    extern const char *entmodel(const entity &e);
    extern void animatemapmodel(const extentity &e, int &anim, int &basetime);
}

namespace game
{
    extern void parseoptions(vector<const char *> &args);

    extern void gamedisconnect(bool cleanup);
    extern void parsepacketclient(int chan, packetbuf &p);
    extern void connectattempt(const char *name, const char *password, const ENetAddress &address);
    extern void connectfail();
    extern void gameconnect(bool _remote);
    extern bool allowedittoggle();
    extern void edittoggled(bool on);
    extern void writeclientinfo(stream *f);
    extern void toserver(char *text);
    extern void sayteam(char *text);
    extern void changemap(const char *name);
    extern void forceedit(const char *name);
    extern bool ispaused();
    extern int scaletime(int t);
    extern bool allowmouselook();

    extern const char *gameident();
    extern const char *savedconfig();
    extern const char *restoreconfig();
    extern const char *defaultconfig();
    extern const char *autoexec();
    extern void loadconfigs();
    extern const char *getentname(dynent *d);

    extern void updateworld();
    extern void initclient();
    extern void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material = 0);
    extern void bounced(physent *d, const vec &surface);
    extern void edittrigger(const selinfo &sel, int op, int arg1 = 0, int arg2 = 0, int arg3 = 0);
#ifdef CLIENT
    extern void vartrigger(ident *id);
#endif
    extern void dynentcollide(physent *d, physent *o, const vec &dir);
    extern const char *getclientmap();
    extern const char *getmapinfo();
    extern void resetgamestate();
    // RR -> Different suicide types
    enum
    {
        SUICIDE_TYPE_GENERIC = 0,
        SUICIDE_TYPE_FALL,
        SUICIDE_TYPE_MAT,
        SUICIDE_TYPE_LAVA,
        SUICIDE_TYPE_GAME
    };
    extern void suicide(physent *d, int type = SUICIDE_TYPE_GENERIC);
    ///RR
    extern void newmap(int size);
    extern void startmap(const char *name);
    extern void preload();

    extern void drawSprite(int sprite, int x, int y, int w, int h); // RR -> Inagme icons
    extern void drawquad(float x, float y, float w, float h, float tx1 = 0, float ty1 = 0, float tx2 = 1, float ty2 = 1, bool flipx = false, bool flipy = false);
    extern void drawtexture(float x, float y, float w, float h, bool flipx = false, bool flipy = false);
    extern void drawsized(float x, float y, float s, bool flipx = false, bool flipy = false);
    extern void drawblend(int x, int y, int w, int h, float r, float g, float b, bool blend = false);
    extern void drawspinner(const char *tex, float yaw, int x, int y, float s, float blend = 1, const vec &colour = vec(1, 1, 1));
    extern float abovegameplayhud(int w, int h);
    extern void gameplayhud(int w, int h);

    extern bool canjump();
    extern bool allowmove(physent *d);
    extern void doattack(bool on, bool altfire);
    extern dynent *iterdynents(int i);
    extern int numdynents();
    extern void rendergame(bool mainpass);
    extern void renderavatar();
    extern void renderplayerpreview(int model, int team, int weap);
    extern void writegamedata(vector<char> &extras);
    extern void readgamedata(vector<char> &extras);
    extern int clipconsole(int w, int h);
    extern void g3d_gamemenus();
    extern const char *getCrosshairName(int index);
    extern int selectcrosshair(float &r, float &g, float &b, float &f);
    extern void lighteffects(dynent *d, vec &color, vec &dir);
    extern void setupcamera();
    extern bool detachcamera();
    extern bool collidecamera();
    extern dynent *followcam();
    extern void showdeathscores();
    extern void adddynlights();
    extern void particletrack(physent *owner, vec &o, vec &d);
    extern void dynlighttrack(physent *owner, vec &o, vec &hud);
    extern bool needminimap(bool cond = true);
    extern bool isscopedweap(int weap = -1);
    extern const char *weapsquinttex();
    extern bool isinfected();
    extern float idlepulse(float scale = 1.f);
    extern bool ismoving(bool move = true, bool strafe = true);
}

namespace server
{
    extern void *newclientinfo();
    extern void deleteclientinfo(void *ci);
    extern void serverinit();
    extern int reserveclients();
    extern int numchannels();
    extern void clientdisconnect(int n);
    extern int clientconnect(int n, uint ip);
    extern void localdisconnect(int n);
    extern void localconnect(int n);
    extern bool allowbroadcast(int n);
    extern void recordpacket(int chan, void *data, int len);
    extern void parsepacket(int sender, int chan, packetbuf &p);
    extern void sendservmsg(const char *s);
    extern void sendservmsgf(int cn, const char *f, ...);
    extern bool sendpackets(bool force = false);
    extern void serverinforeply(ucharbuf &req, ucharbuf &p);
    extern void serverupdate();
    extern bool servercompatible(char *name, char *sdec, char *map, int ping, const vector<int> &attr, int np);
    extern int laninfoport();
    extern int serverinfoport(int servport = -1);
    extern int serverport(int infoport = -1);
    extern const char *defaultmaster();
    extern int masterport();
    extern void processmasterinput(const char *cmd, int cmdlen, const char *args);
    extern bool ispaused();
    extern int protocolversion();
    extern int scaletime(int t);
}

