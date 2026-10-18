local ffi = require "ffi"
local Object = require "luvit.core".Object

local geoip
local errors = {}

do
    local try = {
        "GeoIP",
        "GeoIP.so.1"
    }

    for k, attempt in pairs(try) do
        local suc, err = pcall(function()
            geoip = ffi.load(attempt, false)
        end)

        if suc then
            break
        else
            errors[#errors+1] = err
        end
    end
end

if not geoip then
    local str = "Could not load GeoIP library"
    for k, v in pairs(errors) do 
        str = str.."\n"..tostring(v)
    end
    error (str)
else
    errors = nil
end

ffi.cdef [[
    typedef struct GeoIP_ GeoIP;
    
    typedef struct GeoIPLookup {
        int netmask;
    } GeoIPLookup;
    
    typedef enum {
        GEOIP_STANDARD = 0,
        GEOIP_MEMORY_CACHE = 1,
        GEOIP_CHECK_CACHE = 2,
        GEOIP_INDEX_CACHE = 4,
        GEOIP_MMAP_CACHE = 8,
    } GeoIPOptions;

    typedef struct GeoIPRecordTag {
        char *country_code;
        char *country_code3;
        char *country_name;
        char *region;
        char *city;
        char *postal_code;
        float latitude;
        float longitude;
        union {
            int metro_code;   /* metro_code is a alias for dma_code */
            int dma_code;
        };
        int area_code;
        int charset;
        char *continent_code;
        int netmask;
    } GeoIPRecord;

    GeoIP * GeoIP_open(const char * filename, int flags);
    GeoIPRecord * GeoIP_record_by_ipnum(GeoIP * gi, unsigned long ipnum);
    GeoIPRecord * GeoIP_record_by_name(GeoIP * gi, const char *host);
    void GeoIPRecord_delete(GeoIPRecord *gir);
]]

local Database = Object:extend()

function Database:initialize(name)
    self.handle = geoip.GeoIP_open(name, geoip.GEOIP_STANDARD)

    if self.handle == nil then
        self.handle = nil
        error("Could not open geoip database: "..tostring(name))
    end
end


function Database:lookup(host)
    local f = geoip.GeoIP_record_by_ipnum

    if type(host) == "string" then
        f = geoip.GeoIP_record_by_name
    end

    local record = ffi.gc(f(self.handle, host), function(record)
        if record ~= nil then
            geoip.GeoIPRecord_delete(record)
        end
    end)

    if record == nil then
        return
    end

    return {
        country_code        = record.country_code ~= nil and ffi.string(record.country_code) or nil,
        country_code3       = record.country_code3 ~= nil and ffi.string(record.country_code3) or nil,
        country_name        = record.country_name ~= nil and ffi.string(record.country_name) or nil,
        region              = record.region ~= nil and ffi.string(record.region) or nil,
        city                = record.city ~= nil and ffi.string(record.city) or nil,
        postal_code         = record.postal_code ~= nil and ffi.string(record.postal_code) or nil,
        latitude            = record.latitude,
        longitude           = record.longitude,
        dma_code            = record.dma_code,
        area_code           = record.area_code,
        charset             = record.charset,
        continent_code      = record.continent_code ~= nil and ffi.string(record.continent_code) or nil,
        netmask             = record.netmask
    }
end

return {
    Database = Database,
}