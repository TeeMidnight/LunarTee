/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>

#include <base/lock_scope.h>
#include <base/logger.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/external/json/json.hpp>

#include <engine/shared/assertion_logger.h>
#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/http.h>
#include <engine/shared/masterserver.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <generated/protocol7.h>
#include <generated/protocolglue.h>

#include <game/version.h>

#include <lunartee/mapgen/mapgen.h>

#include <lunartee/localization/localization.h>

#include "register.h"
#include "server.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

#include <signal.h>

#if defined(CONF_FAMILY_WINDOWS)
	#define _WIN32_WINNT 0x0501
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#elif defined(CONF_FAMILY_UNIX)
    #include <cstring>//fixes memset error
#endif

volatile sig_atomic_t InterruptSignaled = 0;

static const char *StrLtrim(const char *pStr)
{
	while(*pStr && *pStr >= 0 && *pStr <= 32)
		pStr++;
	return pStr;
}

static void StrRtrim(char *pStr)
{
	int i = str_length(pStr);
	while(i >= 0)
	{
		if(pStr[i] < 0 || pStr[i] > 32)
			break;
		pStr[i] = 0;
		i--;
	}
}


CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for(int i = 0; i < MAX_IDS; i++)
	{
		m_aIDs[i].m_Next = i+1;
		m_aIDs[i].m_State = 0;
	}

	m_aIDs[MAX_IDS-1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}


void CSnapIDPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIDs[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIDs[m_FirstTimed].m_Next = m_FirstFree;
	m_aIDs[m_FirstTimed].m_State = 0;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64_t Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	dbg_assert(ID != -1, "id error");
	if(ID == -1)
		return ID;
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = 1;
	m_Usage++;
	m_InUsage++;
	return ID;
}

