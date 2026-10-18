#ifndef WEAPONS_H_INCLUDED
#define WEAPONS_H_INCLUDED

extern int quakemillis;
extern char *deathmessage;

enum
{
    WEAP_FIST = 0,
    WEAP_SLUGSHOT,
    WEAP_MG,
    WEAP_ROCKETL,
    WEAP_SNIPER,
    WEAP_FLAMEJET,
    WEAP_CROSSBOW,
    WEAP_GRENADIER,
    WEAP_HEALER,
    WEAP_MORTAR,
    WEAP_PISTOL,
    WEAP_INFECTED,
    WEAP_BITE,
    WEAP_BARREL,
    WEAP_INSANITY,
    WEAP_SLIME,

    NUMWEAPS,
    WEAP_NONE = NUMWEAPS,
    WEAP_USABLE_LAST = WEAP_BITE,
};

enum parttypes
{
    PT_REGULAR_SPLASH = 0,
    PT_REGULAR_FLAME,

    PT_SPLASH,
    PT_TRAIL,
    PT_FLARE,
    PT_FIREBALL,
};

enum projpartpresets
{
    PP_NONE = 0,
    PP_MUZZLE_FLASH_1,
    PP_MUZZLE_FLASH_2,
    PP_MUZZLE_FLASH_3,
    PP_MUZZLE_FLASH_4,
    PP_MUZZLE_FLASH_5,
    PP_MUZZLE_FLASH_6,
    PP_MUZZLE_FLASH_7,
    PP_MUZZLE_FLASH_8,
    PP_MUZZLE_FLASH_9,
    PP_MUZZLE_FLASH_10,
    PP_SMOKE_SPLASH_1,
    PP_SMOKE_SPLASH_2,
    PP_SMOKE_SPLASH_3,
    PP_SMOKE_SPLASH_4,
    PP_FLAME_SPLASH_1,
    PP_FLAME_SPLASH_2,
    PP_FLAME_SPLASH_3,
    PP_STREAK_FLARE_1,
    PP_STREAK_FLARE_2,
    PP_STREAK_FLARE_3,
    PP_STREAK_FLARE_4,
    PP_SPARK_SPLASH_1,
    PP_SPARK_SPLASH_2,
    PP_SPARK_SPLASH_3,
    PP_SPARK_SPLASH_4,
    PP_SMOKE_TRAIL_1,
    PP_SMOKE_TRAIL_2,
    PP_SMOKE_TRAIL_3,
    PP_WISPS_TRAIL,
    PP_SPARK_TRAIL,
    PP_HEAL_TRAIL,
    PP_EXPLOSION_RED,
    PP_EXPLOSION_BLUE,
    PP_EXPLOSION_YELLOW,
    PP_EXPLOSION_BLACK,
    PP_NUM,
};

enum WeaponEffect_e
{
    WE_NONE     = 0 << 0,
    WE_SLIMED   = 1 << 0,
};
typedef int WeaponEffect;

struct AppliedWeaponEffect
{
    WeaponEffect type;
    int endTime;
};

enum lastattackgun
{
    LA_WEAPSWITCH       = -16,
    LA_NOAMMO           = -17
};

