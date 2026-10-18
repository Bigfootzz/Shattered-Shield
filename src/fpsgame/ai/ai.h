struct fpsent;

#define MAXBOTS 32

namespace ai
{
    const int MAXWAYPOINTS = USHRT_MAX - 2;
    const int MAXWAYPOINTLINKS = 6;
    const int WAYPOINTRADIUS = 16;

    const float MINWPDIST       = 4.f;     // is on top of
    const float CLOSEDIST       = 32.f;    // is close
    const float FARDIST         = 128.f;   // too far to remap close
    const float JUMPMIN         = 4.f;     // decides to jump
    const float JUMPMAX         = 32.f;    // max jump
    const float SIGHTMIN        = 64.f;    // minimum line of sight
    const float SIGHTMAX        = 1024.f;  // maximum line of sight
    const float VIEWMIN         = 90.f;    // minimum field of view
    const float VIEWMAX         = 180.f;   // maximum field of view

    struct waypoint
    {
        vec o;
        float curscore, estscore;
        int weight;
        ushort route, prev;
        ushort links[MAXWAYPOINTLINKS];

        waypoint() {}
        waypoint(const vec &o, int weight = 0) : o(o), weight(weight), route(0) { memset(links, 0, sizeof(links)); }

        int score() const { return int(curscore) + int(estscore); }

        int find(int wp)
        {
            loopi(MAXWAYPOINTLINKS) if(links[i] == wp) return i;
            return -1;
        }
		bool haslinks() { return links[0]!=0; }
    };
    extern vector<waypoint> waypoints;

    static inline bool iswaypoint(int n)
    {
        return n > 0 && n < waypoints.length();
    }

    extern int showwaypoints, dropwaypoints;
    extern int closestwaypoint(const vec &pos, float mindist, bool links, fpsent *d = NULL);
    extern void findwaypointswithin(const vec &pos, float mindist, float maxdist, vector<int> &results);
    extern void inferwaypoints(fpsent *d, const vec &o, const vec &v, float mindist = ai::CLOSEDIST);

    struct avoidset
    {
        struct obstacle
        {
            void *owner;
            int numwaypoints;
            float above;

            obstacle(void *owner, float above = -1) : owner(owner), numwaypoints(0), above(above) {}
        };

        vector<obstacle> obstacles;
        vector<int> waypoints;

        void clear()
        {
            obstacles.setsize(0);
            waypoints.setsize(0);
        }

        void add(void *owner, float above)
        {
            obstacles.add(obstacle(owner, above));
        }

        void add(void *owner, float above, int wp)
        {
            if(obstacles.empty() || owner != obstacles.last().owner) add(owner, above);
            obstacles.last().numwaypoints++;
            waypoints.add(wp);
        }

        void add(avoidset &avoid)
        {
            waypoints.put(avoid.waypoints.getbuf(), avoid.waypoints.length());
            loopv(avoid.obstacles)
            {
                obstacle &o = avoid.obstacles[i];
				if(obstacles.empty() || o.owner != obstacles.last().owner) add(o.owner, o.above);
                obstacles.last().numwaypoints += o.numwaypoints;
            }
        }

        void avoidnear(void *owner, float above, const vec &pos, float limit);

        #define loopavoid(v, d, body) \
            if(!(v).obstacles.empty()) \
            { \
                int cur = 0; \
                loopv((v).obstacles) \
                { \
                    const ai::avoidset::obstacle &ob = (v).obstacles[i]; \
                    int next = cur + ob.numwaypoints; \
                    if(ob.owner != d) \
                    { \
                        for(; cur < next; cur++) \
                        { \
                            int wp = (v).waypoints[cur]; \
                            body; \
                        } \
                    } \
                    cur = next; \
                } \
            }

        bool find(int n, fpsent *d) const
        {
            loopavoid(*this, d, { if(wp == n) return true; });
            return false;
        }

        int remap(fpsent *d, int n, vec &pos, bool retry = false);
    };

    extern bool route(fpsent *d, int node, int goal, vector<int> &route, const avoidset &obstacles, int retries = 0);
    extern void navigate();
    extern void clearwaypoints(bool full = false);
    extern void seedwaypoints();
    extern void loadwaypoints(bool force = false, const char *mname = NULL);
    extern void savewaypoints(bool force = false, const char *mname = NULL);

    enum AiType
    {
        AI_TYPE_NONE = 0,
        AI_TYPE_BOT,
        AI_TYPE_MONSTER,
        AI_TYPE_NUM
    };

