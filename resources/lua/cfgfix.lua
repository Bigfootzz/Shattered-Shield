--[[
    Script to dump texture information

    Purpose: Dump all information of all textures in the default config to the default output
]]

local table = require "table"
local math = require "math"

local process = require "luvit.process"
local fs = require "luvit.fs"

local cubescript = require "cubescript"
local stdlib = require "cubescript.stdlib"

local utils = require "luvit.utils"
utils.DUMP_MAX_DEPTH = 5

local env = cubescript.createEnvironment()

for k, v in pairs(stdlib.api) do
    env:register(k, v)
end

local function dirname(f)
    return f:gsub("(.*)/(.-)$", function(a) return a end)
end

local function mkdir(directory)
    local parent = dirname(directory)
    if parent ~= "" and directory:find("/") then mkdir(parent) end
    local suc, err = pcall(fs.mkdirSync, directory, "777")
    if not suc and (type(err) ~= "table" or err.code ~= "EEXIST") then
        error(err)
    end
end

local function executeFile(file)
    return env:run({file = file, line = 1, value = fs.readFileSync(file)})
end

local function noop() return "" end

env:register("maptitle", noop)
env:register("1", function() return env:toBool(true) end) --TODO: fix this

env:register("exec", function(env, scope, trace, file)
    return executeFile(file)
end)

env:register("findfile", function(env, scope, trace, file)
    return env:toBool(fs.openSync(file, "r"))
end)

env:register("mapmodelreset", noop)
env:register("materialreset", noop)
env:register("mmodel", noop)
env:register("skybox", noop)

local slotOrder = {
    "c", "u", "d", "n", "g", "s", "z", "e"
}

local availableSlots = {}
for k, slot in pairs(slotOrder) do availableSlots[slot] = true end

local function textureType(type)
    return ({
        [0] = "c",
        [1] = "u",
    })[type] or type
end

local textures = {}
local textureIndex = 0

local definedShaders = {}
local currentShader = "stdworld"

env:register("texturereset", function() textureIndex = 0 textures = {} return "" end)

env:register("setshader", function(env, scope, trace, shader)
    if not definedShaders[shader] then definedShaders[shader] = { name = shader } end
    currentShader = shader
    return shader
end)

env:register("setshaderparam", function(env, scope, trace, name, ...)
    definedShaders[currentShader][name] = {...}
    return name
end)

env:register("texture", function(evn, scope, trace, type, name, rot, xoffset, yoffset, scale)
    local shaderSettings = {}

    for k, v in pairs(definedShaders[currentShader] or {}) do
        shaderSettings[k] = v
    end

    type = textureType(type)
    
    local traceElement = trace:copy()

    local texture = {
        index   = textureIndex,
        type    = type,
        name    = name,
        rot     = env:toNumber(rot, 0),
        xoffset = env:toNumber(xoffset, 0),
        yoffset = env:toNumber(yoffset, 0),
        scale   = env:toNumber(scale, 1),
        shaderSettings = shaderSettings,
        derrivatives = {},
        location = traceElement
    }

    if type == "c" or type =="water" or type == "lava" then
        textureIndex = textureIndex + 1
        textures[textureIndex] = texture
    else
        if not availableSlots[type] then
            error ("Unkown slot type: "..type)
        end
        textures[textureIndex].derrivatives[type] = texture
    end

    return tostring(textureIndex)
end)

env:register("texscale", function(env, scope, trace, scale)
    textures[textureIndex].scale = scale
    return scale
end)

env:register("texlayer", function(env, scope, trace, layer, name, mode, scale)
    local t = textures[textureIndex]

    t.variants = t.variants or {}
    t.variants.layer    = layer < 0 and math.max(textureIndex+layer, 0) or layer
    t.layermaskname     = name and "packages/"..name or nil
    t.layermaskmode     = mode
    t.layermaskscale    = env:toNumber(scale, 0) <= 0 and 1 or scale

    return ""
end)

env:register("nextslotindex", function() return textureIndex - 1 end)

env.globalScope["do"] = nil
env.globalScope["loopconcat"] = nil
executeFile("data/stdlib.cfg")

executeFile("data/default_map_settings.cfg")
local i = 1
local readingOptions = true
local checkOnly = false
while process.argv[i] do
    if readingOptions and process.argv[i]:match("^%-%-") then
        if process.argv[i] == "--" then
            readingOptions = false
        elseif process.argv[i] == "--check-only" then
            checkOnly = true
        else
            error("Unkown option: "..process.argv[i])
        end
    else
        executeFile(process.argv[i])
        executeFile("data/default_map_settings.cfg") -- The most "efficient" way to make sure the global .tex files are not overwritten
    end
    i = i + 1
