// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "engine.h"
#include "version.h"

#ifdef STANDALONE

#ifdef RR_NEED_AUTH
extern "C"
{
    #include <tomcrypt.h>
}
#endif

#include "server/lua.h"

void fatal(const char *s, ...) 
{ 
    void cleanupserver();
    cleanupserver(); 
    defvformatstring(msg,s,s);
    conoutf("servererror: %s", msg); 
    exit(EXIT_FAILURE); 
}

static bool dateValid = false;
static string date = {0};
static time_t timeValue = {0};
static struct tm * timeinfo;

void updateDate()
{
    time (&timeValue);
    timeinfo = localtime (&timeValue);

    strftime(date, sizeof(date) - 1, "%Y-%m-%dT%H:%M:%S", timeinfo);
    dateValid = true;
}

void conoutfv(int type, const char *fmt, va_list args)
{
    while(*fmt == '\n' && fmt++) printf("\n");
    string sf, sp;
    vformatstring(sf, fmt, args);
    filtertext(sp, sf);

    if(!sf[0])
    {
        return;
    }

    if(!dateValid) updateDate();

    printf("[%s] %s\n", date, sp);
}


void conoutf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(CON_INFO, fmt, args);
    va_end(args);
}

void conoutf(int type, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    conoutfv(type, fmt, args);
    va_end(args);
}
#endif

#define DEFAULTCLIENTS 8

enum { ST_EMPTY, ST_LOCAL, ST_TCPIP };

struct client                   // server side version of "dynent" type
{
    int type;
    int num;
    ENetPeer *peer;
    string hostname;
    void *info;
};

vector<client *> clients;

ENetHost *serverhost = NULL;
int laststatus = 0; 
ENetSocket pongsock = ENET_SOCKET_NULL, lansock = ENET_SOCKET_NULL;

int localclients = 0, nonlocalclients = 0;

bool hasnonlocalclients() { return nonlocalclients!=0; }
bool haslocalclients() { return localclients!=0; }

client &addclient(int type)
{
    client *c = NULL;
    loopv(clients) if(clients[i]->type==ST_EMPTY)
    {
        c = clients[i];
        break;
    }
    if(!c)
    {
        c = new client;
        c->num = clients.length();
        clients.add(c);
    }
    c->info = server::newclientinfo();
    c->type = type;
    switch(type)
    {
        case ST_TCPIP: nonlocalclients++; break;
        case ST_LOCAL: localclients++; break;
    }
    return *c;
}

void delclient(client *c)
{
    if(!c) return;
    switch(c->type)
    {
        case ST_TCPIP: nonlocalclients--; if(c->peer) c->peer->data = NULL; break;
        case ST_LOCAL: localclients--; break;
        case ST_EMPTY: return;
    }
    c->type = ST_EMPTY;
    c->peer = NULL;
    if(c->info)
    {
        server::deleteclientinfo(c->info);
        c->info = NULL;
    }
}

void cleanupserver()
{
    if(serverhost) enet_host_destroy(serverhost);
    serverhost = NULL;

    if(pongsock != ENET_SOCKET_NULL) enet_socket_destroy(pongsock);
    if(lansock != ENET_SOCKET_NULL) enet_socket_destroy(lansock);
    pongsock = lansock = ENET_SOCKET_NULL;
}

VARF(maxclients, 0, DEFAULTCLIENTS, MAXCLIENTS, { if(!maxclients) maxclients = DEFAULTCLIENTS; });
VARF(maxdupclients, 0, 0, MAXCLIENTS, { if(serverhost) serverhost->duplicatePeers = maxdupclients ? maxdupclients : MAXCLIENTS; });

void process(ENetPacket *packet, int sender, int chan);
//void disconnect_client(int n, int reason);

int getservermtu() { return serverhost ? serverhost->mtu : -1; }
void *getclientinfo(int i) { return !clients.inrange(i) || clients[i]->type==ST_EMPTY ? NULL : clients[i]->info; }
ENetPeer *getclientpeer(int i) { return clients.inrange(i) && clients[i]->type==ST_TCPIP ? clients[i]->peer : NULL; }
int getnumclients()        { return clients.length(); }
uint getclientip(int n)    { return clients.inrange(n) && clients[n]->type==ST_TCPIP ? clients[n]->peer->address.host : 0; }