static const struct projpartpreset { short type, part, num, fade, radius, gravity; int color; float size; } projpartpresets[PP_NUM] =
{
    { },
    // type                part                num    fade    radius    gvt    color        size
    { PT_FLARE,            PART_MUZZLE_FLASH3,    0,    200,    0,        0,    0xFFEEAA,    2.75f    },    // PP_MUZZLE_FLASH_1
    { PT_FLARE,            PART_MUZZLE_FLASH2,    0,    250,    0,        0,    0xEEAA99,    3.0f    },    // PP_MUZZLE_FLASH_2
    { PT_FLARE,            PART_MUZZLE_FLASH1,    0,    600,    0,        0,    0x906020,    2.7f    },    // PP_MUZZLE_FLASH_3
    { PT_FLARE,            PART_MUZZLE_FLASH2,    0,    200,    0,        0,    0xFFFFFF,    1.5f    },    // PP_MUZZLE_FLASH_4
    { PT_FLARE,            PART_MUZZLE_FLASH3,    0,    150,    0,        0,    0xFFFFFF,    1.25f    },    // PP_MUZZLE_FLASH_5
    { PT_FLARE,            PART_MUZZLE_FLASH1,    0,    100,    0,        0,    0xFFEEAA,    2.75f    },    // PP_MUZZLE_FLASH_6
    { PT_FLARE,            PART_MUZZLE_FLASH1,    0,    200,    0,        0,    0xFFFFFF,    1.25f    },    // PP_MUZZLE_FLASH_7
    { PT_FLARE,            PART_MUZZLE_FLASH1,    0,    350,    0,        0,    0xF54C2C,    1.3f    },    // PP_MUZZLE_FLASH_8
    { PT_FLARE,            PART_MUZZLE_FLASH1,    0,    600,    0,        0,    0x664C75,    2.7f    },    // PP_MUZZLE_FLASH_9
    { PT_FLARE,            PART_MUZZLE_FLASH2,    0,    200,    0,        0,    0xAA8844,    4.0f    },    // PP_MUZZLE_FLASH_10
    { PT_REGULAR_SPLASH,PART_SMOKE,            5,    300,    25,        -20,0x404040,    1.6f    },    // PP_SMOKE_SPLASH_1
    { PT_REGULAR_SPLASH,PART_SMOKE,            2,    300,    150,    -20,0x404040,    0.6f    },    // PP_SMOKE_SPLASH_2
    { PT_REGULAR_SPLASH,PART_SMOKE,            5,    300,    50,        -20,0x404040,    2.4f    },    // PP_SMOKE_SPLASH_3
    { PT_REGULAR_SPLASH,PART_SMOKE,            2,    150,    50,        -20,0x404040,    2.4f    },    // PP_SMOKE_SPLASH_4
    { PT_REGULAR_SPLASH,PART_FJFLAME,        1,    1000,    60,        -35,0xFF6633,    10.0f    },    // PP_FLAME_SPLASH_1
    { PT_REGULAR_SPLASH,PART_FLAME,            8,    100,    50,        -5,    0x906020,    2.6f    },    // PP_FLAME_SPLASH_2
    { PT_REGULAR_SPLASH,PART_FJFLAME,        1,    1000,    60,        -35,0x664CB2,    10.0f    },    // PP_FLAME_SPLASH_3
    { PT_FLARE,            PART_STREAK,        0,    600,    0,        0,    0xFFC864,    2.8f    },    // PP_STREAK_FLARE_1
    { PT_FLARE,            PART_STREAK,        0,    300,    0,        0,    0xFFC864,    0.28f    },    // PP_STREAK_FLARE_2
    { PT_FLARE,            PART_SHOCKWAVE,        0,    5,        0,        0,    0xFFC864,    0.5f    },    // PP_STREAK_FLARE_3
    { PT_FLARE,            PART_SHOCKWAVE,        0,    5,        0,        0,    0xFF5543,    0.5f    },    // PP_STREAK_FLARE_4
    { PT_SPLASH,        PART_SPARK,            100,250,    150,    2,    0xB49B4B,    0.24f    },    // PP_SPARK_SPLASH_1
    { PT_SPLASH,        PART_SPARK,            50,    250,    150,    2,    0xB45B3B,    0.4f    },    // PP_SPARK_SPLASH_2
    { PT_SPLASH,        PART_SPARK,            50,    1000,    80,        -1,    0xE54C2C,    0.5f    },    // PP_SPARK_SPLASH_3
    { PT_SPLASH,        PART_SPARK,            100,250,    150,    2,    0xFF5543,    0.24f    },    // PP_SPARK_SPLASH_4
    { PT_TRAIL,            PART_SMOKE,            0,    500,    30,        20,    0x404040,    1.4f    },    // PP_SMOKE_TRAIL_1
    { PT_TRAIL,            PART_SMOKE,            0,    200,    10,        100,0x508080,    0.5f    },    // PP_SMOKE_TRAIL_2
    { PT_TRAIL,            PART_SMOKE,            0,    200,    0,        100,0x905045,    0.4f    },    // PP_SMOKE_TRAIL_3
    { PT_TRAIL,            PART_WISPS,            0,    350,    20,        100,0x404040,    1.0f    },    // PP_WISPS_TRAIL
    { PT_TRAIL,            PART_SPARK,            0,    1000,    50,        0,    0xE54C2C,    0.5f    },    // PP_SPARK_TRAIL
    { PT_TRAIL,            PART_HEAL,            0,    500,    50,        0,    0x4CE52C,    0.5f    },    // PP_HEAL_TRAIL
    { PT_FIREBALL,        PART_EXPLOSION,        0,    1000,    60,        0,    0xFF0404,    20.0f    },    // PP_EXPLOSION_RED 0xFF8080
    { PT_FIREBALL,        PART_EXPLOSION,        0,    1000,    60,        0,    0x8080FF,    20.0f    },    // PP_EXPLOSION_BLUE
    { PT_FIREBALL,        PART_EXPLOSION,        0,    1000,    100,    0,    0xFF5004,    20.0f    },    // PP_EXPLOSION_YELLOW 0xFFF080
    { PT_FIREBALL,        PART_EXPLOSION,        0,    700,    30,        0,    0x201010,    10.0f    },    // PP_EXPLOSION_BLACK
    // temporarily, new explosion effects for weapons should probably also be added to explode() in "weapon.cpp"
};