    struct AiState;

    struct AiInterface
    {
        virtual void destroy(fpsent *d) = 0;
        virtual void create(fpsent *d) = 0;
        virtual void respawn(fpsent *d) {}

        virtual void clearState(AiState *state) {};
        virtual void killed(fpsent *d, fpsent *e) {};
        virtual void itemSpawned(fpsent *d, int ent) {};

        virtual void startThink() {};
        virtual void think(fpsent *d, bool run) {};
        virtual void endThink() {};

        /**
         * For SP AI, should put all ai back to its initial position when editing is toggled
         */
        virtual void reset() {};

        virtual void collide(fpsent *d, physent *o, const vec &dir) {};

        virtual void render(fpsent *d, float i) {};
    };

    //TODO: allow server-only AIs
#ifndef STANDALONE
    extern AiInterface * const getAiType(AiType type);
    extern AiInterface * const getAiType(fpsent *d);

    #define AI_MULTI_DISPATCH(callback) MACRO_BODY({ \
    for(int aiType = (int)::ai::AI_TYPE_NONE; aiType < (int)::ai::AI_TYPE_NUM; aiType++) { \
            ::ai::AiInterface *ai = ::ai::getAiType(::ai::AiType(aiType)); \
            callback; \
        } \
    })
#endif

    struct AiState
    {
        AiType type;
        int skill;
        bool local;
        void *data;

        AiState() : type(AI_TYPE_NONE), skill(0), local(false), data(NULL) {}

        ~AiState()
        {
#ifndef STANDALONE
            getAiType(type)->clearState(this);
#endif
        }
    };


    struct DummyAi : AiInterface
    {
        void destroy(fpsent *d);
        void create(fpsent *d);
    };

    struct AiInfo
    {
        AiType type;
        AiInfo(AiType type) : type(type) {}

        static const int NUMPREVNODES = 6;
        int prevnodes[NUMPREVNODES];

        virtual void clear(bool prev)
        {
            if(prev) memset(prevnodes, -1, sizeof(prevnodes));
        }

        bool hasprevnode(int n) const
        {
            loopi(NUMPREVNODES) if(prevnodes[i] == n) return true;
            return false;
        }

        void addprevnode(int n)
        {
            if(prevnodes[0] != n)
            {
                memmove(&prevnodes[1], prevnodes, sizeof(prevnodes) - sizeof(prevnodes[0]));
                prevnodes[0] = n;
            }
        }

    };

    // Api for AI implementations
    extern int forcegun, aidebug;
    extern avoidset obstacles;
    extern vec aitarget;

    extern float viewdist(int x = 101);
    extern float viewfieldx(int x = 101);
    extern float viewfieldy(int x = 101);
    extern bool targetable(fpsent *d, fpsent *e);
    extern bool badhealth(fpsent *d);

    inline void normalizeYaw(float &yaw, float angle)
    {
        while(yaw<angle-180.0f) yaw += 360.0f;
        while(yaw>angle+180.0f) yaw -= 360.0f;
    }

    extern bool canmove(fpsent *d);
    extern bool getsight(vec &o, float yaw, float pitch, vec &q, vec &v, float mdist, float fovx, float fovy);
    extern bool weaprange(fpsent *d, int weap, float dist);
    extern bool hasgoodammo(fpsent *d);
    extern void findorientation(vec &o, float yaw, float pitch, vec &pos);
    extern void fixrange(float &yaw, float &pitch);
    extern void getyawpitch(const vec &from, const vec &pos, float &yaw, float &pitch);
    extern bool lockon(fpsent *d, fpsent *e, float maxdist);
    extern void scaleyawpitch(float &yaw, float &pitch, float targyaw, float targpitch, float frame, float scale);
    extern bool canshoot(fpsent *d, fpsent *e);
    extern bool isgoodammo(int gun);
    ///Api

    extern void init(fpsent *d, AiType aiType, int on, int sk, int bn, int pm, int pc, const char *name, const char *team);
    extern void update();
    extern void avoid();

    extern void spawned(fpsent *d);
    extern void damaged(fpsent *d, fpsent *e);
    extern void killed(fpsent *d, fpsent *e);
    extern void itemspawned(int ent);

    void requestAdd(AiType type = AI_TYPE_BOT, int skill = -1);
    void requestDel(AiType type = AI_TYPE_BOT);
    
    extern void render();
}

#include "bot.h"
#include "monster.h"