void sendpacket(int n, int chan, ENetPacket *packet, int exclude)
{
    if(n<0)
    {
        server::recordpacket(chan, packet->data, packet->dataLength);
        loopv(clients) if(i!=exclude && server::allowbroadcast(i)) sendpacket(i, chan, packet);
        return;
    }
    switch(clients[n]->type)
    {
        case ST_TCPIP:
        {
            enet_peer_send(clients[n]->peer, chan, packet);
            break;
        }

#ifndef STANDALONE
        case ST_LOCAL:
            localservertoclient(chan, packet);
            break;
#endif
    }
}

ENetPacket *sendf(int cn, int chan, const char *format, ...)
{
    int exclude = -1;
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    packetbuf p(MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'x':
            exclude = va_arg(args, int);
            break;

        case 'v':
        {
            int n = va_arg(args, int);
            int *v = va_arg(args, int *);
            loopi(n) putint(p, v[i]);
            break;
        }

        case 'i': 
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 'f':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putfloat(p, (float)va_arg(args, double));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'm':
        {
            int n = va_arg(args, int);
            if(*format == '+') putint(p, n);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);
    ENetPacket *packet = p.finalize();
    sendpacket(cn, chan, packet, exclude);
    return packet->referenceCount > 0 ? packet : NULL;
}

ENetPacket *sendfile(int cn, int chan, stream *file, const char *format, ...)
{
    if(cn < 0)
    {
#ifdef STANDALONE
        return NULL;
#endif
    }
    else if(!clients.inrange(cn)) return NULL;

    int len = (int)min(file->size(), stream::offset(INT_MAX));
    if(len <= 0 || len > 16<<20) return NULL;

    packetbuf p(MAXTRANS+len, ENET_PACKET_FLAG_RELIABLE);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
        case 'l': putint(p, len); break;
        case 'm':
        {
            int n = va_arg(args, int);
            if(*format == '+') putint(p, n);
            p.put(va_arg(args, uchar *), n);
            break;
        }
    }
    va_end(args);

    file->seek(0, SEEK_SET);
    file->read(p.subbuf(len).buf, len);

    ENetPacket *packet = p.finalize();
    if(cn >= 0) sendpacket(cn, chan, packet, -1);
#ifndef STANDALONE
    else sendclientpacket(packet, chan);
#endif
    return packet->referenceCount > 0 ? packet : NULL;
}

const char *disconnectreason(int reason)
{
    switch(reason)
    {
        case DISC_EOP: return "end of packet";
        case DISC_LOCAL: return "server is in local mode";
        case DISC_KICK: return "kicked/banned";
        case DISC_MSGERR: return "message error";
        case DISC_IPBAN: return "ip is banned";
        case DISC_PRIVATE: return "server is in private mode";
        case DISC_MAXCLIENTS: return "server FULL";
        case DISC_TIMEOUT: return "connection timed out";
        case DISC_OVERFLOW: return "overflow";
        case DISC_PASSWORD: return "invalid password";
        default: return NULL;
    }
}

void disconnect_client(int n, int reason)
{
    if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
    enet_peer_disconnect(clients[n]->peer, reason);
    server::clientdisconnect(n);
    delclient(clients[n]);
    const char *msg = disconnectreason(reason);
    string s;
    if(msg) formatstring(s)("client (%s) disconnected because: %s", clients[n]->hostname, msg);
    else formatstring(s)("client (%s) disconnected", clients[n]->hostname);
    conoutf("%s", s);
    server::sendservmsg(s);
}

void kicknonlocalclients(int reason)
{
    loopv(clients) if(clients[i]->type==ST_TCPIP) disconnect_client(i, reason);
}

void process(ENetPacket *packet, int sender, int chan)   // sender may be -1
{
    packetbuf p(packet);
    server::parsepacket(sender, chan, p);
    if(p.overread())
    {
        {
            packetbuf q (packet);
            
            conoutf("EOP received: ");
            while(q.remaining())
            {
                conoutf("%i ", getint(q));
            }
            conoutf("\n");
        }
        
        disconnect_client(sender, DISC_EOP);
        return;
    }
}

