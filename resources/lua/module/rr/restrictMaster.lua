
local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

function Module:load(config)
    self.allow = { config.allowVeto, config.allowLocked, config.allowPrivate }

    if type(self.serverHandle.priv[config.overridePriv]) ~= "number" then
        error("Invalid privilege name: "..tostring(config.overridePriv))
    end

    self.overridePriv = self.serverHandle.priv[config.overridePriv]

    self.handler = function(event)
        if event.mastermode > 0 and (event.allow == 1 or event.allow == -1) then
            if self.allow[event.mastermode] or self.serverHandle:havePrivilege(event.cn, self.overridePriv) then
                event.allow = 1
            else
                event.allow = 0
            end
        end
    end

    self.serverHandle:print (("[restrictMaster] veto: %s, locked: %s, private: %s"):format(unpack(self.allow)))

    self.serverHandle:on("client.mastermode", self.handler)

    BaseModule.load(self, config)
end

function Module:unload()
    if self.handler then
        self.serverHandle:removeListener("client.mastermode", self.handler)
        self.handler = nil
    end
    BaseModule.unload(self)
end

return Module