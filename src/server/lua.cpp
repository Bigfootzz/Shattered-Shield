#include "server/lua.h"
#include "fpsgame/game.h"
#include "engine/engine.h"
#include "shared/version.h"

namespace server
{
    extern bool checkpassword(server::clientinfo *ci, const char *wanted, const char *given);
}

/**
 * \defgroup lua C++ api to the lua module
 * \{
 */

namespace lua
{
    lua_State *L;
    
    /**
     * Name -> lua registry index
     */
    static hashtable<const char*, int> externals;
    
    bool pushEvent(const char *name)
    {
        int *ref = externals.access(name);

        if(ref)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
            return true;
        }

        ref = externals.access("event.none");

        if(ref)
        {
            lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
            lua_pushstring(L, name);
            lua_call(L, 1,0);
        }


        return false;
    }
    
    /**
     * Lua commands
     * \internal
     */
    namespace LUACOMMAND
    {
        static int setCallback(lua_State *L)
        {
            const char *name = luaL_checkstring(L, 1);
            int *ref = externals.access(name);
            if (ref)
            {
                lua_rawgeti(L, LUA_REGISTRYINDEX, *ref);
                luaL_unref (L, LUA_REGISTRYINDEX, *ref);
            }
            else
            {
                lua_pushnil(L);
            }
            /* let's pin the name so the garbage collector doesn't free it */
            lua_pushvalue(L, 1); lua_setfield(L, LUA_REGISTRYINDEX, name);
            /* and now we can ref */
            lua_pushvalue(L, 2);
            externals.access(name, luaL_ref(L, LUA_REGISTRYINDEX));
            return 1;
        }
    }
    
    bool init(int argc, const char **argv)
    {
        L = luaL_newstate();

        luaL_openlibs(L);

        createArgumentsTable(L, argc, argv);
        
        uv_loop_t *loop = uv_default_loop();
        
        #ifdef USE_OPENSSL
        luvit_init_ssl();
        #endif
        
        lua_newtable(L);
        lua_setfield(L, LUA_REGISTRYINDEX, "__pinstrs");

        if (luvit_init(L, loop))
        {
            fprintf(stderr, "luvit_init has failed\n");
            return false;
        }
        
        lua_pushcfunction(L, LUACOMMAND::setCallback);
        lua_setglobal(L, "setCallback");
        
        ASSERT(0 == luaL_dostring(L, "package.path = package.path .. \";resources/lua/?.lua;resources/lua/?/init.lua\""));

        if(luaL_dostring(L, "require \"server\""))
        {
            printf("x %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
            lua_close(L);
            return false;
        }
        
        return true;
    }
    
    void close()
    {
        lua_close(L);
        L = NULL;
    }

    void createArgumentsTable(lua_State *L, int argc, const char **argv)
    {
        lua_pushstring(L, "argv");
        lua_createtable (L, argc, 0);
        
        for (int index = 0; index < argc; index++)
        {
            lua_pushstring (L, argv[index]);
            lua_rawseti(L, -2, index);
        }
        
        lua_rawset(L, LUA_GLOBALSINDEX);
    }
    
    /**
     * Pushes the pinned string on the stack
     * \internal
     */
    inline void pushPinnedString(const char *string)
    {
        lua_pushliteral(L, "__pinstrs"); // __pinstrs 
        lua_rawget (L, LUA_REGISTRYINDEX); // _G["__pinstrs"]
        
        lua_pushstring (L, string); // _G["__pinstrs"], string 
        lua_pushvalue (L, -1); // _G["__pinstrs"], string, string
        
        lua_rawget (L, -3); // _G["__pinstrs"], string, _G["__pinstrs"][string]
    }
    
    void pinString(const char *string)
    {
        pushPinnedString(string);
        
        int count = lua_tointeger(L, -1);
        
        lua_pop(L, 1); //_G["__pinstrs"], string
        
        lua_pushinteger(L, count + 1); //_G["__pinstrs"], string, _G["__pinstrs"][string]+1
        
        lua_rawset(L, -3); // _G["__pinstrs"][string]
        lua_pop(L, 1); //
    }
    
    void unPinString(const char *string)
    {
        pushPinnedString(string);
        ASSERT(lua_isnumber(L, -1));
        
        int count = lua_tointeger(L, -1);
        
        lua_pop(L, 1); //_G["__pinstrs"], string
        
        if (count == 1)
        {
            lua_pushnil(L); //_G["__pinstrs"], string, nil
        }
        else
        {
            lua_pushinteger(L, count - 1); //_G["__pinstrs"], string, _G["__pinstrs"][string]-1
        }
        
        lua_rawset(L, -3); // _G["__pinstrs"]
        lua_pop(L, 1); //
    }
}