void CSnapIDPool::TimeoutIDs()
{
	// process timed ids
	while(m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIDPool::FreeID(int ID)
{
	if(ID < 0)
		return;
	dbg_assert(m_aIDs[ID].m_State == 1, "id is not alloced");

	m_InUsage--;
	m_aIDs[ID].m_State = 2;
	m_aIDs[ID].m_Timeout = time_get()+time_freq()*5;
	m_aIDs[ID].m_Next = -1;

	if(m_LastTimed != -1)
	{
		m_aIDs[m_LastTimed].m_Next = ID;
		m_LastTimed = ID;
	}
	else
	{
		m_FirstTimed = ID;
		m_LastTimed = ID;
	}
}


void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer* pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s?ir", CFGFLAG_SERVER|CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_aClients.count(Server()->m_RconClientID))
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(auto& Client : Server()->m_aClients)
		{
			if(Client.first == Server()->m_RconClientID)
				continue;

			if(Client.second.m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(Client.first)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for(auto& Client : Server()->m_aClients)
		{
			if(Client.second.m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(Client.first)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for(auto& Client : Server()->m_aClients)
	{
		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(Client.first)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(Client.first, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";

	if(StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(!pThis->Server()->m_aClients.count(ClientID))
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes*60, pReason);
	}
	else
		ConBan(pResult, pUser);
}


CServer::CClient::CClient()
{
	m_aName[0] = 0;
	m_aClan[0] = 0;
	m_Country = -1;
	m_Snapshots.Init();
	m_Sixup = false;
	m_InMenu = false;
	str_copy(m_aLanguage, g_Config.m_SvDefaultLanguage);
}

void CServer::CClient::Reset()
{
	// reset input
	for(int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_Score = 0;
	m_NextMapChunk = 0;
}

const char *CServer::GetClientLanguage(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return "en";
	return m_aClients[ClientID].m_aLanguage;
}

void CServer::SetClientLanguage(int ClientID, const char *pLanguage)
{
	if(!m_aClients.count(ClientID))
		return;
	str_copy(m_aClients[ClientID].m_aLanguage, pLanguage, sizeof(m_aClients[ClientID].m_aLanguage));
}

CServer::CServer() : m_DemoRecorder(&m_SnapshotDelta)
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = 0;
	m_RunServer = 1;

	m_MapReload = 0;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoHighLoad = false;
	m_ServerInfoNeedsUpdate = false;

	m_pRegister = nullptr;

	m_Active = false;

	m_pMainMapData = nullptr;
	m_pMenuMapData = nullptr;

	Init();
}

CServer::~CServer()
{
	delete m_pRegister;
}

int CServer::TrySetClientName(int ClientID, const char *pName)
{
	char aTrimmedName[64];

	// trim the name
	str_copy(aTrimmedName, StrLtrim(pName), sizeof(aTrimmedName));
	StrRtrim(aTrimmedName);

	// check for empty names
	if(!aTrimmedName[0])
		return -1;

	// check if new and old name are the same
	if(m_aClients[ClientID].m_aName[0] && str_comp(m_aClients[ClientID].m_aName, aTrimmedName) == 0)
		return 0;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "'%s' -> '%s'", pName, aTrimmedName);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
	pName = aTrimmedName;

	// make sure that two clients doesn't have the same name
	for(auto& Client : m_aClients)
		if(Client.first != ClientID && Client.second.m_State >= CClient::STATE_READY)
		{
			if(str_comp(pName, Client.second.m_aName) == 0)
				return -1;
		}

	// set the client name
	str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
	return 0;
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	if(!m_aClients.count(ClientID) || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	if(!pName)
		return;

	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pName, sizeof(aCleanName));

	if(TrySetClientName(ClientID, aCleanName))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			char aNameTry[MAX_NAME_LENGTH];
			str_format(aNameTry, sizeof(aCleanName), "(%d)%s", i, aCleanName);
			if(TrySetClientName(ClientID, aNameTry) == 0)
				break;
		}
	}
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(!m_aClients.count(ClientID) || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].m_aClan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(!m_aClients.count(ClientID) || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::SetClientScore(int ClientID, int Score)
{
	if(!m_aClients.count(ClientID) || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_Score = Score;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(!m_aClients.count(ClientID))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
 		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
 		return;
	}

	m_NetServer.Drop(ClientID, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64_t CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq()*Tick)/SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	m_CurrentGameTick = 0;

	return 0;
}

void CServer::SendLogLine(const CLogMessage *pMessage)
{
	if(pMessage->m_Level <= IConsole::ToLogLevel(g_Config.m_ConsoleOutputLevel))
	{
		SendRconLogLine(-1, pMessage);
	}
	if(pMessage->m_Level <= IConsole::ToLogLevel(g_Config.m_EcOutputLevel))
	{
		m_Econ.Send(-1, pMessage->m_aLine);
	}
}

void CServer::SendRconLogLine(int ClientID, const CLogMessage *pMessage)
{
	const char *pLine = pMessage->m_aLine;
	const char *pStart = str_find(pLine, "<{");
	const char *pEnd = pStart == NULL ? NULL : str_find(pStart + 2, "}>");
	const char *pLineWithoutIps;
	char aLine[512];
	char aLineWithoutIps[512];
	aLine[0] = '\0';
	aLineWithoutIps[0] = '\0';

	if(pStart == NULL || pEnd == NULL)
	{
		pLineWithoutIps = pLine;
	}
	else
	{
		str_append(aLine, pLine, pStart - pLine + 1);
		str_append(aLine, pStart + 2, pStart - pLine + pEnd - pStart - 1);
		str_append(aLine, pEnd + 2, sizeof(aLine));

		str_append(aLineWithoutIps, pLine, pStart - pLine + 1);
		str_append(aLineWithoutIps, "XXX", sizeof(aLineWithoutIps));
		str_append(aLineWithoutIps, pEnd + 2, sizeof(aLineWithoutIps));

		pLine = aLine;
		pLineWithoutIps = aLineWithoutIps;
	}

	if(ClientID == -1)
	{
		for(auto& Client : m_aClients)
		{
			if(Client.second.m_Authed >= AUTHED_ADMIN)
				SendRconLine(Client.first, pLineWithoutIps);
		}
	}
	else
	{
		if(m_aClients.count(ClientID))
			SendRconLine(ClientID, pLineWithoutIps);
	}
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

bool CServer::IsAuthed(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return false;
	return m_aClients[ClientID].m_Authed;
}

bool CServer::GetClientInfo(int ClientID, CClientInfo *pInfo)
{
	dbg_assert(m_aClients.count(ClientID), "ClientID is not valid");
	dbg_assert(pInfo != nullptr, "pInfo cannot be null");

	if(m_aClients.count(ClientID) && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_Authed = m_aClients[ClientID].m_Authed;
		pInfo->m_pName = m_aClients[ClientID].m_aName;
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		pInfo->m_GotDDNetVersion = m_aClients[ClientID].m_DDNetVersionSettled;
		pInfo->m_DDNetVersion = m_aClients[ClientID].m_DDNetVersion >= 0 ? m_aClients[ClientID].m_DDNetVersion : VERSION_VANILLA;
		if(m_aClients[ClientID].m_GotDDNetVersionPacket)
		{
			pInfo->m_pConnectionID = &m_aClients[ClientID].m_ConnectionID;
			pInfo->m_pDDNetVersionStr = m_aClients[ClientID].m_aDDNetVersionStr;
		}
		else
		{
			pInfo->m_pConnectionID = nullptr;
			pInfo->m_pDDNetVersionStr = nullptr;
		}
		return true;
	}
	return false;
}

void CServer::SetClientDDNetVersion(int ClientID, int DDNetVersion)
{
	dbg_assert(m_aClients.count(ClientID), "ClientID is not valid");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
		m_aClients[ClientID].m_DDNetVersionSettled = true;
	}
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size)
{
	if(m_aClients.count(ClientID) && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}


const char *CServer::ClientName(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return "(invalid)";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aName;
	else
		return "(connecting)";

}

const char *CServer::ClientClan(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID)
{
	return m_aClients.count(ClientID) && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

int CServer::EndClientID() const
{
	return (*m_aClients.end()).first;
}

static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer, bool Sixup)
{
	int MsgId = pMsg->m_MsgID;
	Packer.Reset();

	if(Sixup && !pMsg->m_NoTranslate)
	{
		if(pMsg->m_System)
		{
			if(MsgId >= OFFSET_UUID)
				;
			else if(MsgId >= NETMSG_MAP_CHANGE && MsgId <= NETMSG_MAP_DATA)
				;
			else if(MsgId >= NETMSG_CON_READY && MsgId <= NETMSG_INPUTTIMING)
				MsgId += 1;
			else if(MsgId == NETMSG_RCON_LINE)
				MsgId = 13;
			else if(MsgId >= NETMSG_AUTH_CHALLENGE && MsgId <= NETMSG_AUTH_RESULT)
				MsgId += 4;
			else if(MsgId >= NETMSG_PING && MsgId <= NETMSG_ERROR)
				MsgId += 4;
			else if(MsgId >= NETMSG_RCON_CMD_ADD && MsgId <= NETMSG_RCON_CMD_REM)
				MsgId -= 11;
			else
			{
				dbg_msg("net", "DROP send sys %d", MsgId);
				return true;
			}
		}
		else
		{
			if(MsgId >= 0 && MsgId < OFFSET_UUID)
				MsgId = Msg_SixToSeven(MsgId);

			if(MsgId < 0)
				return true;
		}
	}

	if(MsgId < OFFSET_UUID)
	{
		Packer.AddInt((MsgId << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt((0 << 1) | (pMsg->m_System ? 1 : 0)); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(MsgId, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(ClientID < 0)
	{
		CPacker Pack6, Pack7;
		if(RepackMsg(pMsg, Pack6, false))
			return -1;
		if(RepackMsg(pMsg, Pack7, true))
			return -1;

		// write message to demo recorders
		if(!(Flags & MSGFLAG_NORECORD))
		{
			m_DemoRecorder.RecordMessage(Pack6.Data(), Pack6.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
		{
			for(auto& Client : m_aClients)
			{
				if(Client.second.m_State == CClient::STATE_INGAME)
				{
					CPacker *pPack = Client.second.m_Sixup ? &Pack7 : &Pack6;
					Packet.m_pData = pPack->Data();
					Packet.m_DataSize = pPack->Size();
					Packet.m_ClientID = Client.first;
					m_NetServer.Send(&Packet);
				}
			}
		}
	}
	else
	{
		CPacker Pack;
		if(RepackMsg(pMsg, Pack, m_aClients[ClientID].m_Sixup))
			return -1;

		Packet.m_ClientID = ClientID;
		Packet.m_pData = Pack.Data();
		Packet.m_DataSize = Pack.Size();

		// write message to demo recorders
		if(!(Flags & MSGFLAG_NORECORD))
		{
			m_DemoRecorder.RecordMessage(Pack.Data(), Pack.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND) && m_aClients.count(ClientID))
			m_NetServer.Send(&Packet);
	}

	return 0;
}

void CServer::DoSnapshot()
{
	GameServer()->OnPreSnap();

	// create snapshot for demo recording
	if(m_DemoRecorder.IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];
		int SnapshotSize;

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_DemoRecorder.RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all players
	for(auto& Client : m_aClients)
	{
		// client must be ingame to recive snapshots
		if(Client.second.m_State != CClient::STATE_INGAME)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(Client.second.m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick() % 50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(Client.second.m_SnapRate == CClient::SNAPRATE_INIT && (Tick() % 10) != 0)
			continue;

		{
			m_SnapshotBuilder.Init(Client.second.m_Sixup);

			GameServer()->OnSnap(Client.first);

			// finish snapshot
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot *)aData; // Fix compiler warning for strict-aliasing
			int SnapshotSize = m_SnapshotBuilder.Finish(pData);

			int Crc = pData->Crc();

			// remove old snapshots
			// keep 3 seconds worth of snapshots
			Client.second.m_Snapshots.PurgeUntil(m_CurrentGameTick - TickSpeed() * 3);

			// save the snapshot
			Client.second.m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0, nullptr);

			// find snapshot that we can perform delta against
			int DeltaTick = -1;
			const CSnapshot *pDeltashot = CSnapshot::EmptySnapshot();
			{
				int DeltashotSize = Client.second.m_Snapshots.Get(Client.second.m_LastAckedSnapshot, nullptr, &pDeltashot, nullptr);
				if(DeltashotSize >= 0)
					DeltaTick = Client.second.m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(Client.second.m_SnapRate == CClient::SNAPRATE_FULL)
						Client.second.m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_SOUNDWORLD, Client.second.m_Sixup);
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_DAMAGE, Client.second.m_Sixup);
			char aDeltaData[CSnapshot::MAX_SIZE];
			int DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;

				char aCompData[CSnapshot::MAX_SIZE];
				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				int NumPackets = (SnapshotSize + MaxSize - 1) / MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, Client.first);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, Client.first);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick - DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, Client.first);
			}
		}
	}

	GameServer()->OnPostSnap();
}

int CServer::ClientRejoinCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID] = CClient();

	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;

	pThis->m_aClients[ClientID].Reset();

	pThis->SendCapabilities(ClientID);
	pThis->SendMap(ClientID);

	return 0;
}

int CServer::NewClientNoAuthCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID] = CClient();

	pThis->m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_pMapData = pThis->m_pMenuMapData ? pThis->m_pMenuMapData : pThis->m_pMainMapData;
	pThis->m_aClients[ClientID].m_InMenu = pThis->m_pMenuMapData;

	memset(&pThis->m_aClients[ClientID].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientID].Reset();

	pThis->SendCapabilities(ClientID);
	pThis->SendMap(ClientID);

	return 0;
}

int CServer::NewClientCallback(int ClientID, void *pUser, bool Sixup)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID] = CClient();

	pThis->m_aClients[ClientID].m_State = CClient::STATE_PREAUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_pMapData = pThis->m_pMenuMapData ? pThis->m_pMenuMapData : pThis->m_pMainMapData;

	pThis->m_aClients[ClientID].Reset();

	pThis->m_aClients[ClientID].m_Sixup = Sixup;
	pThis->m_aClients[ClientID].m_InMenu = pThis->m_pMenuMapData;

	return 0;
}

int CServer::DelClientCallback(int ClientID, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=%s reason='%s'", ClientID, aAddrStr,	pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if(pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY)
		pThis->GameServer()->OnClientDrop(ClientID, pReason);

	if(pThis->m_RunServer)
		pThis->m_aClients.erase(ClientID);

	return 0;
}