void localclienttoserver(int chan, ENetPacket *packet)
{
    client *c = NULL;
    loopv(clients) if(clients[i]->type==ST_LOCAL) { c = clients[i]; break; }
    if(c) process(packet, c->num, chan);
}

ENetAddress serveraddress = { ENET_HOST_ANY, ENET_PORT_ANY };

client &addclient()
{
    loopv(clients) if(clients[i]->type==ST_EMPTY)
    {
        clients[i]->info = server::newclientinfo();
        return *clients[i];
    }
    client *c = new client;
    c->num = clients.length();
    c->info = server::newclientinfo();
    clients.add(c);
    return *c;
}

static ENetAddress pongaddr;

void sendserverinforeply(ucharbuf &p)
{
    ENetBuffer buf;
    buf.data = p.buf;
    buf.dataLength = p.length();
    enet_socket_send(pongsock, &pongaddr, &buf, 1);
}

#define MAXPINGDATA 32

void checkserversockets()        // reply all server info requests
{
    static ENetSocketSet sockset;
    ENET_SOCKETSET_EMPTY(sockset);
    ENetSocket maxsock = pongsock;
    ENET_SOCKETSET_ADD(sockset, pongsock);
    if(lansock != ENET_SOCKET_NULL)
    {
        maxsock = max(maxsock, lansock);
        ENET_SOCKETSET_ADD(sockset, lansock);
    }
    if(enet_socketset_select(maxsock, &sockset, NULL, 0) <= 0) return;

    ENetBuffer buf;
    uchar pong[MAXTRANS];
    loopi(2)
    {
        ENetSocket sock = i ? lansock : pongsock;
        if(sock == ENET_SOCKET_NULL || !ENET_SOCKETSET_CHECK(sockset, sock)) continue;

        buf.data = pong;
        buf.dataLength = sizeof(pong);
        int len = enet_socket_receive(sock, &pongaddr, &buf, 1);
        if(len < 0 || len > MAXPINGDATA) continue;
        ucharbuf req(pong, len), p(pong, sizeof(pong));
        p.len += len;
        server::serverinforeply(req, p);
        string hn;
        conoutf("Extinfo: Received request from %s", enet_address_get_host_ip(&pongaddr, hn, sizeof(hn))==0 ? hn : "unknown");
    }
}

VAR(serveruprate, 0, 0, INT_MAX);
SVAR(serverip, "");
VARF(serverport, 0, server::serverport(), 0xFFFF-1, { if(!serverport) serverport = server::serverport(); });

#ifdef STANDALONE
int curtime = 0, lastmillis = 0, elapsedtime = 0, totalmillis = 0;
#endif

uint totalsecs = 0;

void updatetime()
{
    static int lastsec = 0;
    if(totalmillis - lastsec >= 1000) 
    {
        int cursecs = (totalmillis - lastsec) / 1000;
        totalsecs += cursecs;
        lastsec += cursecs * 1000;
    }
}

