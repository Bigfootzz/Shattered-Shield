
local fs = require "luvit.fs"


local function loadConfigFile(file, cb, env)
    fs.readFile(file, function(err, buf)
        if err then
            cb(err, nil)
        else
            local func = assert(loadstring(buf, file))
            local data = env or {}
            data._G = data
            setfenv(func, data)
            func()
            cb(nil, data)
        end
    end)
end

return {
    loadConfigFile = loadConfigFile
}