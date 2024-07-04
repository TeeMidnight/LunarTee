#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include "hand.h"

CWeaponHand::CWeaponHand(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_HAND, WEAPON_HAMMER, 75, 1)
{
}

void CWeaponHand::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    CCharacter *pOwnerChr = GameServer()->GetPlayerChar(Owner);
    if(!pOwnerChr)
    {
        return;
    }
    
	GameServer()->CreateSound(Pos, SOUND_HAMMER_FIRE);

    int Hits = 0;

	std::vector<CEntity*> vpEnts;

	pGameWorld->FindEntities(Pos, pOwnerChr->m_ProximityRadius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		auto pTarget = (CCharacter *) vpEnts[i];

        if ((pTarget == pOwnerChr) || pGameWorld->Collision()->IntersectLine(Pos, pTarget->m_Pos, NULL, NULL))
            continue;

        // set his velocity to fast upward (for now)
        if(length(pTarget->m_Pos-Pos) > 0.0f)
            GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-Pos)*pOwnerChr->m_ProximityRadius*0.5f);
        else
            GameServer()->CreateHammerHit(Pos);

        vec2 Dir;
        if (length(pTarget->m_Pos - pOwnerChr->m_Pos) > 0.0f)
            Dir = normalize(pTarget->m_Pos - pOwnerChr->m_Pos);
        else
            Dir = vec2(0.f, -1.f);

        pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, GetDamage(),
            pOwnerChr->GetPlayer()->GetCID(), GetShowType());
        Hits++;
    }

    // if we Hit anything, we have to wait for the reload
    if(Hits)
        pOwnerChr->SetReloadTimer(GameServer()->Server()->TickSpeed()/3);

    
    return;
}