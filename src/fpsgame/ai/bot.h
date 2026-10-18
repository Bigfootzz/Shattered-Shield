namespace ai
{
    namespace bot
    {
        // ai state information for the owner client
        enum
        {
            AI_S_WAIT = 0,      // waiting for next command
            AI_S_DEFEND,        // defend goal target
            AI_S_PURSUE,        // pursue goal target
            AI_S_INTEREST,      // interest in goal entity
            AI_S_MAX
        };
        
        enum
        {
            AI_T_NODE,
            AI_T_PLAYER,
            AI_T_AFFINITY,
            AI_T_ENTITY,
            AI_T_MAX
        };
        
        struct interest
        {
            int state, node, target, targtype;
            float score;
            interest() : state(-1), node(-1), target(-1), targtype(-1), score(0.f) {}
            ~interest() {}
        };
        
        struct aistate
        {
            int type, millis, targtype, target, idle;
            bool override;
            
            aistate(int m, int t, int r = -1, int v = -1) : type(t), millis(m), targtype(r), target(v)
            {
                reset();
            }
            ~aistate() {}
            
            void reset()
            {
                idle = 0;
                override = false;
            }
        };

        
        struct BotAiInfo : AiInfo
        {
            vector<aistate> state;
            vector<int> route;
            vec target, spot;
            int enemy, enemyseen, enemymillis, weappref, targnode, targlast, targtime, targseq,
            lastrun, lasthunt, lastaction, lastcheck, jumpseed, jumprand, blocktime, huntseq, blockseq, lastaimrnd;
            float targyaw, targpitch, views[3], aimrnd[3];
            bool dontmove, becareful, tryreset, trywipe;
            
            BotAiInfo() : AiInfo(AI_TYPE_BOT)
            {
                clearsetup();
                reset();
                loopk(3) views[k] = 0.f;
            }
            ~BotAiInfo() {}
            
            void clearsetup()
            {
                weappref = WEAP_PISTOL;
                spot = target = vec(0, 0, 0);
                lastaction = lasthunt = lastcheck = enemyseen = enemymillis = blocktime = huntseq = blockseq = targtime = targseq = lastaimrnd = 0;
                lastrun = jumpseed = lastmillis;
                jumprand = lastmillis+5000;
                targnode = targlast = enemy = -1;
            }
            
            void clear(bool prev = false)
            {
                AiInfo::clear(prev);
                route.setsize(0);
            }
            
            void wipe(bool prev = false)
            {
                clear(prev);
                state.setsize(0);
                addstate(AI_S_WAIT);
                trywipe = false;
            }
            
            void clean(bool tryit = false)
            {
                if(!tryit) becareful = dontmove = false;
                targyaw = rnd(360);
                targpitch = 0.f;
                tryreset = tryit;
            }
            
            void reset(bool tryit = false) { wipe(); clean(tryit); }
            
            aistate &addstate(int t, int r = -1, int v = -1)
            {
                return state.add(aistate(lastmillis, t, r, v));
            }
            
            void removestate(int index = -1)
            {
                if(index < 0) state.pop();
                else if(state.inrange(index)) state.remove(index);
                if(!state.length()) addstate(AI_S_WAIT);
            }
            
            aistate &getstate(int idx = -1)
            {
                if(state.inrange(idx)) return state[idx];
                return state.last();
            }
            
            aistate &switchstate(aistate &b, int t, int r = -1, int v = -1)
            {
                if((b.type == t && b.targtype == r) || (b.type == AI_S_INTEREST && b.targtype == AI_T_NODE))
                {
                    b.millis = lastmillis;
                    b.target = v;
                    b.reset();
                    return b;
                }
                return addstate(t, r, v);
            }
        };

        struct BotAi : AiInterface
        {
            void destroy(fpsent *d);
            void create(fpsent *d);
            void spawned(fpsent *d);
            void killed(fpsent *d, fpsent *e);
            void itemSpawned(fpsent *d, int ent);
            void think(fpsent *d, bool run);
            void render(fpsent *d, float i);
        };

        extern BotAiInfo *getBotState(fpsent *d);

        extern bool cansee(fpsent *d, vec &x, vec &y, vec &targ = aitarget);

        extern bool checkothers(vector<int> &targets, fpsent *d = NULL, int state = -1, int targtype = -1, int target = -1, bool teams = false, int *members = NULL);
        extern bool makeroute(fpsent *d, aistate &b, int node, bool changed = true, int retries = 0);
        extern bool makeroute(fpsent *d, aistate &b, const vec &pos, bool changed = true, int retries = 0);
        extern bool randomnode(fpsent *d, aistate &b, const vec &pos, float guard = SIGHTMIN, float wander = SIGHTMAX);
        extern bool randomnode(fpsent *d, aistate &b, float guard = SIGHTMIN, float wander = SIGHTMAX);
        extern bool violence(fpsent *d, aistate &b, fpsent *e, int pursue = 0);
        extern bool patrol(fpsent *d, aistate &b, const vec &pos, float guard = SIGHTMIN, float wander = SIGHTMAX, int walk = 1, bool retry = false);
        extern bool defend(fpsent *d, aistate &b, const vec &pos, float guard = SIGHTMIN, float wander = SIGHTMAX, int walk = 1);
        extern void assist(fpsent *d, aistate &b, vector<interest> &interests, bool all = false, bool force = false);
        extern bool parseinterests(fpsent *d, aistate &b, vector<interest> &interests, bool override = false, bool ignore = false);
        extern void tryWipe(fpsent *d);
        extern void switchState(fpsent *d, aistate &b, int t, int r = -1, int v = -1);

        extern bool damaged(fpsent *d, fpsent *e);
        extern void render(fpsent *d, float i);

        struct BotGameMode
        {
            virtual void aifind(fpsent *d, aistate &b, vector<interest> &interests) {}
            virtual bool aicheck(fpsent *d, aistate &b) { return false; }
            virtual bool aidefend(fpsent *d, aistate &b) { return false; }
            virtual bool aipursue(fpsent *d, aistate &b) { return false; }
        };
    }
}