enum projtypes
{
    PJ_RAY = 0,
    PJ_BOUNCER,
    PJ_PROJECTILE,
    PJ_FLAME,
    PJ_STUB,
    PJ_SPECIAL, // sniper rifle IR scope

    PJT_HOME        = 1<<8,
    PJT_TIMED        = 1<<9,
    PJT_STICKY        = 1<<10,
    //PJT_EXPLODE        = 1<<17,
};

static const char * const projmodels[] =
{
        "gamemodels/projectiles/bullet",            // 0
        "gamemodels/projectiles/grenade",    // 1
        "gamemodels/projectiles/rocket",    // 2
        "gamemodels/projectiles/rocket",    // 3
        "gamemodels/projectiles/sawblade",        // 4
        "gamemodels/projectiles/sawblade",        // 5 -> knife
};

static const char * const gibmodels[] = { "playermodels/shared/gibs/gib01", "playermodels/shared/gibs/gib02", "playermodels/shared/gibs/gib03", "playermodels/shared/gibs/gib04", "playermodels/shared/gibs/gib05", "playermodels/shared/gibs/gib06" };
static const char * const debrismodels[] = { "gamemodels/debris/debris01", "gamemodels/debris/debris02", "gamemodels/debris/debris03", "gamemodels/debris/debris04" };
static const char * const barreldebrismodels[] = { "gamemodels/barreldebris/debris01", "gamemodels/barreldebris/debris02", "gamemodels/barreldebris/debris03", "gamemodels/barreldebris/debris04" };

