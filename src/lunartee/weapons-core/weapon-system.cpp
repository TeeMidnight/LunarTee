#include "weapon.h"

void WeaponSystem::InitWeapon(int Number, IWeapon *pWeapon)
{
	m_aWeapons[Number] = pWeapon;
}

WeaponSystem g_Weapons;