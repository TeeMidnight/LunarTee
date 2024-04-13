#include "weapon.h"

#include <lunartee/weapons/hammer.h>
#include <lunartee/weapons/hand.h>
#include <lunartee/weapons/gun.h>
#include <lunartee/weapons/shotgun.h>
#include <lunartee/weapons/grenade.h>
#include <lunartee/weapons/rifle.h>
#include <lunartee/weapons/ninja.h>
#include <lunartee/weapons/freeze-rifle.h>

void WeaponInit::InitWeapons(CGameContext *pGameServer)
{
	g_Weapons.InitWeapon(LT_WEAPON_HAMMER, new CWeaponHammer(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_GUN, new CWeaponGun(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_SHOTGUN, new CWeaponShotgun(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_GRENADE, new CWeaponGrenade(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_RIFLE, new CWeaponRifle(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_NINJA, new CWeaponNinja(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_FREEZE_RIFLE, new CWeaponFreezeRifle(pGameServer));
	g_Weapons.InitWeapon(LT_WEAPON_HAND, new CWeaponHand(pGameServer));
}