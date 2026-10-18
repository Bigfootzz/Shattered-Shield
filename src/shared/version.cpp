#include "version.h"
#include "rr_version.h"

namespace version
{
    bool isRelease()
    {
        return RR_VERSION_TYPE == RR_VERSION_TYPE_RELEASE;
    }

    bool isDevelopmentVersion()
    {
        return RR_VERSION_TYPE == RR_VERSION_TYPE_DEVELOPMENT;
    }

    int getVersion()
    {
        return RR_VERSION_VAL;
    }

    int getMajor()
    {
        return RR_VERSION_MAJOR;
    }

    int getMinor()
    {
        return RR_VERSION_MINOR;
    }

    int getPatch()
    {
        return RR_VERSION_PATCH;
    }

    int getTag()
    {
        return RR_VERSION_TAG;
    }

    const char *getVersionString()
    {
        return RR_VERSION_STRING;
    }

    const char *getVersionDate()
    {
        return RR_VERSION_DATE;
    }
}