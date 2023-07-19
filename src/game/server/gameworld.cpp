/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "gameworld.h"
#include "entity.h"
#include "gamecontext.h"

#include <algorithm>
#include <utility>
#include <engine/shared/config.h>

static int s_SpawnPointLaserNum = 8;

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	m_pGameServer = 0x0;
	m_pServer = 0x0;

	m_ResetRequested = false;
	for(int i = 0; i < NUM_ENTTYPES; i++)
		m_apFirstEntityTypes[i] = 0;
}

CGameWorld::~CGameWorld()
{
	// delete all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		while(m_apFirstEntityTypes[i])
			delete m_apFirstEntityTypes[i];

	for(int i = 0; i < (int) m_vSpawnPointsID.size(); i ++)
	{
		Server()->SnapFreeID(m_vSpawnPointsID[i]);
	}
}

void CGameWorld::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();
}

CEntity *CGameWorld::FindFirst(int Type)
{
	return Type < 0 || Type >= NUM_ENTTYPES ? 0 : m_apFirstEntityTypes[Type];
}

CClientMask CGameWorld::WorldMask()
{
	CClientMask Mask;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameServer()->m_apPlayers[i])
			continue; // Player doesn't exist

		if(GameServer()->m_apPlayers[i]->GameWorld() != this)
			continue;

		Mask.set(i);
	}
	return Mask;
}

void CGameWorld::FindEntities(vec2 Pos, float Radius, std::vector<CEntity*> *vpEnts, int Type)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return;

	vpEnts->clear();

	for(CEntity *pEnt = m_apFirstEntityTypes[Type];	pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			if(vpEnts)
				vpEnts->push_back(pEnt);
		}
	}
}

void CGameWorld::InsertEntity(CEntity *pEnt)
{
	// insert it
	if(m_apFirstEntityTypes[pEnt->m_ObjType])
		m_apFirstEntityTypes[pEnt->m_ObjType]->m_pPrevTypeEntity = pEnt;
	pEnt->m_pNextTypeEntity = m_apFirstEntityTypes[pEnt->m_ObjType];
	pEnt->m_pPrevTypeEntity = 0x0;
	m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt;
}

void CGameWorld::DestroyEntity(CEntity *pEnt)
{
	pEnt->m_MarkedForDestroy = true;
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	// not in the list
	if(!pEnt->m_pNextTypeEntity && !pEnt->m_pPrevTypeEntity && m_apFirstEntityTypes[pEnt->m_ObjType] != pEnt)
		return;

	// remove
	if(pEnt->m_pPrevTypeEntity)
		pEnt->m_pPrevTypeEntity->m_pNextTypeEntity = pEnt->m_pNextTypeEntity;
	else
		m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt->m_pNextTypeEntity;
	if(pEnt->m_pNextTypeEntity)
		pEnt->m_pNextTypeEntity->m_pPrevTypeEntity = pEnt->m_pPrevTypeEntity;

	// keep list traversing valid
	if(m_pNextTraverseEntity == pEnt)
		m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;

	pEnt->m_pNextTypeEntity = 0;
	pEnt->m_pPrevTypeEntity = 0;
}

//
void CGameWorld::Snap(int SnappingClient)
{
	SnapSpawnPoint(SnappingClient); // snap spawn point

	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Snap(SnappingClient);
			pEnt = m_pNextTraverseEntity;
		}
	}
}

void CGameWorld::SnapSpawnPoint(int SnappingClient)
{
	float AngleStep = 2.0f * pi / s_SpawnPointLaserNum;
	for(int i = 0; i < (int) m_vSpawnPoints[0].size(); i ++)
	{
		if(NetworkClipped(this, SnappingClient, m_vSpawnPoints[0][i]))
			continue;
		for(int j = 0; j < s_SpawnPointLaserNum; j ++)
		{
			vec2 Pos = m_vSpawnPoints[0][i] + vec2(16.f * cos(AngleStep*j), 16.f * sin(AngleStep*j));
			vec2 NextPos = m_vSpawnPoints[0][i] + vec2(16.f * cos(AngleStep*(j+1)), 16.f * sin(AngleStep*(j+1)));

			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_vSpawnPointsID[i * s_SpawnPointLaserNum + j], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)Pos.x;
			pObj->m_Y = (int)Pos.y;
			pObj->m_FromX = (int)NextPos.x;
			pObj->m_FromY = (int)NextPos.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
}

void CGameWorld::Reset()
{
	// reset all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Reset();
			pEnt = m_pNextTraverseEntity;
		}
	RemoveEntities();

	GameServer()->m_pController->PostReset();
	RemoveEntities();

	m_ResetRequested = false;
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->m_MarkedForDestroy)
			{
				RemoveEntity(pEnt);
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
}

