/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H
#include "kernel.h"
#include "message.h"
#include <base/math.h>

#include <generated/protocol.h>
#include <engine/shared/protocol.h>
#include <engine/shared/jobs.h>

class IMap;

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	class CLocalization* m_pLocalization;
	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,
	};
public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
		int m_Authed;
		bool m_GotDDNetVersion;
		int m_DDNetVersion;
		const char *m_pDDNetVersionStr;
		const CUuid *m_pConnectionID;
	};

	inline class CLocalization* Localization() { return m_pLocalization; }

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int MaxClients() const = 0;
	virtual const char *ClientName(int ClientID) = 0;
	virtual const char *ClientClan(int ClientID) = 0;
	virtual int ClientCountry(int ClientID) = 0;
	virtual bool ClientIngame(int ClientID) = 0;
	virtual bool GetClientInfo(int ClientID, CClientInfo *pInfo) const = 0;
	virtual void SetClientDDNetVersion(int ClientID, int DDNetVersion) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) = 0;
	virtual int GetClientVersion(int ClientID) const = 0;
	virtual bool Is64Player(int ClientID) const = 0;
	virtual class IMap *GetClientMap(int ClientID) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		if(ClientID == -1)
		{
			for(int i = 0; i < MaxClients(); i++)
				if(ClientIngame(i))
					Result = SendPackMsgTranslate(pMsg, Flags, i);
		}
		else
		{
			Result = SendPackMsgTranslate(pMsg, Flags, ClientID);
		}
		return Result;
	}

	template<class T>
	int SendPackMsgTranslate(T *pMsg, int Flags, int ClientID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_Emoticon *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_Emoticon MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		return Translate(MsgCopy.m_ClientID, ClientID) && SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	char msgbuf[1000];

	int SendPackMsgTranslate(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_Chat MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));

		char aBuf[1000];
		if(MsgCopy.m_ClientID >= 0 && !Translate(MsgCopy.m_ClientID, ClientID))
		{
			str_format(aBuf, sizeof(aBuf), "%s: %s", ClientName(MsgCopy.m_ClientID), MsgCopy.m_pMessage);
			MsgCopy.m_pMessage = aBuf;
			MsgCopy.m_ClientID = (Is64Player(ClientID) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS) - 1;
		}
		return SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_KillMsg *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_KillMsg MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		if(!Translate(MsgCopy.m_Victim, ClientID))
			return 0;
		if(!Translate(MsgCopy.m_Killer, ClientID))
			MsgCopy.m_Killer = MsgCopy.m_Victim;
		return SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	template<class T>
	int SendPackMsgOne(T *pMsg, int Flags, int ClientID)
	{
		dbg_assert(ClientID != -1, "SendPackMsgOne called with -1");
		CMsgPacker Packer(T::ms_MsgID, false);

		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	bool Translate(int &Target, int Client)
	{
		int *pMap = GetIdMap(Client);
		bool Found = false;
		for(int i = 0; i < (Is64Player(Client) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS); i++)
		{
			if(Target == pMap[i])
			{
				Target = i;
				Found = true;
				break;
			}
		}
		return Found;
	}

	bool ReverseTranslate(int &Target, int Client)
	{
		Target = clamp(Target, 0, (Is64Player(Client) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS) - 1);
		int *pMap = GetIdMap(Client);
		if(pMap[Target] == -1)
			return false;
		Target = pMap[Target];
		return true;
	}

	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;
	virtual void SetClientScore(int ClientID, int Score) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV=-1,
		RCON_CID_VOTE=-2,
	};
	virtual void SetRconCID(int ClientID) = 0;
	virtual bool IsAuthed(int ClientID) = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual bool DemoRecorder_IsRecording() = 0;

	virtual const char* GetClientLanguage(int ClientID) = 0;
	virtual void SetClientLanguage(int ClientID, const char* pLanguage) = 0;
	virtual int* GetIdMap(int ClientID) = 0;
	
	virtual void ExpireServerInfo() = 0;
	virtual void RegenerateMap() = 0;

	virtual bool IsActive() = 0;

	virtual void ChangeClientMap(int ClientID, int MapID) = 0;
	virtual int GetLoadedMapNum() const = 0;

	virtual int GetOneWorldPlayerNum(int ClientID) const = 0;

	virtual void CreateNewTheardJob(std::shared_ptr<IJob> pJob) = 0;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;

	virtual void OnClientConnected(int ClientID) = 0;
	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientReady(int ClientID) = 0;
	virtual bool IsClientPlayer(int ClientID) = 0;

	virtual int GetOneWorldPlayerNum(int ClientID) const = 0;

	virtual const char *GameType() = 0;
	virtual const char *Version() = 0;
	virtual const char *NetVersion() = 0;

	virtual void OnSetAuthed(int ClientID, int Level) = 0;
	
	virtual void OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int ID) = 0;
};

extern IGameServer *CreateGameServer();
#endif