// a negative 'numrays' denotes random offsetting
// projparts[] =  muzzle, trail_1, trail_2, explosion
// todo: implement 'projlife'
static const struct weapinfo {
    short sound, looping, icon, attackdelay, kickamount, range, power, damage, numrays, offset, numshots,
        projtype, projmdl, projspeed, projradius, projgravity, projlife, projfree, decal; float decalsize, muzzlelightsize, quakemul; vec color; uchar projparts[4]; int hiteffects;
    short sound2, looping2, icon2, attackdelay2, kickamount2, range2, power2, damage2, numrays2, offset2, numshots2, // altfire
        projtype2, projmdl2, projspeed2, projradius2, projgravity2, projlife2, projfree2, decal2; float decalsize2, muzzlelightsize2, quakemul2; vec color2; uchar projparts2[4]; int hiteffects2;
    short id; const char *name, *file, *emptyFile; bool scoped, autodown; } weapons[] =
{
    //  sound                   lp      icon            atckdly kick    range   power   damage  numrays offset  numshots        projtype                        mdl     prjspd  rad     gvt     prjlife -free   decal           dclsz   mzllsz  quakem  color                           projparts                                                                               hiteffects
    //  id              name,           file,           empty file      scoped, autodown
    {   S_KNIFE,                0,      HICON_KNIFE,    550,    0,      14,     198,    50,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   0.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        S_KNIFE,                0,      HICON_KNIFE,    550,    0,      1024,   198,    50,     1,      0,      1,              PJ_PROJECTILE,                  5,      45,     0,      100,    0,      0,      -1,             0.0f,   0.0f,   0.0f,   vec(0,0,0),                     {0, PP_SMOKE_TRAIL_2, 0, PP_SPARK_SPLASH_2 },                                           0,
        WEAP_FIST,      "Knife",        "fist",         "emptyFist",    false,  false },

    {   S_SG,                   0,      HICON_SG,       1400,   20,     1024,   130,    10,     20,     4,      1,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      DECAL_BULLET,   2.0f,   30.0f,  1.0f,   vec(0.5f, 0.375f, 0.25f),       {PP_MUZZLE_FLASH_1, PP_STREAK_FLARE_2, 0, 0 },                                          0,
        S_SG,                   0,      HICON_SG,       1600,   20,     1024,   130,    70,     1,      0,      2,              PJ_PROJECTILE,                  2,      100,    30,     0,      0,      0,      DECAL_BULLET,   2.0f,   30.0f,  1.0f,   vec(0.2f, 0.1f, 0.1f),          {PP_MUZZLE_FLASH_1, PP_SMOKE_SPLASH_4, 0, PP_EXPLOSION_BLACK },                         0,
        WEAP_SLUGSHOT,  "Slugshot",     "shotg",        NULL,           false,  false },

    {   S_CG,                   0,      HICON_CG,       100,    7,      1024,   80,     25,     1,      1,      1,              PJ_PROJECTILE,                  0,      512,    0,      0,      0,      0,      DECAL_BULLET,   2.0f,   30.0f,  1.0f,   vec(0.5f, 0.375f, 0.25f),       {PP_MUZZLE_FLASH_6, PP_STREAK_FLARE_3, 0, PP_SPARK_SPLASH_1 },                          0,
        S_CG,                   0,      HICON_CG,       130,    7,      1024,   80,     40,     1,      1,      2,              PJ_PROJECTILE,                  0,      200,    0,      0,      0,      0,      DECAL_BULLET,   2.0f,   30.0f,  1.0f,   vec(0.5f, 0.15f, 0.10f),        {PP_MUZZLE_FLASH_6, PP_STREAK_FLARE_4, 0, PP_SPARK_SPLASH_4 },                          0,
        WEAP_MG,        "Assault Rifle","chaing",       NULL,           false,  false },

    {   S_RLFIRE,               0,      HICON_RL,       1200,   15,     1024,   180,    110,    1,      0,      1,              PJ_PROJECTILE,                  2,      45,     60,     0,      0,      0,      DECAL_SCORCH,   30.0f,  25.0f,  1.6f,   vec(0.6f, 0.3f, 0.15f),         {PP_MUZZLE_FLASH_2, PP_SMOKE_SPLASH_3, PP_FLAME_SPLASH_2, PP_EXPLOSION_RED },           0,
        S_RLFIRE,               0,      HICON_RL,       2000,   15,     2048,   180,    100,    1,      0,      2,              PJ_PROJECTILE|PJT_HOME,         2,      15,     60,     0,      0,      0,      DECAL_SCORCH,   30.0f,  25.0f,  1.6f,   vec(0.6f, 0.55f, 0.15f),        {PP_MUZZLE_FLASH_2, PP_SMOKE_SPLASH_3, PP_FLAME_SPLASH_2, PP_EXPLOSION_YELLOW },        0,
        WEAP_ROCKETL,   "Rocket Launcher","rocket",     NULL,           false,  true },

    {   S_RIFLE,                0,      HICON_RIFLE,    1500,   30,     2048,   140,    100,    1,      0,      1,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      DECAL_BULLET,   3.0f,   25.0f,  1.0f,   vec(0.5f, 0.375f, 0.25f),       {PP_MUZZLE_FLASH_5, PP_SMOKE_TRAIL_1, 0, PP_SPARK_SPLASH_1 },                           0,
        S_ITEMHEALTH,           0,      HICON_RIFLE,    250,    0,      2048,   140,    800,    1,      0,      1,              PJ_SPECIAL,                     0,      0,      0,      0,      0,      0,      DECAL_BULLET,   3.0f,   25.0f,  1.0f,   vec(0.5f, 0.275f, 0.15f),       {0, 0, 0, 0 },                                                                          0,
        WEAP_SNIPER,    "Sniper Rifle", "rifle",        NULL,           true,   true },

    {   S_FLAME,                1,      HICON_FT,       180,    0,      1024,   135,    10,     1,      0,      1,              PJ_FLAME,                       0,      11,     30,     -1,     0,      0,      DECAL_SCORCH,   20.0f,  25.0f,  1.0f,   vec(1.f, 0.4f, 0.2f),           {PP_MUZZLE_FLASH_3, PP_FLAME_SPLASH_1, 0, 0 },                                          0,
        S_FLAME,                1,      HICON_FT,       300,    50,      1024,  135,    0,      1,      0,      2,              PJ_FLAME,                       0,      1,      30,     -1,     0,      0,      -1,             20.0f,  25.0f,  1.0f,   vec(0.4f, 0.3f, 0.7f),          {PP_MUZZLE_FLASH_9, PP_FLAME_SPLASH_3, 0, 0 },                                          0,
        WEAP_FLAMEJET,  "Flame Jet",    "flameg",       NULL,           false,  false },

    {   S_CBOW,                 0,      HICON_SAWBLADE, 1500,   25,     2048,   140,    100,    1,      0,      1,              PJ_PROJECTILE,                  4,      800,    0,      0,      0,      0,      DECAL_CRACK,    2.5f,   0.0f,   1.0f,   vec(0,0,0),                     {0, PP_SMOKE_TRAIL_2, 0, PP_SPARK_SPLASH_2 },                                           0,
        S_CBOW,                 0,      HICON_SAWBLADE, 400,    10,     1024,   140,    50,     1,      0,      2,              PJ_BOUNCER,                     4,      800,   10,     10,      1500,   0,      DECAL_CRACK,    2.5f,   0.0f,   1.0f,   vec(0,0,0),                     {0, PP_SMOKE_TRAIL_2, 0, PP_SPARK_SPLASH_2 },                                           0,
        WEAP_CROSSBOW,  "Crossbow",     "cbow",         NULL,           true,  false },

    {   S_FLAUNCH,              0,      HICON_GL,       500,    10,     1024,   130,    80,     1,      0,      1,              PJ_BOUNCER|PJT_TIMED,           1,      250,    50,     0,      1500,   0,      DECAL_SCORCH,   25.0f,  20.0f,  1.6f,   vec(0.25f, 1.0f, 1.0f),         {PP_MUZZLE_FLASH_4, PP_SMOKE_SPLASH_4, 0, PP_EXPLOSION_BLUE },                          0,
        S_FLAUNCH,              0,      HICON_GL,       500,    10,     1024,   130,    100,    1,      0,      5,              PJ_BOUNCER|PJT_STICKY|PJT_TIMED,1,      80,     30,     0,      15000,  100,    DECAL_SCORCH,   25.0f,  20.0f,  1.6f,   vec(0.01f, 0.06f, 0.06f),       {PP_MUZZLE_FLASH_4, PP_SMOKE_SPLASH_4, 0, PP_EXPLOSION_BLUE },                          0,
        WEAP_GRENADIER, "Grenadier",    "gl",           NULL,           false,  false },

    {   S_ITEMHEALTH,           0,      HICON_HEAL,     800,    0,      1024,   150,    -30,    1,      0,      1,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.5f,   1.0f,   vec(0.8f, 0.3f, 0.7f),          {PP_MUZZLE_FLASH_8, PP_HEAL_TRAIL, 0, 0/*PP_SPARK_SPLASH_3*/ },                         0,
        S_ITEMHEALTH,           0,      HICON_HEAL,     800,    0,      1024,   150,    60,     1,      0,      1,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.5f,   1.0f,   vec(0.8f, 0.3f, 0.7f),          {PP_MUZZLE_FLASH_8, PP_SPARK_TRAIL, 0, 0/*PP_SPARK_SPLASH_3*/ },                        0,
        WEAP_HEALER,    "Healer",       "healer",       NULL,           false,  false },

    {   S_FLAUNCH,              0,      HICON_RL,       2200,   30,     4096,   170,    70,     1,      4,      1,              PJ_PROJECTILE,                  1,      50,     200,    260,    0,      0,      DECAL_SCORCH,   50.0f,  40.0f,  4.0f,   vec(0.8f, 0.7f, 0.4f),          {PP_MUZZLE_FLASH_10, 0, 0, PP_EXPLOSION_YELLOW },                                       0,
        S_FLAUNCH,              0,      HICON_RL,       2200,   30,     4096,   170,    70,     1,      4,      1,              PJ_PROJECTILE,                  1,      50,     200,    160,    0,      0,      DECAL_SCORCH,   50.0f,  40.0f,  4.0f,   vec(0.8f, 0.7f, 0.4f),          {PP_MUZZLE_FLASH_10, 0, 0, PP_EXPLOSION_YELLOW },                                       0,
        WEAP_MORTAR,    "Mortar",       "mortar",       "emptyFist",    false,  true },

    {   S_PISTOL,               0,      HICON_PISTOL,   250,    7,      1024,   80,     20,     1,      0,      1,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      DECAL_BULLET,   2.0f,   15.0f,  1.0f,   vec(0.5f, 0.375f, 0.25f),       {PP_MUZZLE_FLASH_5, PP_STREAK_FLARE_2, 0, PP_SPARK_SPLASH_1 },                          0,
        S_PISTOL,               0,      HICON_PISTOL,   500,    7,      1024,   80,     10,     3,      1,      2,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      DECAL_BULLET,   2.0f,   15.0f,  1.0f,   vec(0.5f, 0.375f, 0.25f),       {PP_MUZZLE_FLASH_5, PP_STREAK_FLARE_2, 0, PP_SPARK_SPLASH_1 },                          0,
        WEAP_PISTOL,    "Pistol",       "pistol",       NULL,           false,  false },

    {   S_PIGR1,                0,      HICON_BITE,     250,    1,      14,     198,    12,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        S_PIGR1,                0,      HICON_BITE,     500,    1,      14,     198,    50,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        WEAP_INFECTED,  "Infected",     NULL,           NULL,           false,  false },

    {   S_PIGR1,                0,      HICON_BITE,     250,    1,      12,     198,    12,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        S_PIGR1,                0,      HICON_BITE,     500,    1,      12,     198,    50,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        WEAP_BITE,      "Bite",         NULL,           NULL,           false,  false },

    {   -1,                     0,      HICON_BARREL,   0,      0,      0,      0,      120,    1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        -1,                     0,      HICON_BARREL,   0,      0,      0,      0,      120,    1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0 },                                                                          0,
        WEAP_BARREL,    "Barrel",       NULL,           NULL,           false,  false },

    {   -1,                     0,      HICON_INSANITY, 0,      0,      0,      0,      20,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0},                                                                          0,
        -1,                     0,      HICON_INSANITY, 0,      0,      0,      0,      20,     1,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0},                                                                          0,
        WEAP_INSANITY,  "Insanity",     NULL,           NULL,           false,  false },

    {   -1,                     0,      HICON_INSANITY, 1500,   30,     100,    0,      5,      3,      4,      0,              PJ_PROJECTILE,                  0,      45,     30,     100,    0,      0,      DECAL_BULLET,   20.0f,  25.0f,  1.0f,   vec(0,0,0),                     {PP_MUZZLE_FLASH_5, PP_SMOKE_TRAIL_1, 0, PP_SPARK_SPLASH_1},                           WE_SLIMED,
        -1,                     0,      HICON_INSANITY, 1500,   30,     100,    0,      5,      3,      4,      0,              PJ_PROJECTILE,                  0,      45,     30,     100,    0,      0,      DECAL_BULLET,   20.0f,  25.0f,  1.0f,   vec(0,0,0),                     {PP_MUZZLE_FLASH_5, PP_SMOKE_TRAIL_1, 0, PP_SPARK_SPLASH_1},                           WE_SLIMED,
        WEAP_SLIME,     "Slime",        NULL,           NULL,           false,  false },

    {   -1,                     0,      HICON_INSANITY, 0,      0,      0,      0,      0,      0,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0},                                                                          0,
        -1,                     0,      HICON_INSANITY, 0,      0,      0,      0,      0,      0,      0,      0,              PJ_RAY,                         0,      0,      0,      0,      0,      0,      -1,             0.0f,   0.0f,   1.0f,   vec(0,0,0),                     {0, 0, 0, 0},                                                                          0,
        WEAP_NONE,      "None",         NULL,           NULL,           false,  false }
};