//Lua commands for ffi

namespace server
{
    extern int gamemode;
    extern string smapname;
    extern int gamemillis, gamelimit;
    extern int numclients(int exclude = -1, bool nospec = true, bool noai = true, bool priv = false);
}

extern "C" 
{
    /**
     * \defgroup ffi The lua ffi bindings
     * \{
     */
    
    /**
     * Exports a function for windows
     */
    #undef EXPORT
    #ifdef WIN32
        #define EXPORT(prototype) \
        __declspec(dllexport) prototype; \
        prototype
    #else
        #define EXPORT(prototype) prototype
    #endif

    /**
     * Returns the current gamemode id.
     * \return The current gamemode id
     */
    EXPORT(int getGameModeId())
    {
        return server::gamemode;
    }
    
    /**
     * Returns the ::GameMode object associated with the id.
     * Returns \p NULL if the \p id is invalid.
     * \param id The id of the gamemode
     * \return The ::GameMode object associated with the id or \p NULL.
     */    
    EXPORT(GameMode *getGameModeInfo(int id))
    {
        return ::getGameModeInfo(id);
    }
    
    /**
     * Returns the current map name
     * \return the current map name.
     */
    EXPORT(const char *getMapName())
    {
        return server::smapname;
    }
    
    /**
     * Returns the amount of players connected.
     * \return The amount of players connected.
     */
    EXPORT(int getPlayerCount())
    {
        return server::numclients(-1, false, true);
    }
    
    /**
     * Returns the time left to play
     * \return the time left to play
     */
    EXPORT(int getTimeLeft())
    {
        return max(0, server::gamelimit - server::gamemillis);
    }

    /**
     * \defgroup version Version information related functions
     * \{
     */
    /**
     * Returns if the current version is a release
     * \return Werther the current version is a release.
     */
    EXPORT(bool isRelease())
    {
        return version::isRelease();
    }
    
    /**
     * Returns if the current version is a development version
     * \return Werther the current version is a development version.
     */    
    EXPORT(bool isDevelopmentVersion())
    {
        return version::isDevelopmentVersion();
    }
    
    /**
     * Returns the current version integer
     * \return the current version integer
     */    
    EXPORT(int getVersion())
    {
        return version::getVersion();
    }
    
    /**
     * Returns the major version number
     * \return the major version number
     */
    EXPORT(int getMajor())
    {
        return version::getMajor();
    }
    
    /**
     * Returns the minor version number
     * \return the minor version number
     */
    EXPORT(int getMinor())
    {
        return version::getMinor();
    }
    
    /**
     * Returns the patch version number
     * \return the patch version number
     */
    EXPORT(int getPatch())
    {
        return version::getPatch();
    }
    
    /**
     * Returns the patch version number
     * \return the patch version number
     */
    EXPORT(int getTag())
    {
        return version::getTag();
    }
    
    /**
     * Returns the version string
     * \return the current version string
     */
    EXPORT(const char *getVersionString())
    {
        return version::getVersionString();
    }
    
    /**
     * Returns the date of the build
     * \return the date of the build
     */
    EXPORT(const char *getVersionDate())
    {
        return version::getVersionDate();
    }
    
    /**
     * Returns the current protocol version of the server
     * \return the protocol version of the server
     */
    EXPORT(int getProtocolVersion())
    {
        return server::protocolversion();
    }
    
    EXPORT(bool checkpassword(int cn, const char *wanted, const char *given))
    {
        server::clientinfo *ci = (server::clientinfo *)getclientinfo(cn);
        return ci && checkpassword(ci, wanted, given);
    }
    
    /**
     * \}
     */
    /**
     * \}
     */
}

/**
 *\}
 */