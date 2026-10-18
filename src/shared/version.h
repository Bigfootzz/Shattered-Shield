#ifndef RR_VERSION_H
#define RR_VERSION_H

namespace version
{
    extern bool isRelease();
    extern bool isDevelopmentVersion();
    extern int getVersion();
    extern int getMajor();
    extern int getMinor();
    extern int getPatch();
    extern int getTag();
    extern const char *getVersionString();
    extern const char *getVersionDate();
}

#endif