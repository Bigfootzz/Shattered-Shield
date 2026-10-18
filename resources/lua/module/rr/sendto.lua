
local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

function Module:load(config)
    self.serverHandle:print ("[sendto] enabled")

    self:registerCommand("sendto", "PRIV_MASTER", function(cn)
        cn = tonumber(cn)
        if type(cn) ~= "number" then error("Invalid client number") end

        self.serverHandle:sendMapTo(cn)

        return ("sendtosuc %i"):format(cn)
    end)

    BaseModule.load(self, config)
end

function Module:unload()
    BaseModule.unload(self)
end

return Module