#define GUN_FRAG_NORMAL                     "killed"
#define GUN_FRAGBY_NORMAL                   "killed"
#define GUN_FRAG_MELEE                      "cut"
#define GUN_FRAGBY_MELEE                    "cut"
#define GUN_FRAG_INFECTED                   "ate"
#define GUN_FRAGBY_INFECTED                 "eaten"
#define GUN_FRAG_EXPLODE                    "blew up"
#define GUN_FRAGBY_EXPLODE                  "blown up"
#define GUN_FRAG_BURN                       "burned up"
#define GUN_FRAGBY_BURN                     "burned up"
#define GUN_FRAG_INSANITY                   "insanatized"
#define GUN_FRAGBY_INSANITY                 "insanatized"

#define GUN_SUICIDE_FALL                    deathmessage
#define GUN_SUICIDE_LAVA                    "incinirated"
#define GUN_SUICIDE_MAT                     "incinirated"

#define GUN_FRAG_SPECIAL                    " with a headshot!"
#define GUN_FRAG_SPECIAL_INFECTED           " by eating his brain!"
#define GUN_EXPLODE_SPECIAL                 " with a direct hit!"

#define GUN_FRAG_MESSAGE(gun, infected)     ((infected && WEAP_IS_MELEE(gun)) ? GUN_FRAG_INFECTED : WEAP_IS_FLAME(gun)?GUN_FRAG_BURN: (WEAP_IS_EXPLOSIVE(gun) && WEAPONI(gun) != WEAP_CROSSBOW ?GUN_FRAG_EXPLODE: (WEAP_IS_MELEE(gun)?GUN_FRAG_MELEE: (WEAP_IS_INSANITY(gun) ? GUN_FRAG_INSANITY : GUN_FRAG_NORMAL))))
#define GUN_FRAGBY_MESSAGE(gun, infected)   ((infected && WEAP_IS_MELEE(gun)) ? GUN_FRAGBY_INFECTED : WEAP_IS_FLAME(gun)?GUN_FRAGBY_BURN: (WEAP_IS_EXPLOSIVE(gun)?GUN_FRAGBY_EXPLODE: (WEAP_IS_MELEE(gun)?GUN_FRAGBY_MELEE: (WEAP_IS_INSANITY(gun) ? GUN_FRAGBY_INSANITY : GUN_FRAGBY_NORMAL))))
#define GUN_SPECIAL_MESSAGE(gun, infected)  (WEAP_IS_EXPLOSIVE(gun) ? GUN_EXPLODE_SPECIAL : (WEAP_IS_MELEE(gun) && infected ? GUN_FRAG_SPECIAL_INFECTED : GUN_FRAG_SPECIAL))

