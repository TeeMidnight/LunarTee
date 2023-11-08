#ifndef GAME_SERVER_DEFINE_H
#define GAME_SERVER_DEFINE_H

#include <base/system.h>

#define MENU_CLOSETICK 200

char* format_int64_with_commas(char commas, int64_t  n);

enum OptionType
{
    MENUOPTION_OPTIONS=0,
    MENUOPTION_ITEMS,
    MENUOPTION_LANGUAGES,

    NUM_MENUOPTIONS,
};

enum LunarTeeWeapons
{
    LT_WEAPON_HAMMER=0,
    LT_WEAPON_GUN,
    LT_WEAPON_SHOTGUN,
    LT_WEAPON_GRENADE,
    LT_WEAPON_RIFLE,
    LT_WEAPON_NINJA,

    LT_WEAPON_FREEZE_RIFLE,

    NUM_LUNARTEE_WEAPONS,
};

enum MenuPages
{
    MENUPAGE_MAIN=0,
    MENUPAGE_NOTMAIN,
    MENUPAGE_ITEM,
    MENUPAGE_LANGUAGE,
};
#endif