void CServer::SendRconType(int ClientID, bool UsernameReq)
{
	CMsgPacker Msg(NETMSG_RCONTYPE, true);
	Msg.AddInt(UsernameReq);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendCapabilities(int ClientID)
{
	CMsgPacker Msg(NETMSG_CAPABILITIES, true);
	Msg.AddInt(SERVERCAP_CURVERSION); // version
	Msg.AddInt(SERVERCAPFLAG_CHATTIMEOUTCODE); // flags
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendMap(int ClientID)
{
	CMapData *pMapData = m_aClients[ClientID].m_pMapData;

	char aBuf[256];
	const char *MapName = GetMapName(pMapData);
	str_format(aBuf, sizeof(aBuf), "Sending ClientID %d Map '%s'", ClientID, MapName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Multimap", aBuf);

	{
		CMsgPacker Msg(NETMSG_MAP_DETAILS, true);
		Msg.AddString(GetMapName(pMapData), 0);
		Msg.AddRaw(&pMapData->m_MapSha256.data, sizeof(pMapData->m_MapSha256.data));
		Msg.AddInt(pMapData->m_MapCrc);
		Msg.AddInt(pMapData->m_MapSize);
		Msg.AddString("", 0); // HTTPS map download URL
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
	{
		CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
		Msg.AddString(GetMapName(pMapData), 0);
		Msg.AddInt(pMapData->m_MapCrc);
		Msg.AddInt(pMapData->m_MapSize);
		if(IsSixup(ClientID))
		{
			Msg.AddInt(g_Config.m_SvMapWindow);
			Msg.AddInt(1024 - 128);
			Msg.AddRaw(&pMapData->m_MapSha256.data, sizeof(pMapData->m_MapSha256.data));
		}
		SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
	}

	m_aClients[ClientID].m_NextMapChunk = 0;
}

void CServer::SendMapData(int ClientID, int Chunk)
{
	CMapData *pMapData = m_aClients[ClientID].m_pMapData;

	unsigned int ChunkSize = 1024 - 128;
	unsigned int Offset = Chunk * ChunkSize;
	int Last = 0;

	// drop faulty map data requests
	if(Chunk < 0 || Offset > pMapData->m_MapSize)
		return;

	if(Offset + ChunkSize >= pMapData->m_MapSize)
	{
		ChunkSize = pMapData->m_MapSize - Offset;
		Last = 1;
	}

	CMsgPacker Msg(NETMSG_MAP_DATA, true);
	if(!IsSixup(ClientID))
	{
		Msg.AddInt(Last);
		Msg.AddInt(pMapData->m_MapCrc);
		Msg.AddInt(Chunk);
		Msg.AddInt(ChunkSize);
	}
	Msg.AddRaw(&pMapData->m_pMapData[Offset], ChunkSize);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);

	if(g_Config.m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
	}
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	static volatile int ReentryGuard = 0;

	if(ReentryGuard) return;
	ReentryGuard++;

	for(auto& Client : pThis->m_aClients)
	{
		if(Client.second.m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(Client.first, pLine);
	}

	ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::UpdateClientRconCommands()
{
	for(auto& Client : m_aClients)
	{
		if(!Client.second.m_Authed)
			continue;

		int ConsoleAccessLevel = Client.second.m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
		for(int i = 0; i < MAX_RCONCMD_SEND && Client.second.m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(Client.second.m_pRconCmdToSend, Client.first);
			Client.second.m_pRconCmdToSend = Client.second.m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
			if(Client.second.m_pRconCmdToSend == nullptr)
			{
				// ddnet msg
				CMsgPacker Msg(NETMSG_RCON_CMD_GROUP_END, true);
				SendMsg(&Msg, MSGFLAG_VITAL, Client.first);
			}
		}
	}
}

static inline int MsgFromSixup(int Msg, bool System)
{
	if(System)
	{
		if(Msg == NETMSG_INFO)
			;
		else if(Msg >= 14 && Msg <= 15)
			Msg += 11;
		else if(Msg >= 18 && Msg <= 28)
			Msg = NETMSG_READY + Msg - 18;
		else if(Msg < OFFSET_UUID)
			return -1;
	}

	return Msg;
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if(IsSixup(ClientID) && (Msg = MsgFromSixup(Msg, Sys)) < 0)
	{
		return;
	}

	if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL, ClientID);
	}

	//if(Msg != NETMSG_INPUT && Msg != NETMSG_REQUEST_MAP_DATA)
	//	dbg_msg("debug", "packet %d of client=%d, state=%d, ready=%d", Msg, ClientID, m_aClients[ClientID].m_State, GameServer()->IsClientReady(ClientID));

	if(Sys)
	{
		// system message
		if(Msg == NETMSG_CLIENTVER)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_PREAUTH)
			{
				CUuid *pConnectionID = (CUuid *)Unpacker.GetRaw(sizeof(*pConnectionID));
				int DDNetVersion = Unpacker.GetInt();
				const char *pDDNetVersionStr = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() || !str_utf8_check(pDDNetVersionStr) || DDNetVersion < 0)
				{
					return;
				}
				m_aClients[ClientID].m_ConnectionID = *pConnectionID;
				m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
				str_copy(m_aClients[ClientID].m_aDDNetVersionStr, pDDNetVersionStr);
				m_aClients[ClientID].m_DDNetVersionSettled = true;
				m_aClients[ClientID].m_GotDDNetVersionPacket = true;
				m_aClients[ClientID].m_State = CClient::STATE_AUTH;
			}
		}
		else if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_AUTH || m_aClients[ClientID].m_State == CClient::STATE_PREAUTH))
			{
				if(g_Config.m_SvMaxClients > 0 && m_aClients.size() > (unsigned long) g_Config.m_SvMaxClients)
				{
					// full
					m_NetServer.Drop(ClientID, "This server is full");
					return;
				}

				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if((str_comp(pVersion, "0.6 626fce9a778df4d4") != 0) && (str_comp(pVersion, "0.7 802f1be60a05665f") != 0))
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", IsSixup(ClientID) ? "0.7 802f1be60a05665f" : "0.6 626fce9a778df4d4", pVersion);
					m_NetServer.Drop(ClientID, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(g_Config.m_Password[0] != 0 && str_comp(g_Config.m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendCapabilities(ClientID);
				SendMap(ClientID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			if(IsSixup(ClientID))
			{
				for(int i = 0; i < g_Config.m_SvMapWindow; i++)
				{
					SendMapData(ClientID, m_aClients[ClientID].m_NextMapChunk++);
				}
				return;
			}

			int Chunk = Unpacker.GetInt();
			if(Unpacker.Error())
			{
				return;
			}
			if(Chunk != m_aClients[ClientID].m_NextMapChunk || !g_Config.m_SvFastDownload)
			{
				SendMapData(ClientID, Chunk);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < g_Config.m_SvMapWindow; i++)
				{
					SendMapData(ClientID, i);
				}
			}
			SendMapData(ClientID, g_Config.m_SvMapWindow + m_aClients[ClientID].m_NextMapChunk);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_CONNECTING))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=%s secure=%s", ClientID, aAddrStr, m_NetServer.HasSecurityToken(ClientID)?"yes":"no");
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

				m_aClients[ClientID].m_State = CClient::STATE_READY;
				GameServer()->OnClientConnected(ClientID, m_aClients[ClientID].m_pMapData->m_aMap, m_aClients[ClientID].m_InMenu);
			}
			SendConnectionReady(ClientID);
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_READY) && GameServer()->IsClientReady(ClientID))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%d addr=%s", ClientID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				if(!IsSixup(ClientID))
				{
					SendServerInfo(m_NetServer.ClientAddr(ClientID), -1, SERVERINFO_EXTENDED, false);
				}
				else
				{
					CMsgPacker Msgp(protocol7::NETMSG_SERVERINFO, true, true);
					GetServerInfoSixup(&Msgp, -1, false);
					SendMsg(&Msgp, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
				}
				GameServer()->OnClientEnter(ClientID);
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64_t TagTime;

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size/4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get()-TagTime)*1000)/time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick)-time_get())*1000) / time_freq();

				CMsgPacker Msg(NETMSG_INPUTTIMING, true);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsg(&Msg, 0, ClientID);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick()+1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size/4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE*sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServer()->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
		}
		else if(Msg == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();

			if(Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				int Version = m_aClients[ClientID].m_DDNetVersion;
				if(Version < VERSION_DDNET_OLD)
				{
					m_aClients[ClientID].m_DDNetVersion = VERSION_DDNET_OLD;
				}
			}
			else if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				switch(m_aClients[ClientID].m_Authed)
				{
					case AUTHED_ADMIN:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
						break;
					case AUTHED_MOD:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
						break;
					default:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
				}	
				Console()->ExecuteLineFlag(pCmd, ClientID, CFGFLAG_SERVER);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = IServer::RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;
			}
		}
		else if(Msg == NETMSG_RCON_AUTH)
		{
			const char *pPw;
			if(!IsSixup(ClientID))
				Unpacker.GetString(); // login name, not used
			pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);

			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				if(g_Config.m_SvRconPassword[0] == 0 && g_Config.m_SvRconModPassword[0] == 0)
				{
					SendRconLine(ClientID, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
				}
				else if(g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
				{
					if(IsSixup(ClientID))
					{
						CMsgPacker Msg(protocol7::NETMSG_RCON_AUTH_ON, true, true);
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
					}
					else
					{
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
					}

					m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
					GameServer()->OnSetAuthed(ClientID, m_aClients[ClientID].m_Authed);
					int SendRconCmds = Unpacker.GetInt();
					if((Unpacker.Error() == 0 && SendRconCmds) || IsSixup(ClientID))
					{
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
						CMsgPacker MsgStart(NETMSG_RCON_CMD_GROUP_START, true);
						SendMsg(&MsgStart, MSGFLAG_VITAL, ClientID);
						if(m_aClients[ClientID].m_pRconCmdToSend == nullptr)
						{
							CMsgPacker MsgEnd(NETMSG_RCON_CMD_GROUP_END, true);
							SendMsg(&MsgEnd, MSGFLAG_VITAL, ClientID);
						}
					}
					SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (admin)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
				{
					if(IsSixup(ClientID))
					{
						CMsgPacker Msg(protocol7::NETMSG_RCON_AUTH_ON, true, true);
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
					}
					else
					{
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
					}

					m_aClients[ClientID].m_Authed = AUTHED_MOD;
					int SendRconCmds = Unpacker.GetInt();
					if((Unpacker.Error() == 0 && SendRconCmds) || IsSixup(ClientID))
					{
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
						CMsgPacker MsgStart(NETMSG_RCON_CMD_GROUP_START, true);
						SendMsg(&MsgStart, MSGFLAG_VITAL, ClientID);
						if(m_aClients[ClientID].m_pRconCmdToSend == nullptr)
						{
							CMsgPacker MsgEnd(NETMSG_RCON_CMD_GROUP_END, true);
							SendMsg(&MsgEnd, MSGFLAG_VITAL, ClientID);
						}
					}
					SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (moderator)", ClientID);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if(m_aClients[ClientID].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if(!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientID, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), g_Config.m_SvRconBantime*60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY, true);
			SendMsg(&Msg, 0, ClientID);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msgp(NETMSG_PONGEX, true);
			Msgp.AddRaw(pID, sizeof(*pID));
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientID);
		}
		else
		{
			if(g_Config.m_Debug)
			{
				constexpr int MaxDumpedDataSize = 32;
				char aBuf[MaxDumpedDataSize * 3 + 1];
				str_hex(aBuf, sizeof(aBuf), pPacket->m_pData, minimum(pPacket->m_DataSize, MaxDumpedDataSize));

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServer()->OnMessage(Msg, &Unpacker, ClientID);
	}
}

bool CServer::RateLimitServerInfoConnless()
{
	bool SendClients = true;
	if(g_Config.m_SvServerInfoPerSecond)
	{
		SendClients = m_ServerInfoNumRequests <= g_Config.m_SvServerInfoPerSecond;
		const int64_t  Now = Tick();

		if(Now <= m_ServerInfoFirstRequest + TickSpeed())
		{
			m_ServerInfoNumRequests++;
		}
		else
		{
			m_ServerInfoNumRequests = 1;
			m_ServerInfoFirstRequest = Now;
		}
	}

	return SendClients;
}

void CServer::SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type)
{
	SendServerInfo(pAddr, Token, Type, RateLimitServerInfoConnless());
}

static inline int GetCacheIndex(int Type, bool SendClient)
{
	if(Type == SERVERINFO_INGAME)
		Type = SERVERINFO_VANILLA;
	else if(Type == SERVERINFO_EXTENDED_MORE)
		Type = SERVERINFO_EXTENDED;

	return Type * 2 + SendClient;
}

CServer::CCache::CCache()
{
	m_vCache.clear();
}

CServer::CCache::~CCache()
{
	Clear();
}

CServer::CCache::CCacheChunk::CCacheChunk(const void *pData, int Size)
{
	m_vData.assign((const uint8_t *)pData, (const uint8_t *)pData + Size);
}

void CServer::CCache::AddChunk(const void *pData, int Size)
{
	m_vCache.emplace_back(pData, Size);
}

void CServer::CCache::Clear()
{
	m_vCache.clear();
}

void CServer::CacheServerInfoSixup(CCache *pCache, bool SendClients)
{
	pCache->Clear();

	CPacker Packer;
	Packer.Reset();

	// Could be moved to a separate function and cached
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(auto& Client : m_aClients)
	{
		if(GameServer()->IsClientPlayer(Client.first))
			PlayerCount++;

		ClientCount++;

		// only choose 64 players
		if(ClientCount >= DDNET_MAX_CLIENTS)
			break;
	}

	Packer.AddString("0.7↔0.6 Teeworlds", 32);

	char aBuf[64];
	const int MaxClients = minimum(g_Config.m_SvMaxClients == -1 ? 256 : g_Config.m_SvMaxClients, (int) DDNET_MAX_CLIENTS);
	if(g_Config.m_SvMaxClients == -1)
		str_format(aBuf, sizeof(aBuf), "%s [%d/∞]", g_Config.m_SvName, ClientCount);
	else
		str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", g_Config.m_SvName, ClientCount, g_Config.m_SvMaxClients);

	// server name
	Packer.AddString(aBuf, 64);
	Packer.AddString(g_Config.m_SvHostname, 128);
	// map
	Packer.AddString(GameServer()->GameType(), 32);

	// gametype
	Packer.AddString(GameServer()->GameType(), 16);

	// flags
	int Flags = 0;
	if(g_Config.m_Password[0]) // password set
		Flags |= SERVER_FLAG_PASSWORD;
	Packer.AddInt(Flags);

	Packer.AddInt(SERVERINFO_LEVEL_MAX); // server skill level
	Packer.AddInt(minimum(MaxClients - 1, PlayerCount)); // num players
	Packer.AddInt(MaxClients); // max players
	Packer.AddInt(minimum(MaxClients - 1, ClientCount)); // num clients
	Packer.AddInt(MaxClients); // max clients

	if(SendClients)
	{
		int Count = 0;
		for(auto& Client : m_aClients)
		{
			Packer.AddString(ClientName(Client.first), MAX_NAME_LENGTH); // client name
			Packer.AddString(ClientClan(Client.first), MAX_CLAN_LENGTH); // client clan
			Packer.AddInt(Client.second.m_Country); // client country
			Packer.AddInt(Client.second.m_Score); // client score
			Packer.AddInt(GameServer()->IsClientPlayer(Client.first) ? 0 : 1); // flag spectator=1, bot=2 (player=0)

			Count++;
			// only choose 64 players
			if(Count >= DDNET_MAX_CLIENTS)
				break;
		}
	}

	pCache->AddChunk(Packer.Data(), Packer.Size());
}

void CServer::CacheServerInfo(CCache *pCache, int Type, bool SendClients)
{
	pCache->Clear();

	// One chance to improve the protocol!
	CPacker p;
	char aBuf[256];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(auto& Client : m_aClients)
	{
		if(GameServer()->IsClientPlayer(Client.first))
			PlayerCount++;

		ClientCount++;
	}

	p.Reset();

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0); \
	} while(0)

	p.AddString("0.6↔0.7 Teeworlds", 32);

	const int MaxClients = minimum(g_Config.m_SvMaxClients == -1 ? 256 : g_Config.m_SvMaxClients, 256);
	if(g_Config.m_SvMaxClients == -1)
		str_format(aBuf, sizeof(aBuf), "%s [%d/∞]", g_Config.m_SvName, ClientCount);
	else
		str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", g_Config.m_SvName, ClientCount, g_Config.m_SvMaxClients);
	p.AddString(aBuf, Type == SERVERINFO_VANILLA ? 64 : 256);
			
	// map name
	p.AddString(GameServer()->GameType(), 32);

	if(Type == SERVERINFO_EXTENDED)
	{
		ADD_INT(p, m_pMainMapData->m_MapCrc);
		ADD_INT(p, m_pMainMapData->m_MapSize);
	}

	// gametype
	p.AddString(GameServer()->GameType(), 16);

	// flags
	ADD_INT(p, g_Config.m_Password[0] ? SERVER_FLAG_PASSWORD : 0);

	// How many clients the used serverinfo protocol supports, has to be tracked
	// separately to make sure we don't subtract the reserved slots from it
	int MaxClientsProtocol = MaxClients;
	if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
	{
		if(ClientCount >= VANILLA_MAX_CLIENTS)
		{
			if(ClientCount < MaxClients)
				ClientCount = VANILLA_MAX_CLIENTS - 1;
			else
				ClientCount = VANILLA_MAX_CLIENTS;
		}
		MaxClientsProtocol = VANILLA_MAX_CLIENTS;
		if(PlayerCount > ClientCount)
			PlayerCount = ClientCount;
	}

	ADD_INT(p, minimum(MaxClientsProtocol - 1, PlayerCount)); // num players
	ADD_INT(p, minimum(MaxClientsProtocol, MaxClients)); // max players
	ADD_INT(p, minimum(MaxClientsProtocol - 1, ClientCount)); // num clients
	ADD_INT(p, minimum(MaxClientsProtocol, MaxClients)); // max clients

	if(Type == SERVERINFO_EXTENDED)
		p.AddString("", 0); // extra info, reserved

	const void *pPrefix = p.Data();
	int PrefixSize = p.Size();

	CPacker q;
	int ChunksStored = 0;
	int PlayersStored = 0;

