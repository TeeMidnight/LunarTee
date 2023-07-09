/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_SERVER_H
#define ENGINE_SERVER_SERVER_H

#include <base/hash.h>

#include <engine/map.h>
#include <engine/server.h>

#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/map.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/protocol.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>

#include <list>
#include <memory>
#include <vector>


class CSnapIDPool
{
	enum
	{
		MAX_IDS = 16*1024,
	};

	class CID
	{
	public:
		short m_Next;
		short m_State; // 0 = free, 1 = alloced, 2 = timed
		int m_Timeout;
	};

	CID m_aIDs[MAX_IDS];

	int m_FirstFree;
	int m_FirstTimed;
	int m_LastTimed;
	int m_Usage;
	int m_InUsage;

public:

	CSnapIDPool();

	void Reset();
	void RemoveFirstTimeout();
	int NewID();
	void TimeoutIDs();
	void FreeID(int ID);
};


class CServerBan : public CNetBan
{
	class CServer *m_pServer;

	template<class T> int BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason);

public:
	class CServer *Server() const { return m_pServer; }

	void InitServerBan(class IConsole *pConsole, class IStorage *pStorage, class CServer* pServer);

	int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason) override;
	int BanRange(const CNetRange *pRange, int Seconds, const char *pReason) override;

	static void ConBanExt(class IConsole::IResult *pResult, void *pUser);
};


class CServer : public IServer
{
	class IGameServer *m_pGameServer;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	class IRegister *m_pRegister;

public:
	class IGameServer *GameServer() { return m_pGameServer; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }

	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,

		MAX_RCONCMD_SEND=16,
	};

	class CClient
	{
	public:

		enum
		{
			STATE_EMPTY = 0,
			STATE_PREAUTH,
			STATE_AUTH,
			STATE_CONNECTING,
			STATE_READY,
			STATE_INGAME,

			SNAPRATE_INIT=0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER
		};

		class CInput
		{
		public:
			int m_aData[MAX_INPUT_SIZE];
			int m_GameTick; // the tick that was chosen for the input
		};

		// connection state info
		int m_State;
		int m_Latency;
		int m_SnapRate;

		int m_LastAckedSnapshot;
		int m_LastInputTick;
		CSnapshotStorage m_Snapshots;


		CInput m_LatestInput;
		CInput m_aInputs[200]; // TODO: handle input better
		int m_CurrentInput;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Score;
		int m_Authed;
		int m_AuthTries;
		int m_NextMapChunk;

		const IConsole::CCommandInfo *m_pRconCmdToSend;
		
		void Reset();

		char m_aLanguage[16];
		NETADDR m_Addr;
		bool m_GotDDNetVersionPacket;
		bool m_DDNetVersionSettled;
		int m_DDNetVersion;
		char m_aDDNetVersionStr[64];
		CUuid m_ConnectionID;

		CMapData *m_pMapData;
	};

	CClient m_aClients[MAX_CLIENTS];

	CIDMap m_aIDMap[MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIDPool m_IDPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
	CServerBan m_ServerBan;

	int64_t m_GameStartTime;
	int m_RunServer;
	int m_MapReload;
	bool m_ReloadedWhenEmpty;
	int m_RconClientID;
	int m_RconAuthLevel;
	int m_PrintCBIndex;

	bool m_ServerInfoHighLoad;
	int64_t m_ServerInfoFirstRequest;
	int m_ServerInfoNumRequests;

	CDemoRecorder m_DemoRecorder;

	std::vector<CMapData> m_vMapDatas;

	bool m_Active;

	CServer();
	~CServer();

	int TrySetClientName(int ClientID, const char *pName);

	void SetClientName(int ClientID, const char *pName) override;
	void SetClientClan(int ClientID, char const *pClan) override;
	void SetClientCountry(int ClientID, int Country) override;
	void SetClientScore(int ClientID, int Score) override;

	void Kick(int ClientID, const char *pReason);

	void DemoRecorder_HandleAutoStart();
	bool DemoRecorder_IsRecording();

	int64_t TickStartTime(int Tick);

	int Init();
	
	void SendLogLine(const CLogMessage *pMessage);
	// Accepts -1 as ClientID to mean "all clients with at least auth level admin"
	void SendRconLogLine(int ClientID, const CLogMessage *pMessage);
	void SetRconCID(int ClientID);
	bool IsAuthed(int ClientID);
	bool GetClientInfo(int ClientID, CClientInfo *pInfo) const override;
	void SetClientDDNetVersion(int ClientID, int DDNetVersion) override;
	void GetClientAddr(int ClientID, char *pAddrStr, int Size);
	const char *ClientName(int ClientID);
	const char *ClientClan(int ClientID);
	int ClientCountry(int ClientID);
	bool ClientIngame(int ClientID);
	int MaxClients() const;
	int GetClientVersion(int ClientID) const override;
	bool Is64Player(int ClientID) const override;
	IMap *GetClientMap(int ClientID) override;

	int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) override;

	void DoSnapshot();
	
	static int ClientRejoinCallback(int ClientID, void *pUser);
	static int NewClientCallback(int ClientID, void *pUser, bool Sixup);
	static int NewClientNoAuthCallback(int ClientID, void *pUser);
	static int DelClientCallback(int ClientID, const char *pReason, void *pUser);

	void SendRconType(int ClientID, bool UsernameReq);
	void SendCapabilities(int ClientID);
	void SendMap(int ClientID);
	void SendMapData(int ClientID, int Chunk);
	void SendConnectionReady(int ClientID);
	void SendRconLine(int ClientID, const char *pLine);
	static void SendRconLineAuthed(const char *pLine, void *pUser);

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

