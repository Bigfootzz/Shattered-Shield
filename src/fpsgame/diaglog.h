// diaglog.h — DIAGNOSTIC (v0.1.75-test only, REMOVE before the next real release).
// Crash-safe logging for the .73/.74 world-entry crash on the Intel PC.
// Every line goes to stdout (flushed immediately) AND is appended to
// diaglog.txt in the game folder (open/write/close per line), so a hard
// crash cannot lose the tail the way stdout.txt did. Call sites bracket
// each world-entry step; the last line in the log is where it died.
// Include only in the files that call diaglog() (fps.cpp, render.cpp,
// client.cpp) so the helper does not force a full rebuild.
#ifndef __DIAGLOG_H__
#define __DIAGLOG_H__

static inline void diaglog(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[DIAG] %s\n", buf);
    fflush(stdout);
    FILE *f = fopen("diaglog.txt", "a");
    if(f) { fprintf(f, "[DIAG] %s\n", buf); fclose(f); }
}

#endif
