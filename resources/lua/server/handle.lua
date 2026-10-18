
local core = require "luvit.core"
local Emitter, Object = core.Emitter, core.Object
local process = require "luvit.process"
local ModuleManager = require "server.module".ModuleManager

local os = require "os"
local ffi = require "ffi"

local rawSetCallback = setCallback
_G.setCallback = nil


local Handle = Emitter:extend()

Handle.priv = {
    PRIV_NONE = 0,
    PRIV_PLAYER = 1,
    PRIV_MODERATOR_INACTIVE = 2,
    PRIV_DEVELOPER_INACTIVE = 3,
    PRIV_CREATOR_INACTIVE = 4,

    PRIV_ADMIN_INACTIVE = 5,

    PRIV_MASTER = 6,
    PRIV_MODERATOR = 7,

    PRIV_DEVELOPER = 8,
    PRIV_CREATOR = 9,

    PRIV_ADMIN = 10
}

function Handle:initialize(nativeHandle)
    self.native = nativeHandle
    self.moduleManager = ModuleManager:new(self)
end

function Handle:getModuleManager()
    return self.moduleManager
end

function Handle:print(...)
    for k, msg in pairs({...}) do
        process.stdout:write(("[%s] %s\n"):format(os.date("%Y-%m-%dT%H:%M:%S"), msg))
    end
end

function Handle:printError(...)
    for k, msg in pairs({...}) do
        process.stderr:write(("[%s] %s\n"):format(os.date("%Y-%m-%dT%H:%M:%S"), msg))
    end
end

function Handle:exit(i)
    process.exit(i)
end

function Handle:start()
    self.native.startServer()
end

function Handle:shutdownFinished()
    self.native.shutdownFinished()
end

function Handle:getVersionString()
    return ffi.string(self.native.getVersionString())
end

function Handle:getVersionDate()
    return ffi.string(self.native.getVersionDate())
end

function Handle:getProtocolVersion()
    return self.native.getProtocolVersion()
end

function Handle:setPort(port)
    self.native.set_serverport(port)
end

function Handle:getPort()
    return self.native.get_serverport()
end

function Handle:setMaxClients(clients)
    self.native.set_maxclients(clients)
end

function Handle:getMaxClients()
    return self.native.get_maxclients()
end

function Handle:setServerName(name)
    self.native.set_serverdesc(name)
end

function Handle:getServerName()
    return ffi.string(self.native.get_serverdesc())
end

function Handle:setHostName(name)
    self.hostName = name
end

function Handle:getGameModeId()
    return self.native.getGameModeId()
end

function Handle:getMapName()
    return ffi.string(self.native.getMapName())
end

function Handle:getHostName()
    return self.hostName
end

function Handle:getTimeLeft()
    return self.native.getTimeLeft()
end

function Handle:getPlayerCount()
    return self.native.getPlayerCount()
end

function Handle:checkPassword(cn, wanted, given)
    return self.native.checkpassword(cn, wanted, given)
end

function Handle:sendMessageTo(cn, msg)
    self.native.sendServerMessageTo(cn, msg)
end

function Handle:broadcast(msg)
    self.native.sendServerMessage(msg)
end

function Handle:sendMapTo(cn)
    self.native.sendMapTo(cn)
end

function Handle:getDisplayName(cn)
    local name = self.native.getDisplayName(cn)
    if name ~= nil then
        return ffi.string(name)
    end
end

function Handle:getIp(cn)
    return self.native.getIp(cn)
end

function Handle:getPrivilege(cn)
    return self.native.getPrivilege(cn)
end

function Handle:havePrivilege(cn, priv)
    return self:getPrivilege(cn) >= priv
end

function Handle:setBotLimit(limit)
    self.native.set_botlimit(limit)
end

function Handle:setBotBalance(balance)
    self.native.set_botbalance(balance and 1 or 0)
end

local settingsMapping = {
    port = "setPort",
    maximumClients = "setMaxClients",
    name = "setServerName",
    host = "setHostName",
    botLimit = "setBotLimit",
    botBalance = "setBotBalance"
}
function Handle:setSetting(name, value)
    local f = settingsMapping[name]
    if not f then
        error("Unkown setting: "..name)
    else
        self[f](self, value)
    end
end

local events = {
    ["client.setmaster"]        = { { "cn", "password" },       { ["newPrivilege"] = Handle.priv.PRIV_NONE } },
    ["client.connect"]          = { { "cn" },                   {} },
    ["client.disconnect"]       = { { "cn" },                   {} },
    ["client.mastermode"]       = { { "cn", "mastermode" },     { ["allow"] = -1 } },
    ["client.isMuted"]          = { { "cn", "type" },           { ["isMuted"] = -1 } },
    ["server.listen"]           = { { "port", "infoport", "lanport" }, {} },
    ["server.stop"]             = { {}, {} },
    ["server.start"]            = { {}, {} },
    ["server.afterStop"]        = { {}, {} },
    ["event.none"]              = { { "name" },                 {} },
}

function Handle:addHandlerType(name)
    if events[name] then
        local ProtoType = Object:extend()
        ProtoType.name = name

        for k, v in pairs(events[name][2]) do
            ProtoType[k] = v
        end

        rawSetCallback(name, function(...)
            local event = ProtoType:new()
            local args = {...}
            for k, v in ipairs(events[name][1]) do
                event[v] = args[k]
            end
            eventSource(name, self.emit, self, name, event)
            local ret = {}
            for k, v in pairs(events[name][2]) do
                ret[#ret+1] = event[k]
            end
            return unpack(ret)
        end)
    elseif name == "client.command" then
        rawSetCallback(name, function(cn, argc, name, ...)
            local event = Object:new()
            event.cn = cn
            event.argc = argc
            event.name = name
            event.arguments = {...}
            event.result = nil

            local res, err = pcall(function()
                self:emit("client.command", event)
            end)

            if not res then
                self:printError("Error while executing command "..name..": "..tostring(err))
                event.result = ([[error %q]]):format(tostring(err))
            end

            return event.result
        end)
    end
end

return {
    Handle = Handle,
}