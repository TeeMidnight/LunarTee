/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "gameworld.h"
#include "entity.h"
#include "gamecontext.h"

#include <algorithm>
#include <utility>
#include <engine/shared/config.h>

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

	UpdatePlayerMaps();
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

bool distCompare(std::pair<float,int> a, std::pair<float,int> b)
{
	return (a.first < b.first);
}

void CGameWorld::UpdatePlayerMaps()
{
	if (Server()->Tick() % g_Config.m_SvMapUpdateRate) 
		return;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!Server()->ClientIngame(i)) 
			continue;

		if(!GameServer()->m_apPlayers[i])
			continue;
	
		if(GameServer()->m_apPlayers[i]->GameWorld() != this)
			continue;

		int* map = Server()->GetIdMap(i);
		int MaxClients = (Server()->Is64Player(i) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS);

		std::vector<std::pair<float,int>> Dist;

		// compute distances
		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			std::pair<float,int> temp;
			temp.second = j;
			temp.first = 1e10;

			Dist.push_back(temp);

			if (!Server()->ClientIngame(j))
				continue;
			if(!GameServer()->m_apPlayers[j])
				continue;
			if(GameServer()->m_apPlayers[j]->GameWorld() == this)
				Dist[Dist.size()-1].first = distance(GameServer()->m_apPlayers[i]->m_ViewPos, GameServer()->m_apPlayers[j]->m_ViewPos);
			else
				Dist[Dist.size()-1].first = 1e8;
		}

		for (int j = 0; j < (int) GameServer()->m_vpBotPlayers.size(); j++)
		{
			if(GameServer()->m_vpBotPlayers[j]->GameWorld() != this)
				continue;
				
			std::pair<float,int> temp;
			temp.first = distance(GameServer()->m_apPlayers[i]->m_ViewPos, GameServer()->m_vpBotPlayers[j]->m_ViewPos);
			temp.second = GameServer()->m_vpBotPlayers[j]->GetCID();

			Dist.push_back(temp);
		}

		// always send the player themselves, even if all in same position
		Dist[i].first = -1;

		std::nth_element(&Dist[0], &Dist[MaxClients - 1], &Dist[Dist.size()], distCompare);

		int Index = 1; // exclude self client id
		for(int j = 0; j < MaxClients - 1; j++)
		{
			map[j + 1] = -1; // also fill player with empty name to say chat msgs
			if(Dist[j].second == i || Dist[j].first > 5e9f)
				continue;
			map[Index++] = Dist[j].second;
		}

		// sort by real client ids, guarantee order on distance changes, O(Nlog(N)) worst case
		// sort just clients in game always except first (self client id) and last (fake client id) indexes
		std::sort(&map[1], &map[minimum(Index, MaxClients - 1)]);
	}
	
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

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index&CCollision::COLFLAG_SOLID || Index&CCollision::COLFLAG_DEATH) continue;
			int GroudIndex = pTiles[(y+1)*pTileMap->m_Width+x].m_Index;
			if(GroudIndex&CCollision::COLFLAG_SOLID && random_int(1, 100) >= y*100/pTileMap->m_Height)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				if(Index&CCollision::COLFLAG_MOONCENTER)
					m_vSpawnPoints[0].push_back(Pos);
				else m_vSpawnPoints[1].push_back(Pos);
			}
		}
	}
}

bool CGameWorld::GetSpawnPos(bool IsBot, vec2& SpawnPos)
{
	for(int i = 0;i < (int) m_vSpawnPoints[IsBot].size(); i++)
	{
		SpawnPos = m_vSpawnPoints[IsBot][random_int(0, m_vSpawnPoints[IsBot].size()-1)];
		if(!ClosestCharacter(SpawnPos, 128.0f, 0x0))
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