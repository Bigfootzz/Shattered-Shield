
enum { ID_VAR, ID_FVAR, ID_SVAR, ID_COMMAND, ID_CCOMMAND, ID_ALIAS };

// Uncomment to dump all vars
inline int dumpVar(const char *name, const char *global, int min, int cur, int max, int flags)
{
    //printf("void set_%s(int %s);\nint get_%s();\n", global, name, global);
    return cur;
}

inline char *dumpVar(const char *name, const char *global, const char *cur, int flags)
{
    //printf("void set_%s(const char *%s);\nconst char *get_%s();\n", global, name, global);
    return newstring(cur);
}

/**
 * Exports a function for windows
 */
#ifdef WIN32
    #define EXPORT(prototype) \
        extern "C" __declspec(dllexport) prototype; \
        extern "C" prototype
#else
    #define EXPORT(prototype) extern "C" prototype
#endif

#define _VAR(name, global, min, cur, max, flags, onchange) \
    int global = dumpVar(#name, #global, min, cur, max, flags); \
    EXPORT(int get_##global ()) { return global; } \
    EXPORT(void set_##global (int value)) { global = clamp(value, min, max); {onchange;} }
#define VAR(name, min, cur, max) _VAR(name, name, min, cur, max, 0, )
#define VARN(name, global, min, cur, max) _VAR(name, global, min, cur, max, 0, )
#define VARP(name, min, cur, max) _VAR(name, name, min, cur, max, 1, )
#define VARF(name, min, cur, max, onchange) _VAR(name, name, min, cur, max, 0, onchange)
#define VARFP(name, min, cur, max, onchange) _VAR(name, name, min, cur, max, 1, onchange)

#define _SVAR(name, global, cur, flags, onchange) \
    char *global = dumpVar(#name, #global, cur, flags); \
    EXPORT(const char *get_##global ()) { return global; } \
    EXPORT(void set_##global (const char *value)) { DELETEA(global); global = newstring(value); onchange }

#define SVAR(name, cur) _SVAR(name, name, cur, 0, )
#define SVARP(name, cur) _SVAR(name, name, cur, 1, )

#define ICOMMAND(...)