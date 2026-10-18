
local max = require "math".max
local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

function Module:load(config)
    self.publicMode = config.publicMode
    self.publicPermission = self:parsePermission(config.publicPermission or "PRIV_MASTER")
    self.users = {}
    for k, v in pairs(config.users) do
        self.users[#self.users+1] = {
            self:parsePermission(v[1] or v.permission),
            v[2] or v.password
        }
    end

    self.handler = function(event)
        if event.password == "" and self.publicMode then
            event.newPrivilege = max(event.newPrivilege, self.publicPermission)
        else
            for k, user in pairs(self.users) do
                if self.serverHandle:checkPassword(event.cn, user[2], event.password) then
                    event.newPrivilege = max(event.newPrivilege, user[1])
                    break
                end
            end
        end
    end

    self.serverHandle:print "[setmaster] Listening for setmaster requests."

    self.serverHandle:on("client.setmaster", self.handler)

    BaseModule.load(self, config)
end

function Module:unload()
    if self.handler then
        self.serverHandle:removeListener("client.setmaster", self.handler)
        self.handler = nil
    end
    BaseModule.unload(self)
end

function Module:parsePermission(perm)
    if type(self.serverHandle.priv[perm]) ~= "number" then
        error("Invalid privilege name: "..tostring(perm))
    end

    return self.serverHandle.priv[perm]
end

return Module