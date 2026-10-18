
local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

function Module:load(config)
    self.message = tostring(config.message)

    self.handler = function(event)
        self.serverHandle:sendMessageTo(event.cn, self.message)
    end

    self.serverHandle:print "[motd] Enabled."

    self.serverHandle:on("client.connect", self.handler)

    BaseModule.load(self, config)
end

function Module:unload()
    if self.handler then
        self.serverHandle:removeListener("client.connect", self.handler)
        self.handler = nil
    end
    BaseModule.unload(self)
end

return Module