#define GUN_SUICIDE_MESSAGE_INSERT_YOU(gun, msg) \
                                            (gun == SUICIDE_TYPE_FALL ? "" : msg)

#define GUN_SUICIDE_MESSAGE(gun)                (\
    (gun == SUICIDE_TYPE_LAVA ? GUN_SUICIDE_LAVA :  (\
        gun == SUICIDE_TYPE_FALL ? GUN_SUICIDE_FALL :   (\
            gun == SUICIDE_TYPE_MAT ? GUN_SUICIDE_MAT : GUN_FRAG_NORMAL))))

#define GUN_MAX_RAYS                        32
#define GUN_MIN_SPREAD                      1
#define GUN_MAX_SPREAD                      4
#define GUN_HEADSHOT_MUL                    1.5f
#define GUN_EXP_SELFDAMDIV                  2
#define GUN_EXP_DISTSCALE                   1.5f

#define GUN_AMMO_MAX(gun)                   ((200-WEAPON(gun).power)/2)
#define GUN_AMMO_ADD(gun,info)              ((200-WEAPON(gun).power)/(5-(info)))

#define WEAPONI(gun)                        (gun%1024)
#define WEAPON(gun)                         (weapons[WEAPONI(gun)])
#define WEAP(gun,m)                         (gun>=1024?weapons[gun%1024].m##2:weapons[gun].m)

