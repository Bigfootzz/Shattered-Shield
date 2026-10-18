local ffi = require "ffi"

ffi.cdef [[
    void sendServerMessage(const char *msg);
    void sendServerMessageTo(int cn, const char *msg);
    void startServer();
    void shutdownFinished();
    int getPrivilege(int cn);
    unsigned long getIp(int cn);
    const char *getDisplayName(int cn);
    void sendMapTo(int cn);

    struct GameMode
    {
        int id;
        int flags;
        const char *name;
        const char *shortName;
        const char *description;
    };

    int getGameModeId();
    struct GameMode *getGameModeInfo(int id);

    const char *getMapName();
    int getPlayerCount();
    int getTimeLeft();
    const char *getVersionString();
    const char *getVersionDate();
    int getProtocolVersion();

    bool checkpassword(int cn, const char *wanted, const char *given);

    void        set_dbgfs(int dbgfs);
    int         get_dbgfs();
    
    void        set_maxclients(int maxclients);
    int         get_maxclients();
    
    void        set_serveruprate(int serveruprate);
    int         get_serveruprate();
    
    void        set_serverip(const char *serverip);
    const char *get_serverip();
    
    void        set_serverport(int serverport);
    int         get_serverport();
    
    void        set_quakemillis(int quakemillis);
    int         get_quakemillis();
    
    void        set_demodir(const char *demodir);
    const char *get_demodir();
    
    void        set_localrecorddemo(int localrecorddemo);
    int         get_localrecorddemo();
    
    void        set_serverpass(const char *serverpass);
    const char *get_serverpass();
    
    void        set_adminpass(const char *adminpass);
    const char *get_adminpass();
    
    void        set_publicserver(int publicserver);
    int         get_publicserver();
    
    void        set_maxzombies(int maxzombies);
    int         get_maxzombies();

    void        set_flaglimit(int flaglimit);
    int         get_flaglimit();

    void        set_timelimit(int timelimit);
    int         get_timelimit();

    void        set_allowweaps(int allowweaps);
    int         get_allowweaps();

    void        set_botlimit(int serverbotlimit);
    int         get_botlimit();

    void        set_botbalance(int serverbotbalance);
    int         get_botbalance();
    
    void        set_serverdesc(const char *serverdesc);
    const char *get_serverdesc();
]]

return ffi.C