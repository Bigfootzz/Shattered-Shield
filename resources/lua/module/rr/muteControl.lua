
local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

Module.mute = {
    EDIT = 1,
    -- TODO: add more
}

function Module:load(config)
    -- cn -> mutes mapping
    self.mutes = {}

    -- Stuff that is muted by default
    self.defaultMute = {}

    for k, v in pairs(config.defaultMute) do
        self.defaultMute[self:parseMute(v)] = 1
    end

    self.isMutedHandler = function(event)
        if event.isMuted == 1 then return end
        if self.mutes[event.cn] and self.mutes[event.cn][event.type] then
            event.isMuted = self.mutes[event.cn][event.type]
        elseif self.defaultMute[event.type] then
            event.isMuted = self.defaultMute[event.type]
        end
        if event.isMuted == 1 then
            self.serverHandle:sendMessageTo(event.cn, "Warning: you are editmuted! your actions will be lost. Do not edit!")
        end
    end

    self.disconnectHandler = function(event)
        self.mutes[event.cn] = nil
    end

    self.serverHandle:print ("[mutecontrol] Muting is enabled (edit="..tostring(self.defaultMute[self.mute.EDIT])..")")

    self.serverHandle:on("client.isMuted", self.isMutedHandler)
    self.serverHandle:on("client.disconnect", self.disconnectHandler)

    self:registerCommand("mute", "PRIV_MASTER", function(cn, mute, value)
        cn = tonumber(cn)
        if type(cn) ~= "number" then error("Invalid client number") end
        mute = tostring(mute)
        mute = self:parseMute(mute)
        value = value and tonumber(value) or 1
        self.mutes[cn] = self.mutes[cn] or {}
        self.mutes[cn][mute] = value
        return ("mutesuc %i %i %i"):format(cn, mute, value)
    end)

    BaseModule.load(self, config)
end

function Module:unload()
    if self.isMutedHandler then
        self.serverHandle:removeListener("client.isMuted", self.isMutedHandler)
        self.isMutedHandler = nil
    end

    if self.disconnectHandler then
        self.serverHandle:removeListener("client.disconnect", self.disconnectHandler)
        self.disconnectHandler = nil
    end

    BaseModule.unload(self)
end

function Module:parseMute(mute)
    if not self.mute[mute] then
        error ("Invalid mute type: "..tostring(mute))
    end
    
    return self.mute[mute]
end

return Module