void CGameWorld::Tick()
{
	if(m_ResetRequested)
		Reset();

	// update all objects
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Tick();
			pEnt = m_pNextTraverseEntity;
		}

	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->TickDefered();
			pEnt = m_pNextTraverseEntity;
		}

	RemoveEntities();
}


// TODO: should be more general
CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2& NewPos, CEntity *pNotThis)
{
	// Find other players
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CCharacter *pClosest = 0;

	CCharacter *p = (CCharacter *)FindFirst(ENTTYPE_CHARACTER);
	for(; p; p = (CCharacter *)p->TypeNext())
 	{
		if(p == pNotThis)
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, p->m_Pos, IntersectPos))
		{
			float Len = distance(p->m_Pos, IntersectPos);
			if(Len < p->m_ProximityRadius+Radius)
			{
				Len = distance(Pos0, IntersectPos);
				if(Len < ClosestLen)
				{
					NewPos = IntersectPos;
					ClosestLen = Len;
					pClosest = p;
				}
			}
		}
	}

	return pClosest;
}

CCharacter *CGameWorld::ClosestCharacter(vec2 Pos, float Radius, CEntity *pNotThis)
{
	// Find other players
	float ClosestRange = Radius*2;
	CCharacter *pClosest = 0;

	std::vector<CEntity*> vpEnts;

	FindEntities(Pos, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		auto p = (CCharacter *) vpEnts[i];

		if(p == pNotThis)
			continue;

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius+Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = p;
			}
		}
	}

	return pClosest;
}
void CGameWorld::InitSpawnPos()
{
	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = Layers()->GameLayer();
	CTile *pTiles = (CTile *) Layers()->Map()->GetData(pTileMap->m_Data);

	for(int y = 0; y < pTileMap->m_Height - 2; y ++)
	{
		for(int x = 0; x < pTileMap->m_Width; x += 5)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index&CCollision::COLFLAG_SOLID || Index&CCollision::COLFLAG_DEATH) 
				continue;

			int GroudIndex = pTiles[(y+1)*pTileMap->m_Width+x].m_Index;
			int UndergroudIndex = pTiles[(y+2)*pTileMap->m_Width+x].m_Index;
			if(!(GroudIndex&CCollision::COLFLAG_SOLID || GroudIndex&CCollision::COLFLAG_DEATH) && UndergroudIndex&CCollision::COLFLAG_SOLID)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				if(Index&CCollision::COLFLAG_MOONCENTER)
				{
					m_vSpawnPoints[0].push_back(Pos);
					for(int i = 0; i < s_SpawnPointLaserNum; i ++)
						m_vSpawnPointsID.push_back(Server()->SnapNewID());
				}
				else 
				{
					m_vSpawnPoints[1].push_back(Pos);
				}
			}
		}
	}
}

bool CGameWorld::GetSpawnPos(bool IsBot, vec2& SpawnPos)
{
	for(int i = 0;i < (int) m_vSpawnPoints[IsBot].size(); i++)
	{
		SpawnPos = m_vSpawnPoints[IsBot][random_int(0, m_vSpawnPoints[IsBot].size()-1)];
		if(!ClosestCharacter(SpawnPos, 48.0f, 0x0))
		{
			return true;
		}
	}
	return false;
}

int CGameWorld::CheckBotInRadius(vec2 Pos, float Radius)
{
	// Find other players
	int Num = 0;
	std::vector<CEntity*> vpEnts;

	FindEntities(Pos, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		auto p = (CCharacter *) vpEnts[i];

		if(p->GetPlayer() && !p->GetPlayer()->IsBot())
			continue;

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius + Radius)
		{
			Num++;
		}
	}

	return Num;
}