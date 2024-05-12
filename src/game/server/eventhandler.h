/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_EVENTHANDLER_H
#define GAME_SERVER_EVENTHANDLER_H

#include <engine/uuid.h>

#include <map>

class CEventMask
{
	std::map<int, bool> m_ClientMaskMap;
public:
	bool &Get(int ClientID)
	{
		if(!m_ClientMaskMap.count(ClientID))
			m_ClientMaskMap[ClientID] = false;

		return m_ClientMaskMap[ClientID];
	}

	// Test ClientID
	bool Test(int ClientID)
	{
		return m_ClientMaskMap.count(ClientID) && m_ClientMaskMap[ClientID];
	}
};

class CEventHandler
{
	enum
	{
		MAX_EVENTS = 128,
		MAX_DATASIZE = 128 * 64,
	};

	int m_aTypes[MAX_EVENTS]; // TODO: remove some of these arrays
	int m_aOffsets[MAX_EVENTS];
	int m_aSizes[MAX_EVENTS];
	CEventMask m_aClientMasks[MAX_EVENTS];
	char m_aData[MAX_DATASIZE];

	class CGameContext *m_pGameServer;
	class CGameWorld *m_pGameWorld;

	int m_CurrentOffset;
	int m_NumEvents;

public:
	CGameContext *GameServer() const { return m_pGameServer; }
	CGameWorld *GameWorld() const { return m_pGameWorld; }
	void SetGameServer(CGameContext *pGameServer);
	void SetGameWorld(CGameWorld *pGameWorld);

	CEventHandler();
	void *Create(int Type, int Size, CEventMask Mask);

	template<typename T>
	T *Create(CEventMask Mask)
	{
		return static_cast<T *>(Create(T::ms_MsgID, sizeof(T), Mask));
	}

	void Clear();
	void Snap(int SnappingClient);

	bool Translate(int SnappingClient, int *pType, const char **ppData);

	void EventToSixup(int *pType, int *pSize, const char **ppData);
};

#endif