#define SAVE(size) \
	do \
	{ \
		pCache->AddChunk(q.Data(), size); \
		ChunksStored++; \
	} while(0)

#define RESET() \
	do \
	{ \
		q.Reset(); \
		q.AddRaw(pPrefix, PrefixSize); \
	} while(0)

	RESET();

	if(Type == SERVERINFO_64_LEGACY)
		q.AddInt(PlayersStored); // offset

	if(!SendClients)
	{
		SAVE(q.Size());
		return;
	}

	if(Type == SERVERINFO_EXTENDED)
	{
		pPrefix = "";
		PrefixSize = 0;
	}

	int Remaining;
	switch(Type)
	{
	case SERVERINFO_EXTENDED: Remaining = SERVERINFO_MAX_CLIENTS; break;
	case SERVERINFO_64_LEGACY: Remaining = 24; break;
	case SERVERINFO_VANILLA: Remaining = VANILLA_MAX_CLIENTS; break;
	case SERVERINFO_INGAME: Remaining = VANILLA_MAX_CLIENTS; break;
	default: dbg_assert(0, "caught earlier, unreachable"); return;
	}

	// Use the following strategy for sending:
	// For vanilla, send the first 16 players.
	// For legacy 64p, send 24 players per packet.
	// For extended, send as much players as possible.
	for(auto& Client : m_aClients)
	{
		if(Remaining == 0)
		{
			if(Type != SERVERINFO_64_LEGACY)
				break;

			// Otherwise we're SERVERINFO_64_LEGACY.
			SAVE(q.Size());
			RESET();
			q.AddInt(PlayersStored); // offset
			Remaining = 24;
		}
		if(Remaining > 0)
		{
			Remaining--;
		}

		int PreviousSize = q.Size();

		q.AddString(ClientName(Client.first), MAX_NAME_LENGTH); // client name
		q.AddString(ClientClan(Client.first), MAX_CLAN_LENGTH); // client clan

		ADD_INT(q, Client.second.m_Country); // client country
		ADD_INT(q, Client.second.m_Score); // client score
		ADD_INT(q, GameServer()->IsClientPlayer(Client.first) ? 1 : 0); // is player?
		if(Type == SERVERINFO_EXTENDED)
			q.AddString("", 0); // extra info, reserved

		if(Type == SERVERINFO_EXTENDED)
		{
			if(q.Size() >= NET_MAX_PAYLOAD - 18) // 8 bytes for type, 10 bytes for the largest token
			{
				// Jump current player.
				SAVE(PreviousSize);
				RESET();
				ADD_INT(q, ChunksStored);
				q.AddString("", 0); // extra info, reserved
				continue;
			}
		}
		PlayersStored++;
	}

	SAVE(q.Size());