#define WEAP_IS_EXPLOSIVE(gun)              (WEAP(gun,projradius)>0)
#define WEAP_NAME(gun)                      (WEAPON(gun).name)
#define WEAP_PROJTYPE(gun)                  (WEAP(gun,projtype)&0xFF)

#define WEAP_IS_MELEE(gun)                  (WEAPONI(gun)==WEAP_FIST || WEAPONI(gun)==WEAP_BITE || WEAPONI(gun)==WEAP_INFECTED)
#define WEAP_IS_RAY(gun)                    (WEAP_PROJTYPE(gun)==PJ_RAY)
#define WEAP_IS_BOUNCER(gun)                (WEAP_PROJTYPE(gun)==PJ_BOUNCER)
#define WEAP_IS_PROJECTILE(gun)             (WEAP_PROJTYPE(gun)==PJ_PROJECTILE)
#define WEAP_IS_FLAME(gun)                  (WEAP_PROJTYPE(gun)==PJ_FLAME)
#define WEAP_IS_SPECIAL(gun)                (WEAP_PROJTYPE(gun)==PJ_SPECIAL)
#define WEAP_IS_INSANITY(gun)               (WEAPONI(gun)==WEAP_INSANITY)
#define WEAP_IS_OBTAINABLE(gun)             (WEAPONI(gun)==WEAP_MORTAR)