void serverslice(bool dedicated, uint timeout)   // main server update, called from main loop in sp, or from below in dedicated server
{
    if(!serverhost) 
    {
        server::serverupdate();
        server::sendpackets();
        return;
    }

    // below is network only

#ifndef SERVER
    if(dedicated) 
    {
#endif
        int millis = (int)enet_time_get();
        elapsedtime = millis - totalmillis;
        static int timeerr = 0;
        int scaledtime = server::scaletime(elapsedtime) + timeerr;
        curtime = scaledtime/100;
        timeerr = scaledtime%100;
        if(server::ispaused()) curtime = 0;
        lastmillis += curtime;
        totalmillis = millis;
        updatetime();
#ifndef SERVER
    }
#endif

#ifdef SERVER
    uv_loop_t *loop = uv_default_loop();
    dateValid = false;
    uv_run(loop, UV_RUN_NOWAIT);
#endif

    server::serverupdate();

    checkserversockets();

    if(totalmillis-laststatus>60*1000)   // display bandwidth stats, useful for server ops
    {
        laststatus = totalmillis;     
        if(nonlocalclients || serverhost->totalSentData || serverhost->totalReceivedData) conoutf("status: %d remote clients, %.1f send, %.1f rec (K/sec)", nonlocalclients, serverhost->totalSentData/60.0f/1024, serverhost->totalReceivedData/60.0f/1024);
        serverhost->totalSentData = serverhost->totalReceivedData = 0;
    }

    ENetEvent event;
    bool serviced = false;
    while(!serviced)
    {
#ifdef SERVER
        uv_run(loop, UV_RUN_NOWAIT);
        if(enet_host_check_events(serverhost, &event) <= 0)
        {
            int uvTimeout = uv_loop_alive(loop) ? uv_backend_timeout(loop) : -1;
            if(uvTimeout != -1) timeout = min(timeout, (uint)uvTimeout);
            if(enet_host_service(serverhost, &event, timeout) <= 0) break;
            serviced = true;
        }
#else
        if(enet_host_check_events(serverhost, &event) <= 0)
        {
            if(enet_host_service(serverhost, &event, timeout) <= 0) break;
            serviced = true;
        }
#endif
        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                client &c = addclient(ST_TCPIP);
                c.peer = event.peer;
                c.peer->data = &c;
                string hn;
                copystring(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
                conoutf("client connected (%s)", c.hostname);
                int reason = server::clientconnect(c.num, c.peer->address.host);
                if(reason) disconnect_client(c.num, reason);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
            {
                client *c = (client *)event.peer->data;
                if(c) process(event.packet, c->num, event.channelID);
                if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: 
            {
                client *c = (client *)event.peer->data;
                if(!c) break;
                conoutf("disconnected client (%s)", c->hostname);
                server::clientdisconnect(c->num);
                delclient(c);
                break;
            }
            default:
                break;
        }
    }
    if(server::sendpackets()) enet_host_flush(serverhost);
}

void flushserver(bool force)
{
    if(server::sendpackets(force) && serverhost) enet_host_flush(serverhost);
}

#ifndef STANDALONE
void localdisconnect(bool cleanup)
{
    bool disconnected = false;
    loopv(clients) if(clients[i]->type==ST_LOCAL) 
    {
        server::localdisconnect(i);
        delclient(clients[i]);
        disconnected = true;
    }
    if(!disconnected) return;
    game::gamedisconnect(cleanup);
    mainmenu = 1;
}

void localconnect()
{
    client &c = addclient(ST_LOCAL);
    copystring(c.hostname, "local");
    game::gameconnect(false);
    server::localconnect(c.num);
}
#endif

bool servererror(bool dedicated, const char *desc)
{
#ifndef STANDALONE
    if(!dedicated)
    {
        conoutf(CON_ERROR, "%s", desc);
        cleanupserver();
    }
    else
#endif
        fatal("%s", desc);
    return false;
}
  
bool setuplistenserver(bool dedicated)
{
    ENetAddress address = { ENET_HOST_ANY, enet_uint16(serverport <= 0 ? server::serverport() : serverport) };
    if(*serverip)
    {
        if(enet_address_set_host(&address, serverip)<0) conoutf(CON_WARN, "WARNING: server ip not resolved");
        else serveraddress.host = address.host;
    }
    serverhost = enet_host_create(&address, min(maxclients + server::reserveclients(), MAXCLIENTS), server::numchannels(), 0, serveruprate);
    if(!serverhost) return servererror(dedicated, "could not create server host");
    serverhost->duplicatePeers = maxdupclients ? maxdupclients : MAXCLIENTS;
    address.port = server::serverinfoport(serverport > 0 ? serverport : -1);
    pongsock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(pongsock != ENET_SOCKET_NULL && enet_socket_bind(pongsock, &address) < 0)
    {
        enet_socket_destroy(pongsock);
        pongsock = ENET_SOCKET_NULL;
    }
    if(pongsock == ENET_SOCKET_NULL) return servererror(dedicated, "could not create server info socket");
    else enet_socket_set_option(pongsock, ENET_SOCKOPT_NONBLOCK, 1);
    address.port = server::laninfoport();
    lansock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if(lansock != ENET_SOCKET_NULL && (enet_socket_set_option(lansock, ENET_SOCKOPT_REUSEADDR, 1) < 0 || enet_socket_bind(lansock, &address) < 0))
    {
        enet_socket_destroy(lansock);
        lansock = ENET_SOCKET_NULL;
    }
    if(lansock == ENET_SOCKET_NULL) conoutf(CON_WARN, "WARNING: could not create LAN server info socket");
    else enet_socket_set_option(lansock, ENET_SOCKOPT_NONBLOCK, 1);
    
#ifdef SERVER
    if(lua::pushEvent("server.listen"))
    {
        lua_pushnumber(lua::L, serverhost->address.port);
        
        ENetAddress addr;
        enet_socket_get_address (pongsock, &addr);
        lua_pushnumber(lua::L, addr.port);
        
        enet_socket_get_address (lansock, &addr);
        lua_pushnumber(lua::L, addr.port);
        lua_call(lua::L, 3, 0);
    }
#endif
    
    return true;
}

void initserver(bool listen, bool dedicated)
{
    server::serverinit();
    
#ifndef STANDALONE
    if(listen && !dedicated) conoutf("listen server started");
#endif
    
}

#ifndef STANDALONE
void startlistenserver(int *usemaster)
{
    if(serverhost) { conoutf(CON_ERROR, "listen server is already running"); return; }

    if(!setuplistenserver(false)) return;
    
    conoutf("listen server started for %d clients", maxclients); 
}
COMMAND(startlistenserver, "i");

void stoplistenserver()
{
    if(!serverhost) { conoutf(CON_ERROR, "listen server is not running"); return; }

    kicknonlocalclients();
    enet_host_flush(serverhost);
    cleanupserver();

    conoutf("listen server stopped");
}
COMMAND(stoplistenserver, "");
#endif

#ifdef CLIENT
bool serveroption(char *opt)
{
    switch(opt[1])
    {
        case 'u': setvar("serveruprate", atoi(opt+2)); return true;
        case 'c': setvar("maxclients", atoi(opt+2)); return true;
        case 'i': setsvar("serverip", opt+2); return true;
        case 'j': setvar("serverport", atoi(opt+2)); return true; 
        case 'q': conoutf("Using home directory: %s", opt+2); sethomedir(opt+2); return true;
        case 'k': conoutf("Adding package directory: %s", opt+2); addpackagedir(opt+2); return true;
        default: return false;
    }
}
#endif

vector<const char *> gameargs;

#ifdef SERVER

static bool rundedicated = false;

void quit()
{
    conoutf("Stopping server");

    if(lua::pushEvent("server.stop"))
    {
        lua_call(lua::L, 0, 0);
    }
}

volatile int shouldTerminate = 0;

void checkTerminate()
{
    if(shouldTerminate)
    {
    #ifndef WIN32
        conoutf("\nCaught signal %i (%s)", shouldTerminate, strsignal(shouldTerminate));
    #else
        conoutf("\nCaught signal %i (<Name function not implemented by OS>)", shouldTerminate);
    #endif
        shouldTerminate = 0;
        quit();
    }
}

void server_sigint(int signal)
{
    shouldTerminate = signal;
}

extern "C"
{
    EXPORT(void startServer())
    {
        initserver(true, true);
        setuplistenserver(true);

        #ifdef WIN32
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif

        if(lua::pushEvent("server.start"))
        {
            lua_call(lua::L, 0, 0);
        }
        
        rundedicated = true;
    }
    
    EXPORT(void stopServer())
    {
        quit();
    }
    
    EXPORT(void shutdownFinished())
    {
        rundedicated = false;

        if(lua::pushEvent("server.afterStop"))
        {
            lua_call(lua::L, 0, 0);
        }
        
        uv_stop(uv_default_loop());
    }
    
    #undef EXPORT
}

int main(int argc, const char **argv)
{   
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);
    
    #ifdef RR_NEED_AUTH
    ltc_mp = ltm_desc;
    
    {
        int status;
        status = register_prng(&sprng_desc);
        if(status != CRYPT_OK)
        {
            fatal("Error registering sprng: %s", error_to_string(status));
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

    findRootDirectory("resources");

    lua::init(argc, argv); //Lua will start the server

    signal(SIGINT, server_sigint);
    signal(SIGTERM, server_sigint);
    signal(SIGHUP, server_sigint);

    do
    {
        for(;rundedicated;)
        {
            checkTerminate();
            serverslice(true, 4);
        }
    }
    while(uv_run(uv_default_loop(), UV_RUN_ONCE));
    
    lua::close();
    
    return 0;
}
#endif