#undef SAVE
#undef RESET
#undef ADD_RAW
#undef ADD_INT
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients)
{
	CPacker p;
	char aBuf[128];
	p.Reset();

	CCache *pCache = &m_aServerInfoCache[GetCacheIndex(Type, SendClients)];

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0); \
	} while(0)

	CNetChunk Packet;
	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;

	for(const auto &Chunk : pCache->m_vCache)
	{
		p.Reset();
		if(Type == SERVERINFO_EXTENDED)
		{
			if(&Chunk == &pCache->m_vCache.front())
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED));
			else
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE));
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_64_LEGACY)
		{
			ADD_RAW(p, SERVERBROWSE_INFO_64_LEGACY);
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
		{
			ADD_RAW(p, SERVERBROWSE_INFO);
			ADD_INT(p, Token);
		}
		else
		{
			dbg_assert(false, "unknown serverinfo type");
		}

		p.AddRaw(Chunk.m_vData.data(), Chunk.m_vData.size());
		Packet.m_pData = p.Data();
		Packet.m_DataSize = p.Size();
		m_NetServer.Send(&Packet);
	}
}

void CServer::GetServerInfoSixup(CPacker *pPacker, int Token, bool SendClients)
{
	if(Token != -1)
	{
		pPacker->Reset();
		pPacker->AddRaw(SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO));
		pPacker->AddInt(Token);
	}

	SendClients = SendClients && Token != -1;

	CCache::CCacheChunk &FirstChunk = m_aSixupServerInfoCache[SendClients].m_vCache.front();
	pPacker->AddRaw(FirstChunk.m_vData.data(), FirstChunk.m_vData.size());
}

void CServer::ExpireServerInfo()
{
	m_ServerInfoNeedsUpdate = true;
}

static char EscapeJsonChar(char c)
{
	switch(c)
	{
	case '\"': return '\"';
	case '\\': return '\\';
	case '\b': return 'b';
	case '\n': return 'n';
	case '\r': return 'r';
	case '\t': return 't';
	// Don't escape '\f', who uses that. :)
	default: return 0;
	}
}

static char *EscapeJson(char *pBuffer, int BufferSize, const char *pString)
{
	dbg_assert(BufferSize > 0, "can't null-terminate the string");
	// Subtract the space for null termination early.
	BufferSize--;

	char *pResult = pBuffer;
	while(BufferSize && *pString)
	{
		char c = *pString;
		pString++;
		char Escaped = EscapeJsonChar(c);
		if(Escaped)
		{
			if(BufferSize < 2)
			{
				break;
			}
			*pBuffer++ = '\\';
			*pBuffer++ = Escaped;
			BufferSize -= 2;
		}
		// Assuming ASCII/UTF-8, "if control character".
		else if((unsigned char)c < 0x20)
		{
			// \uXXXX
			if(BufferSize < 6)
			{
				break;
			}
			str_format(pBuffer, BufferSize, "\\u%04x", c);
			pBuffer += 6;
			BufferSize -= 6;
		}
		else
		{
			*pBuffer++ = c;
			BufferSize--;
		}
	}
	*pBuffer = 0;
	return pResult;
}

