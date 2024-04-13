#ifndef GAME_SERVER_DEFINE_H
#define GAME_SERVER_DEFINE_H

#include <base/system.h>

// TODO: make weapon system use CUuid
enum LunarTeeWeapons
{
    LT_WEAPON_HAMMER = 0,
    LT_WEAPON_GUN,
    LT_WEAPON_SHOTGUN,
    LT_WEAPON_GRENADE,
    LT_WEAPON_RIFLE,
    LT_WEAPON_NINJA,

    LT_WEAPON_FREEZE_RIFLE,
    LT_WEAPON_HAND,

    NUM_LUNARTEE_WEAPONS,
};

#endif