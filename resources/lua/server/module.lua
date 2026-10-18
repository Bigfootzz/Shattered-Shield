
local ibmt = require "ibmt"
local core = require "luvit.core"
local Emitter, instanceof = core.Emitter, core.instanceof

local BaseModule = Emitter:extend()

function BaseModule:initialize(serverHandle, name)
    self.serverHandle = serverHandle
    self.name = name
    self.commands = {}
end

function BaseModule:load(config)
    self:emit("load")
end

function BaseModule:unload()
    if self.commandHandler ~= nil then
        self.serverHandle:removeListener("client.command", self.commandHandler)
        self.commandHandler = nil
    end
    self:emit("unload")
end

function BaseModule:parsePrivilege(priv)
    if type(self.serverHandle.priv[priv]) ~= "number" then
        error("Invalid privilege name: "..tostring(priv))
    end

    return self.serverHandle.priv[priv]
end

function BaseModule:registerCommand(name, priv, cb)
    if self.commandHandler == nil then
        self.commandHandler = function(event)
            if event.result == nil and self.commands[event.name] then
                if self.serverHandle:havePrivilege(event.cn, self.commands[event.name][1]) then
                    event.result = self.commands[event.name][2](unpack(event.arguments))
                else
                    event.result = [[error "Access denied"]]
                end
            end
        end
        self.serverHandle:on("client.command", self.commandHandler)
    end

    priv = self:parsePrivilege(priv)
    self.commands[name] = {priv, cb}
end

local ModuleManager = Emitter:extend()

function ModuleManager:initialize(serverHandle)
    self.serverHandle = serverHandle
    self.modules = {}
end

function ModuleManager:load(name, config)
    local moduleLoadingTask = ibmt.create()
    local module = require ("module."..name)
    if not instanceof (module, BaseModule) then
        error ("Could not load module: "..name.." is not a valid module")
    end
    moduleLoadingTask:push(function()
        module = module:new(self.serverHandle, name)
        module:on("load", function() moduleLoadingTask:pop() end)
        module:load(config)
        self.modules[#self.modules+1] = module
    end)

    return moduleLoadingTask
end

function ModuleManager:unload(module)
    local moduleUnloadingTask = ibmt.create()

    if not instanceof (module, BaseModule) then
        error ("Invalid module given")
    end

    for k, v in pairs(self.modules) do
        if v == module then
            self.modules[k] = nil
        end
    end

    moduleUnloadingTask:push(function()
        module:on("unload", function()
            if module then
                moduleUnloadingTask:pop()
                module = nil
            end
        end)
        local suc, err = pcall(eventSource, 'module:on("unload")', module.unload, module)
        if not suc then
            moduleUnloadingTask:cancel(err)
        end
    end)

    return moduleUnloadingTask
end

function ModuleManager:unloadAll()
    local moduleUnloadingTask = ibmt.create()

    moduleUnloadingTask:push()
    for k, module in pairs(self.modules) do
        moduleUnloadingTask:push(self:unload(module))
    end
    moduleUnloadingTask:pop()

    return moduleUnloadingTask
end




return {
    ModuleManager = ModuleManager,
    BaseModule = BaseModule
}