end

local wrote = {}
for i, texture in pairs(textures) do
    -- Don't do anything for builtin textures
    if dirname(texture.name) ~= "textures" then
    local str = 
    "// %s (%i)\n"..
    "// Automatically generated using cfgfix\n"..
    "setshader %q\n"
    
    str = str:format(texture.name, texture.index-1, texture.shaderSettings.name or "unkown")
    
    for parameterName, shaderParameter in pairs(texture.shaderSettings) do
        if parameterName ~= "name" then
            str = str .. ("setshaderparam %q"):format(parameterName)
            for j, value in ipairs(shaderParameter) do
                str = str .. (" %f"):format(value)
            end
            str = str .. "\n"
        end
    end
    local modifiers = {}
    
    if texture.rot > 0 then
        modifiers["-rotated"] = "-rotated"
    end
    
    str = str .. ("texture %q %q %i %i %i %f\n"):format(texture.type, texture.name, texture.rot, texture.xoffset, texture.yoffset, texture.scale)
    
    for k, slot in pairs(slotOrder) do
        if texture.derrivatives[slot] then
            local subTex = texture.derrivatives[slot]
            str = str .. ("texture %q %q %i %i %i %f\n"):format(subTex.type, subTex.name, subTex.rot, subTex.xoffset, subTex.yoffset, subTex.scale)
        end
    end
    
    local need
    local shaderName = texture.shaderSettings.name
    
    if shaderName == "stdworld" then
        need = {"c"}
    elseif shaderName == "decalworld" then
        need = {"c", "d"}
    elseif shaderName == "glowworld" then
        need = {"c", "g"}
    elseif shaderName == "bumpworld" then
        need = {"c", "n"}
    elseif shaderName == "bumpglowworld" then
        need = {"c", "n", "g"}
    elseif shaderName == "bumpspecworld" then
        need = {"c", "n"}
    elseif shaderName == "bumpspecmapworld" then
        need = {"c", "n", "s"}
    elseif shaderName == "bumpspecglowworld" then
        need = {"c", "n", "g"}
    elseif shaderName == "bumpspecmapglowworld" then
        need = {"c", "n", "s", "g"}
    elseif shaderName == "bumpparallaxworld" then
        need = {"c", "n", "z"}
    elseif shaderName == "bumpspecparallaxworld" then
        need = {"c", "n", "z"}
    elseif shaderName == "bumpspecmapparallaxworld" then
        need = {"c", "n", "s", "z"}
    elseif shaderName == "bumpparallaxglowworld" then
        need = {"c", "n", "z", "g"}
    elseif shaderName == "bumpspecparallaxglowworld" then
        need = {"c", "n", "z", "g"}
    elseif shaderName == "bumpspecmapparallaxglowworld" then
        need = {"c", "n", "s", "z", "g"}
    else
        error("Unkown shader type: "..texture.shaderSettings.name)
    end
    
    local defined = { [texture.type] = true }
    
    for k, subTex in pairs(texture.derrivatives) do
        defined[subTex.type] = true
    end
    
    for k, needs in pairs(need) do
        if not defined[needs] then
            env:throwError("Missing texture slot "..needs.." for shader "..shaderName, texture.location)
       end
    end
    
    for def, k in pairs(defined) do
        local used = false
        for j, req in pairs(need) do
            used = used or req == def
        end
        
        if not used then
            env:throwError("Unused texture slot "..def, texture.location)
        end
    end
    
    -- Write back to the .tex file if we can
    local texName = texture.location:pop().statement.file
    
    if not (texName:find("%.tex")) then
        texName = (texture.name):gsub("%.(.-)$", ""):gsub("_c$", "")..".tex"
    end
    
    texName = texName:gsub("^packages/", "")
    texName = "packages2/"..texName
    mkdir(dirname(texName))

    if wrote[texName] then
        local moName = ""
        for k, v in pairs(modifiers) do moName = moName .. v end
        texName = texName:gsub("%.tex", moName..".tex")
    end
    
    if not wrote[texName] then
        if not checkOnly then
            print ("Wrote: "..texName)
            fs.writeFileSync(texName, str)
        end
        wrote[texName] = texName
    else
        p(texture, modifiers)
        error ("Duplicate file write attempt: "..texName)
    end
    end
end

if checkOnly then
    print "-- Textures validated"
end
