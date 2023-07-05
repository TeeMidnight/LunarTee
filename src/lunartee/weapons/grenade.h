#ifndef LUNARTEE_WEAPONS_GRENADE_H
#define LUNARTEE_WEAPONS_GRENADE_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponGrenade : public CWeapon
{
public:
    CWeaponGrenade(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif