/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "eventhandler.h"

#include "entity.h"
#include "gamecontext.h"

#include <base/system.h>
#include <base/vmath.h>

//////////////////////////////////////////////////
// Event handler
//////////////////////////////////////////////////
CEventHandler::CEventHandler()
{
	m_pGameServer = 0;
	Clear();
}

void CEventHandler::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
}

void *CEventHandler::Create(int Type, int Size, CClientMask Mask)
{
	if(m_NumEvents == MAX_EVENTS)
		return 0;
	if(m_CurrentOffset + Size >= MAX_DATASIZE)
		return 0;

	void *p = &m_aData[m_CurrentOffset];
	m_aOffsets[m_NumEvents] = m_CurrentOffset;
	m_aTypes[m_NumEvents] = Type;
	m_aSizes[m_NumEvents] = Size;
	m_aClientMasks[m_NumEvents] = Mask;
	m_CurrentOffset += Size;
	m_NumEvents++;
	return p;
}

void CEventHandler::Clear()
{
	m_NumEvents = 0;
	m_CurrentOffset = 0;
}

void CEventHandler::Snap(int SnappingClient)
{
	for(int i = 0; i < m_NumEvents; i++)
	{
		if(SnappingClient == -1 || m_aClientMasks[i].test(SnappingClient))
		{
			CNetEvent_Common *pEvent = (CNetEvent_Common *)&m_aData[m_aOffsets[i]];
			if(!NetworkClipped(GameWorld(), SnappingClient, vec2(pEvent->m_X, pEvent->m_Y)))
			{
				int Type = m_aTypes[i];
				int Size = m_aSizes[i];
				const char *pData = &m_aData[m_aOffsets[i]];
				if(!Translate(SnappingClient, &Type, &pData))
					continue;

				if(GameServer()->Server()->IsSixup(SnappingClient))
					EventToSixup(&Type, &Size, &pData);

				void *pItem = GameServer()->Server()->SnapNewItem(Type, i, Size);
				if(pItem)
					mem_copy(pItem, pData, Size);
			}
		}
	}
}

bool CEventHandler::Translate(int SnappingClient, int *pType, const char **ppData)
{
	if(*pType == NETEVENTTYPE_DEATH)
	{
		CNetEvent_Death *pEvent = (CNetEvent_Death *)(*ppData);

		if(!GameServer()->Server()->Translate(pEvent->m_ClientID, SnappingClient))
			pEvent->m_ClientID = 0;
	}
	return true;
}

void CEventHandler::EventToSixup(int *pType, int *pSize, const char **ppData)
{
	static char s_aEventStore[128];
	if(*pType == NETEVENTTYPE_DAMAGEIND)
	{
		const CNetEvent_DamageInd *pEvent = (const CNetEvent_DamageInd *)(*ppData);
		protocol7::CNetEvent_Damage *pEvent7 = (protocol7::CNetEvent_Damage *)s_aEventStore;
		*pType = -protocol7::NETEVENTTYPE_DAMAGE;
		*pSize = sizeof(*pEvent7);

		pEvent7->m_X = pEvent->m_X;
		pEvent7->m_Y = pEvent->m_Y;

		pEvent7->m_ClientID = 0;
		pEvent7->m_Angle = pEvent->m_Angle;

		// This will need some work, perhaps an event wrapper for damageind,
		// a scan of the event array to merge multiple damageinds
		// or a separate array of "damage ind" events that's added in while snapping
		pEvent7->m_HealthAmount = 1;

		pEvent7->m_ArmorAmount = 0;
		pEvent7->m_Self = 0;

		*ppData = s_aEventStore;
	}
}