/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "entity.h"
#include "gamecontext.h"

//////////////////////////////////////////////////
// Entity
//////////////////////////////////////////////////
CEntity::CEntity(CGameWorld *pGameWorld, int ObjType)
{
	m_pGameWorld = pGameWorld;

	m_ObjType = ObjType;
	m_Pos = vec2(0,0);
	m_ProximityRadius = 0;

	m_MarkedForDestroy = false;
	m_ID = Server()->SnapNewID();

	m_pPrevTypeEntity = 0;
	m_pNextTypeEntity = 0;
}

CEntity::~CEntity()
{
	GameWorld()->RemoveEntity(this);
	Server()->SnapFreeID(m_ID);
}

bool CEntity::NetworkClipped(int SnappingClient)
{
	return NetworkClipped(SnappingClient, m_Pos);
}

bool CEntity::NetworkClipped(int SnappingClient, vec2 CheckPos)
{
	return ::NetworkClipped(GameWorld(), SnappingClient, CheckPos);
}

bool NetworkClipped(CGameWorld *pGameworld, int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == -1)
		return false;
	
	if(pGameworld->GameServer()->m_apPlayers[SnappingClient]->GameWorld() != pGameworld)
		return true;

	float dx = pGameworld->GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	float dy = pGameworld->GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return true;

	return false;
}

bool CEntity::GameLayerClipped(vec2 CheckPos)
{
	return round_to_int(CheckPos.x)/32 > GameWorld()->Collision()->GetWidth()+32 ||
			round_to_int(CheckPos.x)/32 < -32 ? true : false;
}
