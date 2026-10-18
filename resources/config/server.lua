--------------------------------------------------------------------------------
-- Revelade Revolution Game Server Configuration                               
--------------------------------------------------------------------------------


server {
    -- A server name for players to identify your server.
    name = "Unnamed server",

    -- Default connection information:
    --   Game Server socket binds to UDP 0.0.0.0:16960 
    --   Game Server Info socket binds to UDP 0.0.0.0:<serverport+1> (16961)

    -- Server's IP address (Don't touch unless you know what you're doing)
    -- ip = "0.0.0.0",

    -- Game server port.
    port = 16960,

    -- Set the maximum number of client connections allowed
    maximumClients = 8,

    -- Hostname of the server, this should resolve to the server's ip
    host = "",

    -- The maximum amount of bot non server admins will be allowed to add
    botLimit = 8,

    -- Enables or disables team switching bots
    botBalance = true,
}


--------------------------------------------------------------------------------
-- Master restrictions
--------------------------------------------------------------------------------

module "rr.restrictMaster" {
    -- Does this server allow the following master modes?
    allowVeto           = true,
    allowLocked         = true,
    allowPrivate        = false,

    -- Privilege level that is required to override
    overridePriv        = "PRIV_ADMIN"
}

--------------------------------------------------------------------------------
-- Basic /setmaster based authentication
--------------------------------------------------------------------------------

module "rr.setmaster" {
    -- Allow /setmaster 1
    publicMode = true,
    publicPermission = "PRIV_MASTER",

    -- List of users, !!CHANGE THIS!!
    users = {
        { "PRIV_ADMIN", "canihazpowers?" }
    },
}


--------------------------------------------------------------------------------
-- Message of the day
--------------------------------------------------------------------------------

module "rr.motd" {
    -- The message to send to clients upon connection
    message = "Welcome to the server!"
}

--------------------------------------------------------------------------------
-- Master client
--------------------------------------------------------------------------------

-- This module causes your server to be registered on the serverlist
-- Don't touch unless you know what you're doing
module "rr.masterclient" {
    -- Different name to use in the masterlist
    serverName = nil,

    -- Masterserver info
    host = nil,
    port = nil,
    registerPath = nil,
    updatePath = nil,
    unRegisterPath = nil
}

--------------------------------------------------------------------------------
-- GeoIp
--------------------------------------------------------------------------------

-- This requires the maxmind geocitylite database to work
module "rr.geoip" {
    path = "/usr/local/share/GeoIP/GeoLiteCity.dat"
}

--------------------------------------------------------------------------------
-- Mute control
--------------------------------------------------------------------------------

module "rr.muteControl" {
    defaultMute = {}
}

module "rr.sendto" {
}