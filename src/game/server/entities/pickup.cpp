/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <generated/protocol.h>
#include <game/server/gamecontext.h>
#include "pickup.h"

CPickup::CPickup(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, const char *Name, int Num)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP)
{
	m_Pos = Pos;
	m_StartPos = Pos;
	m_Direction = Dir;
	str_copy(m_aName, Name);
	m_Num = Num;
	m_ProximityRadius = PickupPhysSize;
	m_StartTick = Server()->Tick();

	GameWorld()->InsertEntity(this);
}

vec2 CPickup::GetPos(float Time)
{
	float Curvature = GameWorld()->m_Core.m_Tuning.m_GrenadeCurvature * 2 * GameWorld()->m_Core.m_Tuning.m_Gravity;
	float Speed = GameWorld()->m_Core.m_Tuning.m_GrenadeSpeed;

	return CalcPos(m_StartPos, m_Direction, Curvature, Speed, Time);
}

void CPickup::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);

	if(GameLayerClipped(CurPos))
	{
		GameWorld()->DestroyEntity(this);
		return;
	}
	
	m_Pos = CurPos;

	vec2 LastPos;
	int Collide = Collision()->IntersectLine(PrevPos, CurPos, NULL, &LastPos);
	if(Collide)
	{
		//Thanks to TeeBall 0.6
		vec2 CollisionPos;
		CollisionPos.x = LastPos.x;
		CollisionPos.y = CurPos.y;
		int CollideY = Collision()->IntersectLine(PrevPos, CollisionPos, NULL, NULL);
		CollisionPos.x = CurPos.x;
		CollisionPos.y = LastPos.y;
		int CollideX = Collision()->IntersectLine(PrevPos, CollisionPos, NULL, NULL);
		
		m_StartPos = LastPos;
		m_Pos = m_StartPos;
		vec2 vel;
		vel.x = m_Direction.x;
		vel.y = m_Direction.y + GameWorld()->m_Core.m_Tuning.m_GrenadeCurvature * 2 * GameWorld()->m_Core.m_Tuning.m_Gravity/10000*Ct*GameWorld()->m_Core.m_Tuning.m_GrenadeSpeed;
		
		if (CollideX && !CollideY)
		{
			m_Direction.x = -vel.x;
			m_Direction.y = vel.y;
		}
		else if (!CollideX && CollideY)
		{
			m_Direction.x = vel.x;
			m_Direction.y = -vel.y;
		}
		else
		{
			m_Direction.x = -vel.x;
			m_Direction.y = -vel.y;
		}
		
		m_Direction.x *= (100 - 50) / 100.0;
		m_Direction.y *= (100 - 50) / 100.0;
		m_StartTick = Server()->Tick();
	}

	// Check if a player intersected us
	CCharacter *pChr = GameWorld()->ClosestCharacter(m_Pos, 32.0f, 0);
	if(pChr && pChr->IsAlive() && pChr->GetPlayer() && !pChr->GetPlayer()->IsBot())
	{
		GameServer()->SendChatTarget_Localization(pChr->GetCID(), _("You got %t x%d"),
			m_aName, m_Num);
		GameServer()->Item()->AddInvItemNum(m_aName, m_Num, pChr->GetCID());
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);

		GameWorld()->DestroyEntity(this);
	}

	if((Server()->Tick() - m_StartTick)/Server()->TickSpeed() >= 60)
		GameWorld()->DestroyEntity(this);
}

void CPickup::TickPaused()
{
	m_StartTick++;
}

void CPickup::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_ID, sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = POWERUP_WEAPON;
	pP->m_Subtype = WEAPON_HAMMER;
}
