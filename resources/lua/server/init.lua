local luvit = require "luvit" -- Init luvit

local config = require "server.config"
local ibmt = require "ibmt"
local nativeHandle = require "server.native"
local ServerHandle = require "server.handle".Handle

local serverApi = ServerHandle:new(nativeHandle)

local startupTask = ibmt.create()


startupTask:push(function()
    local modules = {}

    local env = {}
    function env.server(confstring)
        for k, v in pairs(confstring) do
            serverApi:setSetting(k, v)
        end
    end

    env.print = print
    
    function env.module(name)
        return function(config)
            modules[#modules+1] = {
                name,
                config
            }
        end
    end

    local nop
    nop = function() return nop end
    env.disabled = {}
    env.disabled.module = nop

    config.loadConfigFile("resources/config/server.lua", function(err, data)
        if err then
            startupTask:cancel(err)
        end

        local moduleManager = serverApi:getModuleManager()

        for k, v in pairs(modules) do
            startupTask:push(moduleManager:load(v[1], v[2]))
        end

        startupTask:pop()
    end, env)
end)

startupTask:on("error", function(message)
    serverApi:printError ("Could not start server: "..tostring(message))

    local traceback = require "debug".traceback()
    serverApi:printError(traceback)

    serverApi:exit(1)
end)

startupTask:on("finish", function()
    serverApi:start()
    startupTask = nil
end)

serverApi:on("server.listen", function(event)
    serverApi:print(
        ("Listening on port %i (extinfo: %i, laninfo: %i)"):format(event.port, event.infoport, event.lanport)
    )
end)

serverApi:on("server.start", function()
    serverApi:print(
        ("Running Revelade Revolution %s - %s"):format(serverApi:getVersionString(), serverApi:getVersionDate()),
        "dedicated server started...",
        "Ctrl-C to exit\n"
    )
end)

local shutdownTask
serverApi:on("server.stop", function()
    if shutdownTask then
        return shutdownTask:cancel "Forced shutdown .."
    end

    serverApi:print "Shutting down ..."

    shutdownTask = ibmt.create()

    shutdownTask:on("finish", function()
        serverApi:shutdownFinished()
    end)
    shutdownTask:on("error", function(msg)
        serverApi:printError("Error while shutting down: "..tostring(msg))
        serverApi:shutdownFinished()
    end)

    shutdownTask:push(serverApi:getModuleManager():unloadAll())
end)

serverApi:on("server.afterStop", function()
    serverApi:print "Exit"
    serverApi:exit()
end)

serverApi:on("event.none", function(event)
    serverApi:printError("No event handler was set for: "..tostring(event.name))
end)