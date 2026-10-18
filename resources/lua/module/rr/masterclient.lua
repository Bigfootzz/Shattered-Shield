
local querystring = require "luvit.querystring"
local http = require "luvit.http"
local timer = require "luvit.timer"
local json = require "luvit.json"

local BaseModule = require "server.module".BaseModule
local Module = BaseModule:extend()

local INTERVAL = 5 * 1000 -- Every 5 sec

Module.registerPath = "/register"
Module.updatePath = "/update"
Module.unRegisterPath = "/delete"
Module.host = "servers.rr.theintercooler.com"
Module.port = 16962

function Module:load(config)
    for k, v in pairs({"registerPath", "updatePath", "unRegisterPath", "host", "port", "serverName"}) do
        self[v] = config[v] or self[v]
    end

    self:makeRequest()
    self:startTimer()

    BaseModule.load(self, config)
end

function Module:startTimer()
    if not self._timer then
        self._timer = timer.setInterval(INTERVAL, function()
            self:makeRequest()
        end)
    end
end

function Module:makeRequest()
    local path = self._password and self.updatePath or self.registerPath

    local wasUpdateRequest = self._password

    if not wasUpdateRequest then
        self.serverHandle:print (("[masterclient] Registering to %s : %i %s"):format(self.host, self.port, path))
    end

    local req
    req = http.request({
        host = self.host,
        port = self.port,
        path = path,
        method = 'POST',
    }, function (res)
        if not self._timer then
            return --We are unloading
        end
        
        if wasUpdateRequest then --was update request
            if res.statusCode == 204 then
                --self.serverHandle:print ("[masterclient] Finished updating.")
            elseif res.statusCode == 404 then
                req:emit("error", "Registration timed out.")
            end
            res:destroy()
            return
        end

        if res.headers["content-type"] ~= "text/json" and res.headers["content-type"] ~= "application/json" then
            res:on("data", print)
            res:on("end", function() req:emit("error", "Invalid content type: "..tostring(res.headers["content-type"])) end)
            return
        end

        local parser = json.streamingParser(function(value)
            if value.error then
                req:emit("error", value.error)
            else        
                if value and value.password and value.uuid then
                    self._uuid = value.uuid
                    self._password = value.password
                    res:destroy()
                    self.serverHandle:print(("[masterserver] Finished registering, UUID: %s, Password: %s"):format(self._uuid, self._password))
                else
                    req:emit("error", "response does not contain either password or uuid.")
                end
            end
        end)

        res:on('data', function (chunk)
            local suc, err = pcall(function()
                parser:parse(chunk)
            end)

            if not suc then
                req:emit("error", err)
                res:destroy()
                parser = nil
            end
        end)

        res:on("end", function ()
            if parser then
                local suc, err = pcall(function()
                    parser:complete()
                end)

                if not suc then
                    req:emit("error", err)
                end
            end
            res:destroy()
        end)
    end)

    req:on("error", function(err)
        self._uuid = nil
        self._password = nil
        self.serverHandle:printError(("[masterclient] Could not update to the master: %s"):format(tostring(err)))
        req:destroy()
    end)

    local versionString = self.serverHandle:getVersionString()
    local protocolVersion = self.serverHandle:getProtocolVersion()

    local content = 
        "gamemode="     ..querystring.urlencode(tonumber(self.serverHandle:getGameModeId()))    .. "&" ..
        "mapname="      ..querystring.urlencode(tostring(self.serverHandle:getMapName()))       .. "&" ..
        "timeleft="     ..querystring.urlencode(tonumber(self.serverHandle:getTimeLeft()))      .. "&" ..
        "playercount="  ..querystring.urlencode(tonumber(self.serverHandle:getPlayerCount()))

    if self._password and self._uuid then
        --Update
        content = content .. "&uuid=" ..querystring.urlencode(self._uuid).. "&password="..querystring.urlencode(self._password)
    else
        local serverName = self.servername and self.serverName ~= "" and self.serverName or self.serverHandle:getServerName()
        --error ""
        --First registration
        content = content .. "&" ..
            "maxplayers="       ..querystring.urlencode(tonumber(self.serverHandle:getMaxClients()))                    .. "&" ..
            "name="             ..querystring.urlencode(tostring(serverName)) .. "&" ..
            "port="             ..querystring.urlencode(tonumber(self.serverHandle:getPort()))                          .. "&" ..
            "version="          ..querystring.urlencode(tostring(versionString))                                        .. "&" ..
            "protocol="         ..querystring.urlencode(tostring(protocolVersion))

        local host = querystring.urlencode(self.serverHandle:getHostName())

        if host ~= ""then
            --Don't send if not given
            content = content .. "&host="..host
        end
    end

    req:setHeader('User-Agent', ('ReveladeRevolution-server/%s (protocol-%i)'):format(versionString, protocolVersion))
    req:setHeader('Content-Length', #content)
    req:setHeader('Content-Type', 'application/x-www-form-urlencoded')

    req:setTimeout(5000, function()

    end)
    req:done(content)
end

function Module:unload()
    if self._timer then
        self._timer:stop()
        self._timer = nil
    end

    if self._uuid and self._password then
        self.serverHandle:print ("[masterclient] Removing server from master list")

        local req

        req = http.request({
            host = self.host,
            port = self.port,
            path = self.unRegisterPath,
            method = 'POST',
        }, function (res)
            local parser = json.streamingParser(function(value)
                if value.error then
                    req:emit("error", value.error)
                end
            end)

            res:on('data', function (chunk)
                local suc, err = pcall(function()
                    parser:parse(chunk)
                end)

                if not suc then
                    res:destroy()
                    parser = nil
                    req:emit("error", err)
                end
            end)

            res:on("end", function ()
                if parser then
                    parser:complete()
                end

                res:destroy()
                BaseModule.unload(self)
            end)
        end)

        req:on("error", function(err)
            self:printError(("[masterclient] Warning: Could not remove from the master server: %s"):format(tostring(err)))
            BaseModule.unload(self)
        end)

        local content = 
            'uuid='..querystring.urlencode(self._uuid or "").. "&"..
            'password='..querystring.urlencode(self._password or "")

        local versionString = self.serverHandle:getVersionString()
        local protocolVersion = self.serverHandle:getProtocolVersion()

        req:setHeader('User-Agent', ('ReveladeRevolution-server/%s (protocol-%i)'):format(versionString, protocolVersion))        

        req:setHeader('Content-Length', #content)
        req:setHeader('Content-Type', 'application/x-www-form-urlencoded')
        req:done(content)
    else
        BaseModule.unload(self)
    end

end



return Module