static const char *JsonBool(bool Bool)
{
	if(Bool)
	{
		return "true";
	}
	else
	{
		return "false";
	}
}

void CServer::UpdateRegisterServerInfo()
{
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(auto& Client : m_aClients)
	{
		if(GameServer()->IsClientPlayer(Client.first))
			PlayerCount++;

		ClientCount++;
	}

	int MaxPlayers = minimum(PlayerCount, 255);
	int MaxClients = minimum(((ClientCount / 64) + 1) * 64, 256);
	char aName[256];
	char aGameType[32];
	char aVersion[64];
	char aMapSha256[SHA256_MAXSTRSIZE];

	if(g_Config.m_SvMaxClients == -1)
		str_format(aName, sizeof(aName), "%s [%d/∞]", g_Config.m_SvName, ClientCount);
	else
		str_format(aName, sizeof(aName), "%s [%d/%d]", g_Config.m_SvName, ClientCount, g_Config.m_SvMaxClients);

	sha256_str(m_pMainMapData->m_MapSha256, aMapSha256, sizeof(aMapSha256));

	char aInfo[16384];
	str_format(aInfo, sizeof(aInfo),
		"{"
		"\"max_clients\":%d,"
		"\"max_players\":%d,"
		"\"passworded\":%s,"
		"\"game_type\":\"%s\","
		"\"name\":\"%s\","
		"\"map\":{"
		"\"name\":\"%s\","
		"\"sha256\":\"%s\","
		"\"size\":%d"
		"},"
		"\"version\":\"0.7↔%s\","
		"\"client_score_kind\":\"score\","
		"\"clients\":[",
		MaxClients,
		MaxPlayers,
		JsonBool(g_Config.m_Password[0]),
		EscapeJson(aGameType, sizeof(aGameType), GameServer()->GameType()),
		EscapeJson(aName, sizeof(aName), aName),
		EscapeJson(aGameType, sizeof(aGameType), GameServer()->GameType()),
		aMapSha256,
		m_pMainMapData->m_MapSize,
		EscapeJson(aVersion, sizeof(aVersion), "0.7↔0.6 Teeworlds"));

	bool FirstPlayer = true;
	int Count = 0;
	for(auto& Client : m_aClients)
	{
		char aCName[32];
		char aCClan[32];

		char aExtraPlayerInfo[512];
		GameServer()->OnUpdatePlayerServerInfo(aExtraPlayerInfo, sizeof(aExtraPlayerInfo), Client.first);

		char aClientInfo[1024];
		str_format(aClientInfo, sizeof(aClientInfo),
			"%s{"
			"\"name\":\"%s\","
			"\"clan\":\"%s\","
			"\"country\":%d,"
			"\"score\":%d,"
			"\"is_player\":%s"
			"%s"
			"}",
			!FirstPlayer ? "," : "",
			EscapeJson(aCName, sizeof(aCName), ClientName(Client.first)),
			EscapeJson(aCClan, sizeof(aCClan), ClientClan(Client.first)),
			Client.second.m_Country,
			Client.second.m_Score,
			JsonBool(GameServer()->IsClientPlayer(Client.first)),
			aExtraPlayerInfo);
		str_append(aInfo, aClientInfo, sizeof(aInfo));
		FirstPlayer = false;

		Count++;

		if(Count >= SERVERINFO_MAX_CLIENTS)
			break;
	}

	str_append(aInfo, "]}", sizeof(aInfo));

	m_pRegister->OnNewInfo(aInfo);
}

void CServer::UpdateServerInfo(bool Resend)
{
	if(!m_pRegister)
		return;

	UpdateRegisterServerInfo();

	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 2; j++)
			CacheServerInfo(&m_aServerInfoCache[i * 2 + j], i, j);

	for(int i = 0; i < 2; i ++)
		CacheServerInfoSixup(&m_aSixupServerInfoCache[i], i);

	if(Resend)
	{
		for(auto& Client : m_aClients)
		{
			SendServerInfo(m_NetServer.ClientAddr(Client.first), -1, SERVERINFO_INGAME, false);
		}
	}

	m_ServerInfoNeedsUpdate = false;
}

void CServer::PumpNetwork(bool PacketWaiting)
{
	CNetChunk Packet;
	SECURITY_TOKEN ResponseToken;

	m_NetServer.Update();

	if(PacketWaiting)
	{
		// process packets
		while(m_NetServer.Recv(&Packet, &ResponseToken))
		{
			if(Packet.m_ClientID == -1)
			{
				if(ResponseToken == NET_SECURITY_TOKEN_UNKNOWN && m_pRegister->OnPacket(&Packet))
					continue;

				{
					int ExtraToken = 0;
					int Type = -1;
					if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
					{
						if(Packet.m_Flags & NETSENDFLAG_EXTENDED)
						{
							Type = SERVERINFO_EXTENDED;
							ExtraToken = (Packet.m_aExtraData[0] << 8) | Packet.m_aExtraData[1];
						}
						else
							Type = SERVERINFO_VANILLA;
					}
					else if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO_64_LEGACY) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO_64_LEGACY, sizeof(SERVERBROWSE_GETINFO_64_LEGACY)) == 0)
					{
						Type = SERVERINFO_64_LEGACY;
					}
					
					if(Type == SERVERINFO_VANILLA && ResponseToken != NET_SECURITY_TOKEN_UNKNOWN)
					{
						CUnpacker Unpacker;
						Unpacker.Reset((unsigned char *)Packet.m_pData + sizeof(SERVERBROWSE_GETINFO), Packet.m_DataSize - sizeof(SERVERBROWSE_GETINFO));
						int SrvBrwsToken = Unpacker.GetInt();
						if(Unpacker.Error())
						{
							continue;
						}

						CPacker Packer;
						GetServerInfoSixup(&Packer, SrvBrwsToken, RateLimitServerInfoConnless());

						CNetChunk Response;
						Response.m_ClientID = -1;
						Response.m_Address = Packet.m_Address;
						Response.m_Flags = NETSENDFLAG_CONNLESS;
						Response.m_pData = Packer.Data();
						Response.m_DataSize = Packer.Size();
						m_NetServer.SendConnlessSixup(&Response, ResponseToken);
					}
					else if(Type != -1)
					{
						int Token = ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)];
						Token |= ExtraToken << 8;
						SendServerInfoConnless(&Packet.m_Address, Token, Type);
					}
				}
			}
			else
			{
				int GameFlags = 0;
				if(Packet.m_Flags & NET_CHUNKFLAG_VITAL)
				{
					GameFlags |= MSGFLAG_VITAL;
				}

				ProcessClientPacket(&Packet);
			}
		}
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

int* CServer::GetIdMap(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return nullptr;

	return m_aClients[ClientID].m_IDMap.m_IDMap;
}

void CServer::ClearIdMap(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return;	

	for(int i = 0;i < DDNET_MAX_CLIENTS; i ++)
	{
		m_aClients[ClientID].m_IDMap.m_IDMap[i] = -1;
	}
	m_aClients[ClientID].m_IDMap.m_IDMap[0] = ClientID;
}

char *CServer::GetMapName(CMapData *pMapData)
{
	// get the name of the map without his path
	char *pMapShortName = &pMapData->m_aMap[0];
	for(int i = 0; i < str_length(pMapData->m_aMap)-1; i++)
	{
		if(pMapData->m_aMap[i] == '/' || pMapData->m_aMap[i] == '\\')
			pMapShortName = &pMapData->m_aMap[i+1];
	}
	return pMapShortName;
}

void CServer::RegenerateMap()
{
	m_MapReload = 1;
}

