#include "engine.h"

extern "C"
{
    #include "uv.h"
}

void nop(uv_handle_t *handle)
{
   
}


static uv_tcp_t serverPortLookUp = {0};

int findFreePort()
{
    uv_loop_t *loop = uv_default_loop();
   
    int result;
    uv_tcp_init(loop, &serverPortLookUp);
   
    struct sockaddr sockname = {0};
    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 0, &bind_addr);
    result = uv_tcp_bind(&serverPortLookUp, (sockaddr *)&bind_addr, 0);
   
    if(result < 0)
    {
        conoutf("Could not find free port, bind failed %s: %s\n", uv_err_name(result), uv_strerror(result));
        return -1;
    }
   
    int namelen = sizeof (sockname);
    result = uv_tcp_getsockname(&serverPortLookUp, &sockname, &namelen);
   
    if(result != 0)
    {
        conoutf("Could not find free port, sockname failed %s: %s\n", uv_err_name(result), uv_strerror(result));
        return -1;
    }
   
    uv_close((uv_handle_t *)&serverPortLookUp, nop);
   
    sockaddr_in addr = *(struct sockaddr_in*)&sockname;
   
    return addr.sin_port;
}

bool running = false;
uv_process_t serverProcess = {0};

static void _onExit(uv_process_s*, int64_t, int)
{
    running = false;
}

void startLocalServer(int port)
{
    defformatstring(portArg)("-p%i", port);

    char binaryDir[] = RR_SERVER_BINARY_DIR;
    char binaryName[] = RR_SERVER_BINARY;

   
    string binary;
    formatstring(binary)("%s/%s", binaryDir, binaryName);
   
    path(binary);
   
    if(!fileexists(binary, "r"))
    {
        formatstring(binary)("%s", binaryName);
        path(binary);
       
        if(!fileexists(binary, "r"))
        {
            conoutf(CON_ERROR, "Could not start server: could not find server executable (%s/%s, %s)", binaryDir, binaryName, binary);
            return;
        }
    }
   
    char *args[] = { newstring(binary), newstring("-mlocal"), newstring(portArg), NULL };
   
    uv_process_options_t serverProcessOptions = {0};
   
    serverProcessOptions.args = args;
    serverProcessOptions.file = binary;
    serverProcessOptions.flags = UV_PROCESS_WINDOWS_HIDE;
   
    uv_stdio_container_t serverStdio[3];
   
    serverStdio[0].flags = UV_INHERIT_FD;
    serverStdio[0].data.fd = 0;
    serverStdio[1].flags = UV_INHERIT_FD;
    serverStdio[1].data.fd = 1;
    serverStdio[2].flags = UV_INHERIT_FD;
    serverStdio[2].data.fd = 2;
   
    serverProcessOptions.stdio_count = 3;
    serverProcessOptions.stdio = serverStdio;
   
    serverProcessOptions.exit_cb = _onExit;
   
    int result = 0;
    if ((result = uv_spawn(uv_default_loop(), &serverProcess, &serverProcessOptions)) < 0)
    {
        conoutf(CON_ERROR, "Could not start server: %s: %s\n", uv_err_name(result), uv_strerror(result));
        return;
    }
   
    conoutf(CON_INFO, "Launched server on port %i with PID %d (%s)", port, serverProcess.pid, serverProcessOptions.file);
    running = true;
}

void stopLocalServer()
{
    if(running)
    {
        conoutf(CON_INFO, "Stopping local server");
        uv_process_kill(&serverProcess, SIGTERM);
    }
}

ICOMMAND(findFreePort, "", (), intret(findFreePort()));
ICOMMAND(startLocalServer, "i", (int *i), {
    int port = *i;
    if(port < 1)
    {
       
        port = findFreePort();
    }
    startLocalServer(port);
    intret(port);
});
COMMAND(stopLocalServer, "");