#define WEAP_VALID(gun)                     (WEAPONI(gun)>=0&&WEAPONI(gun)<NUMWEAPS)
#define WEAP_USABLE(gun)                    (WEAPONI(gun)>=WEAP_FIST&&WEAPONI(gun)<=WEAP_USABLE_LAST)
#define WEAP_CLAMP(gun)                     (WEAP_USABLE(gun) ? gun : WEAP_FIST)

#define WEAP_PREF_ORDER                     WEAP_MG, \
                                            WEAP_ROCKETL, \
                                            WEAP_SLUGSHOT, \
                                            WEAP_SNIPER, \
                                            WEAP_FLAMEJET, \
                                            WEAP_CROSSBOW, \
                                            WEAP_GRENADIER, \
                                            WEAP_PISTOL, \
                                            WEAP_FIST, \
                                            WEAP_INFECTED, \
                                            WEAP_BITE

#define WEAP_FALLBACK(ammo)                 (ammo[WEAP_BITE] ? WEAP_BITE : (ammo[WEAP_INFECTED] ? WEAP_INFECTED : WEAP_FIST))

#define MORTAR_MAX_AMMO                     2
#define WEAP_MAX_IRSM                       3000

enum
{
    PCS_OFFENSE,
    PCS_DEFENSE,
    //PCS_HEAVY,
    PCS_STEALTH,
    PCS_ENGINEER,
    NUMPCS
};

enum Abilities
{
    ABILITY_SEE_HEALTH = 1 << 0,
};

bool canshootwith(fpsstate *d, int gun, int gamemode, bool checkAmmo = false);
int getRandomWeapon(fpsstate *d);

#define CAN_SHOOT_WITH(d,gun)               (canshootwith( d, gun, gamemode))
#define CAN_SHOOT_WITH2(d,gun)              (canshootwith(&d, gun, gamemode))

#endif // WEAPONS_H_INCLUDED