int CServer::LoadMap(const char *pMapName)
{
	CUuid Uuid = CalculateUuid(pMapName);
	if(m_MapDatas.count(Uuid))
		return 0;

	m_MapReload = false;
	//DATAFILE *df;
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);

	CMapData MapData;
	MapData.m_pMapData = 0;
	MapData.m_MapSize = 0;

	MapData.m_pMap = CreateEngineMap();

	bool Menu = false;
	if(!MapData.m_pMap->Load(aBuf, Storage()))
	{
		str_format(aBuf, sizeof(aBuf), "chunk/%s.map", pMapName);
		if(!MapData.m_pMap->Load(aBuf, Storage()))
		{
			str_format(aBuf, sizeof(aBuf), "menu/%s.map", pMapName); // this is menu
			if(!MapData.m_pMap->Load(aBuf, Storage()))
				return 0;
			Menu = true;
		}
	}

	str_copy(MapData.m_aMap, pMapName, sizeof(MapData.m_aMap));
	//map_set(df);

	// load complete map into memory for download
	{
		free(MapData.m_pMapData);
		void *pData;
		Storage()->ReadFile(aBuf, IStorage::TYPE_ALL, &pData, &MapData.m_MapSize);
		MapData.m_pMapData = (unsigned char *)pData;

		MapData.m_MapSha256 = sha256(MapData.m_pMapData, MapData.m_MapSize);
		
		// get the crc of the map
		MapData.m_MapCrc = MapData.m_pMap->Crc();
	}

	m_MapDatas[Uuid] = MapData;

	if(Menu && !m_pMenuMapData)
		m_pMenuMapData = &m_MapDatas[Uuid];
	else if(!m_pMainMapData)
		m_pMainMapData = &m_MapDatas[Uuid];

	return 1;
}

int CServer::GenerateMap(const char *pMapName)
{
	CMapGen MapGen(Storage(), Console(), this);
	
	if(!MapGen.CreateMap(pMapName))
		return 0;

	LoadMap(pMapName);
	
	return 1;
}

void CServer::CreateMapThread(const char *pMapName)
{
	char aBuf[256];
	str_copy(aBuf, pMapName);
	std::thread Thread([this, aBuf]()
	{
		static std::mutex s_Lock;
		if(!s_Lock.try_lock())
		{
			log_error("server", "failed lock create map thread");
			return;
		}

		// load map
		if(!GenerateMap(aBuf) && !m_MainMapLoaded)
		{
			s_Lock.unlock();

			log_error("server", "failed generate main map");
			Console()->ExecuteLine("shutdown", -1);
			
			return;
		}

		s_Lock.unlock();

		m_MainMapLoaded = true;
		UpdateServerInfo();
		log_info("server", "Loaded new worlds '%s'", aBuf);
	});
	Thread.detach();
}

int CServer::Run()
{
	//
	m_PrintCBIndex = Console()->RegisterPrintCallback(g_Config.m_ConsoleOutputLevel, SendRconLineAuthed, this);
	
	m_MainMapLoaded = false;

	CMapGen MapGen(Storage(), Console(), this);
	
	if(!MapGen.CreateMenu("menu"))
		return 0;

	LoadMap("menu");

	// load map
	CreateMapThread("moon");

	// start server
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}

	int Port = g_Config.m_SvPort;
	for(BindAddr.port = Port != 0 ? Port : 8303; !m_NetServer.Open(BindAddr, &m_ServerBan, g_Config.m_SvMaxClientsPerIP); BindAddr.port++)
	{
		if(Port != 0 || BindAddr.port >= 8310)
		{
			dbg_msg("server", "couldn't open socket. port %d might already be in use", BindAddr.port);
			return -1;
		}
	}

	IEngine *pEngine = Kernel()->RequestInterface<IEngine>();
	m_pRegister = CreateRegister(&g_Config, m_pConsole, pEngine, g_Config.m_SvPort, m_NetServer.GetGlobalToken());

	m_NetServer.SetCallbacks(NewClientCallback, NewClientNoAuthCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Console(), &m_ServerBan);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "===========================================");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "+   |                     -----           +");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "+   |  ,   ,,---.,---..---. | ,---.,---.  +");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "+   |  |   ||   |,---||     | |---'|---'  +");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "+   |  '---''   '`---''     ' `---'`---'  +");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "+   `------                               +");
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "===========================================");
	
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", g_Config.m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	GameServer()->OnInit();

	// process pending commands
	m_pConsole->StoreCommands(false);
	m_pRegister->OnConfigChange();

	// start game
	{
		m_Active = true;
		bool PacketWaiting = false;

		m_GameStartTime = time_get();
	
		while(m_RunServer)
		{
			if(!m_Active && m_MainMapLoaded)
				PumpNetwork(PacketWaiting);

			set_new_tick();

			int64_t  t = time_get();
			int NewTicks = 0;

			// load new map
			if(m_CurrentGameTick >= 0x5FFFFFFF)// force reload to make sure the ticks stay within a valid range
			{
				// new map loaded
				GameServer()->OnShutdown();

				for(auto& Client : m_aClients)
				{
					if(Client.second.m_State <= CClient::STATE_AUTH)
						continue;

					SendMap(Client.first);
					Client.second.Reset();
					Client.second.m_State = CClient::STATE_CONNECTING;
				}

				m_GameStartTime = time_get();
				m_CurrentGameTick = 0;
				m_ServerInfoFirstRequest = 0;
				Kernel()->ReregisterInterface(GameServer());
				GameServer()->OnInit();
				UpdateServerInfo(true);
			}
			
			while(t > TickStartTime(m_CurrentGameTick + 1))
			{
				m_CurrentGameTick++;
				NewTicks++;

				// apply new input
				for(auto& Client : m_aClients)
				{
					if(Client.second.m_State != CClient::STATE_INGAME)
						continue;
					for(auto &Input : Client.second.m_aInputs)
					{
						if(Input.m_GameTick == Tick())
						{
							GameServer()->OnClientPredictedInput(Client.first, Input.m_aData);
							break;
						}
					}
				}

				GameServer()->OnTick();
			}

			// snap game
			if(NewTicks)
			{
				if(g_Config.m_SvHighBandwidth || (m_CurrentGameTick % 2) == 0)
				{
					DoSnapshot();

					UpdateClientRconCommands();
				}
			}

			// master server stuff
			if(m_MainMapLoaded)
				m_pRegister->Update();

			if(m_MainMapLoaded && m_ServerInfoNeedsUpdate)
				UpdateServerInfo();

			if(m_MainMapLoaded && m_Active)
				PumpNetwork(PacketWaiting);

			m_Active = m_aClients.size() > 0;

			if(!m_MainMapLoaded)	
				continue;

			// wait for incoming data
			if(!m_Active)
			{
				if(g_Config.m_SvReloadWhenEmpty == 1)
				{
					m_MapReload = true;
					g_Config.m_SvReloadWhenEmpty = 0;
				}
				else if(g_Config.m_SvReloadWhenEmpty == 2 && !m_ReloadedWhenEmpty)
				{
					m_MapReload = true;
					m_ReloadedWhenEmpty = true;
				}

				if(g_Config.m_SvShutdownWhenEmpty)
					m_RunServer = 0;
				else
					PacketWaiting = net_socket_read_wait(m_NetServer.Socket(), 1000000);
			}
			else
			{
				m_ReloadedWhenEmpty = false;

				set_new_tick();
				t = time_get();
				int x = (TickStartTime(m_CurrentGameTick + 1) - t) * 1000000 / time_freq() + 1;

				PacketWaiting = x > 0 ? net_socket_read_wait(m_NetServer.Socket(), x) : true;
			}

			if(InterruptSignaled)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "interrupted");
				break;
			}
		}
	}
	// disconnect all clients on shutdown
	for(auto& Client : m_aClients)
	{
		m_NetServer.Drop(Client.first, "Server shutdown");
	}

	m_Econ.Shutdown();

	GameServer()->OnShutdown();

	m_pRegister->OnShutdown();
	
	for(auto &Data : m_MapDatas)
	{
		Data.second.m_pMap->Unload();

		if(Data.second.m_pMapData)
			free(Data.second.m_pMapData);
	}
	return 0;
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if(pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *)pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *)pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer* pThis = static_cast<CServer *>(pUser);

	for(auto& Client : pThis->m_aClients)
	{
		net_addr_str(pThis->m_NetServer.ClientAddr(Client.first), aAddrStr, sizeof(aAddrStr), true);
		if(Client.second.m_State == CClient::STATE_INGAME)
		{
			const char *pAuthStr = Client.second.m_Authed == CServer::AUTHED_ADMIN ? "(Admin)" :
									Client.second.m_Authed == CServer::AUTHED_MOD ? "(Mod)" : "";
			str_format(aBuf, sizeof(aBuf), "id=%d addr=%s name='%s' score=%d secure=%s %s", Client.first, aAddrStr,
				Client.second.m_aName, Client.second.m_Score, pThis->m_NetServer.HasSecurityToken(Client.first) ? "yes":"no", pAuthStr);
		}
		else
			str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", Client.first, aAddrStr);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
	}
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_RunServer = 0;
}