class CCache
	{
	public:
		class CCacheChunk
		{
		public:
			CCacheChunk(const void *pData, int Size);
			CCacheChunk(const CCacheChunk &) = delete;
			CCacheChunk(CCacheChunk &&) = default;

			std::vector<uint8_t> m_vData;
		};

		std::vector<CCacheChunk> m_vCache;

		CCache();
		~CCache();

		void AddChunk(const void *pData, int Size);
		void Clear();
	};
	CCache m_aServerInfoCache[3 * 2];
	CCache m_aSixupServerInfoCache[2];
	bool m_ServerInfoNeedsUpdate;

	void ExpireServerInfo() override;
	void CacheServerInfo(CCache *pCache, int Type, bool SendClients);
	void SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients);
	bool RateLimitServerInfoConnless();
	void SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type);
	void UpdateRegisterServerInfo();
	void UpdateServerInfo(bool Resend = false);

	void PumpNetwork(bool PacketWaiting);

	char *GetMapName(CMapData *pMapData);

	int LoadMap(const char *pMapName);
	int GenerateMap(const char *pMapName);
	void CreateMapThread(const char *pMapName);

	bool m_MainMapLoaded;

	int Run();

	static void ConKick(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown(IConsole::IResult *pResult, void *pUser);
	static void ConRecord(IConsole::IResult *pResult, void *pUser);
	static void ConStopRecord(IConsole::IResult *pResult, void *pUser);
	static void ConLogout(IConsole::IResult *pResult, void *pUser);
	static void ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	
	static void ConNewMap(IConsole::IResult *pResult, void *pUser);

	void RegisterCommands();

	int SnapNewID() override;
	void SnapFreeID(int ID) override;
	void *SnapNewItem(int Type, int ID, int Size) override;
	void SnapSetStaticsize(int ItemType, int Size);
	
public:
	const char *GetClientLanguage(int ClientID) override;
	void SetClientLanguage(int ClientID, const char *pLanguage) override;
	int* GetIdMap(int ClientID) override;
	void ClearIdMap(int ClientID) override;
	void RegenerateMap() override;
	bool IsActive() override;

	void ChangeClientMap(int ClientID, int MapID) override;
	int GetLoadedMapNum() const override;

	int GetOneWorldPlayerNum(int ClientID) const override;
	void CreateNewTheardJob(std::shared_ptr<IJob> pJob) override;
};

#endif