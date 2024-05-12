#include <generated/protocol.h>
#include <game/server/gamecontext.h>
#include "tws-laser.h"

CTWSLaser::CTWSLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Damage, int Weapon, bool Freeze)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	m_Damage = Damage;
	m_Weapon = Weapon;
	m_Freeze = Freeze;
	GameWorld()->InsertEntity(this);
	DoBounce();
}


bool CTWSLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	if(!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	pHit->TakeDamage(vec2(0.f, 0.f), m_Damage, m_Owner, m_Weapon);
	if(m_Freeze)
	{
		pHit->Freeze(3.0f);
	}
	return true;
}

void CTWSLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameWorld()->m_Core.m_Tuning.m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameWorld()->m_Core.m_Tuning.m_LaserBounceNum)
				m_Energy = -1;

			GameWorld()->CreateSound(m_Pos, SOUND_RIFLE_BOUNCE);
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CTWSLaser::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CTWSLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameWorld()->m_Core.m_Tuning.m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CTWSLaser::TickPaused()
{
	++m_EvalTick;
}

void CTWSLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(GameServer()->GetClientVersion(SnappingClient) >= VERSION_DDNET_MULTI_LASER)
	{
		CNetObj_DDNetLaser *pObj = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, m_ID, sizeof(CNetObj_DDNetLaser)));
		if(!pObj)
			return;

		pObj->m_ToX = (int)m_Pos.x;
		pObj->m_ToY = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
		pObj->m_Owner = m_Owner;
		pObj->m_Type = m_Freeze ? LASERTYPE_FREEZE : LASERTYPE_RIFLE;
	}
	else
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_FromX = (int)m_From.x;
		pObj->m_FromY = (int)m_From.y;
		pObj->m_StartTick = m_EvalTick;
	}
}