void CServer::DemoRecorder_HandleAutoStart()
{
	if(g_Config.m_SvAutoDemoRecord)
	{
		m_DemoRecorder.Stop();
		char aFilename[128];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_DemoRecorder.Start(Storage(), m_pConsole, aFilename, "0.6 626fce9a778df4d4", m_pMainMapData->m_aMap, m_pMainMapData->m_MapCrc, "server");
		if(g_Config.m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", g_Config.m_SvAutoDemoMax);
		}
	}
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_DemoRecorder.IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	CServer* pServer = (CServer *)pUser;
	char aFilename[128];

	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_DemoRecorder.Start(pServer->Storage(), pServer->Console(), aFilename, "0.6 626fce9a778df4d4", pServer->m_pMainMapData->m_aMap, pServer->m_pMainMapData->m_MapCrc, "server");
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_DemoRecorder.Stop();
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(!pServer->m_aClients.count(pServer->m_RconClientID))
	{
		if(pServer->IsSixup(pServer->m_RconClientID))
		{
			CMsgPacker Msg(protocol7::NETMSG_RCON_AUTH_OFF, true, true);
			pServer->SendMsg(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID);
		}
		else
		{
			CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
			Msg.AddInt(0);	//authed
			Msg.AddInt(0);	//cmdlist
			pServer->SendMsg(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID);
		}

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", pServer->m_RconClientID);
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CServer::ConNewMap(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->CreateMapThread(pResult->GetString(0));
}

void CServer::ConchainSpecialInfoUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->UpdateServerInfo(true);
}

void CServer::ConchainMaxclientsUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		if(g_Config.m_SvMaxClients == 0)
			g_Config.m_SvMaxClients = 1;
		((CServer *)pUserData)->UpdateServerInfo(true);
	}
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
}

void CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(auto& Client : pThis->m_aClients)
			{
				if(Client.second.m_Authed != CServer::AUTHED_MOD ||
					(Client.second.m_pRconCmdToSend && str_comp(pResult->GetString(0), Client.second.m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, Client.first);
				else
					pThis->SendRconCmdRem(pInfo, Client.first);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
}

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	HttpInit(m_pStorage);

	// register console commands
	Console()->Register("kick", "i?r", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("shutdown", "", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");

	Console()->Register("record", "?s", CFGFLAG_SERVER|CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("new_map", "r", CFGFLAG_SERVER, ConNewMap, this, "Create new map");

	Console()->Chain("sv_name", ConchainSpecialInfoUpdate, this);
	Console()->Chain("password", ConchainSpecialInfoUpdate, this);

	Console()->Chain("sv_max_clients", ConchainMaxclientsUpdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);
	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_pGameServer->OnConsoleInit();
}


int CServer::SnapNewID()
{
	return m_IDPool.NewID();
}

void CServer::SnapFreeID(int ID)
{
	m_IDPool.FreeID(ID);
}

void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	dbg_assert(ID >= 0 && ID <=0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

void HandleSigIntTerm(int Param)
{
	InterruptSignaled = 1;

	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

int main(int argc, const char **argv) // ignore_convention
{
	bool Silent = false;

	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			Silent = true;
		}
	}
	
	std::vector<std::shared_ptr<ILogger>> vpLoggers;
	if(!Silent)
	{
		vpLoggers.push_back(std::shared_ptr<ILogger>(log_logger_stdout()));
	}
	std::shared_ptr<CFutureLogger> pFutureFileLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureFileLogger);
	std::shared_ptr<CFutureLogger> pFutureConsoleLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureConsoleLogger);
	std::shared_ptr<CFutureLogger> pFutureAssertionLogger = std::make_shared<CFutureLogger>();
	vpLoggers.push_back(pFutureAssertionLogger);
	log_set_global_logger(log_logger_collection(std::move(vpLoggers)).release());

	if(secure_random_init() != 0)
	{
		log_error("secure", "could not initialize secure RNG");
		return -1;
	}

	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("LunarTee", pFutureConsoleLogger, 2);

	// IEngineMap *pEngineMap = CreateEngineMap();
	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON);
	IStorage *pStorage = CreateStorage("LunarTee", IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();

	pFutureAssertionLogger->Set(CreateAssertionLogger(pStorage, MOD_NAME));

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		// RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap)); // register as both
		// RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pGameServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);

		if(RegisterFail)
			return -1;
	}

	pEngine->Init();
	pConfig->Init();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	pServer->m_pLocalization = new CLocalization(pStorage);
	if(!pServer->m_pLocalization->Init())
	{
		log_error("localization", "could not initialize localization");
		return -1;
	}

	// parse the command line arguments
	if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc-1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	log_set_loglevel((LEVEL)g_Config.m_Loglevel);
	const int Mode = g_Config.m_Logappend ? IOFLAG_APPEND : IOFLAG_WRITE;
	if(g_Config.m_Logfile[0])
	{
		IOHANDLE Logfile = pStorage->OpenFile(g_Config.m_Logfile, Mode, IStorage::TYPE_ALL);
		if(Logfile)
		{
			pFutureFileLogger->Set(log_logger_file(Logfile));
		}
		else
		{
			log_error("server", "failed to open '%s' for logging", g_Config.m_Logfile);
		}
	}
	auto pServerLogger = std::make_shared<CServerLogger>(pServer);
	pEngine->SetAdditionalLogger(pServerLogger);

	// run the server
	log_info("server", "starting...");
	pServer->Run();

	pServerLogger->OnServerDeletion();
	// free
	delete pServer->m_pLocalization;

	delete pKernel;
	
	return 0;
}

int CServer::GetClientVersion(int ClientID)
{
	// Assume latest client version for server demos
	if(ClientID == -1)
		return DDNET_VERSIONNR;

	CClientInfo Info;
	if(GetClientInfo(ClientID, &Info))
		return Info.m_DDNetVersion;
	return VERSION_NONE;
}

bool CServer::Is64Player(int ClientID)
{
	return GetClientVersion(ClientID) >= VERSION_DDNET_OLD || IsSixup(ClientID);
}

IMap *CServer::GetClientMap(int ClientID)
{
	return m_aClients[ClientID].m_pMapData->m_pMap;
}

bool CServer::IsActive()
{
	return m_Active;
}

void CServer::ChangeClientMap(int ClientID, CUuid *pMapID)
{
	if(!m_aClients.count(ClientID))
		return;

	if(m_aClients[ClientID].m_State <= CClient::STATE_AUTH)
		return;

	m_aClients[ClientID].m_pMapData = &m_MapDatas[*pMapID];

	m_aClients[ClientID].m_InMenu = m_aClients[ClientID].m_pMapData == m_pMenuMapData;
	
	SendMap(ClientID);
	m_aClients[ClientID].Reset();
	m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
}

int CServer::GetLoadedMapNum() const
{
	return (int) m_MapDatas.size();
}

int CServer::GetOneWorldPlayerNum(int ClientID)
{
	return m_pGameServer->GetOneWorldPlayerNum(ClientID);
}

void CServer::CreateNewTheardJob(std::shared_ptr<IJob> pJob)
{
	IEngine *pEngine = Kernel()->RequestInterface<IEngine>();
	pEngine->AddJob(pJob);
}

bool CServer::IsSixup(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return false;

	return m_aClients[ClientID].m_Sixup;
}

bool CServer::IsInMenu(int ClientID)
{
	if(!m_aClients.count(ClientID))
		return false;

	return m_aClients[ClientID].m_InMenu;
}

const char *CServer::GetMainMap()
{
	return m_pMainMapData->m_aMap;
}

const char *CServer::GetMenuMap()
{
	return m_pMenuMapData->m_aMap;
}