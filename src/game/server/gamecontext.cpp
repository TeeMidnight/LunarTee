/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>

#include <mutex>
#include <thread>

#include <base/math.h>

#include <engine/map.h>
#include <engine/console.h>

#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <engine/external/json/json.hpp>

#include <engine/shared/config.h>
#include <engine/shared/map.h>

#include <lunartee/localization//localization.h>
#include <engine/server/crypt.h>

#include <lunartee/item/item.h>
#include <lunartee/item/craft.h>

#include <lunartee/datacontroller.h>
#include <lunartee/postgresql.h>

#include "gamecontroller.h"
#include "gamecontext.h"

#include <zip.h>
enum
{
	RESET,
	NO_RESET
};


CClientMask const& CmaskAll() { static CClientMask bs; static bool init = false; if (!init) { init = true; bs.set(); } return bs; }
CClientMask CmaskOne(int ClientID) { CClientMask bs; bs[ClientID] = 1; return bs; }
CClientMask CmaskAllExceptOne(int ClientID) { CClientMask bs; bs.set(); bs[ClientID] = 0; return bs; }

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	m_BiggestBotID = -1;
	m_FirstFreeBotID = MAX_CLIENTS;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_LockTeams = 0;
	m_ChatResponseTargetID = -1;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	for(auto &pPlayer : m_vpBotPlayers)
		delete pPlayer;
	for(auto &pWorld : m_vpWorlds)
		delete pWorld;
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	if(!m_Resetting)
		delete m_pBotController;

	delete m_pMenu;
}

void CGameContext::OnSetAuthed(int ClientID, int Level)
{
	if(m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_Authed = Level;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	CBotController* pController = m_pBotController;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_pBotController = pController;
	m_Tuning = Tuning;
}

int CGameContext::FindWorldIDWithWorld(CGameWorld *pGameWorld) const
{
	for(int i = 0; i < (int) m_vpWorlds.size(); i ++)
	{
		if(m_vpWorlds[i] == pGameWorld)
		{
			return i;
		}
	}

	return -1;
}

CGameWorld *CGameContext::CreateNewWorld(IMap *pMap, const char *WorldName)
{
	m_vpWorlds.push_back(new CGameWorld());
	int ID = m_vpWorlds.size() - 1;

	m_vpWorlds[ID]->SetGameServer(this);

	m_vpWorlds[ID]->Layers()->Init(pMap);
	m_vpWorlds[ID]->Collision()->Init(m_vpWorlds[ID]->Layers());

	m_vpWorlds[ID]->InitSpawnPos();

	str_copy(m_vpWorlds[ID]->m_aWorldName, WorldName);

	return m_vpWorlds[ID];
}

class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0)
		return 0;
	
	CPlayer *pPlayer = (ClientID >= MAX_CLIENTS) ? GetBotWithCID(ClientID) : m_apPlayers[ClientID];

	if(pPlayer && pPlayer->GetCharacter())
		return pPlayer->GetCharacter();
	
	return 0;
}

CPlayer *CGameContext::GetPlayer(int ClientID)
{
	if(ClientID < 0)
		return 0;
	
	CPlayer *pPlayer = (ClientID >= MAX_CLIENTS) ? GetBotWithCID(ClientID) : m_apPlayers[ClientID];

	if(!pPlayer)
		return 0;
	else
		return pPlayer;
}

CGameWorld *CGameContext::FindWorldWithClientID(int ClientID) const
{
	if(m_apPlayers[ClientID])
		return m_apPlayers[ClientID]->GameWorld();

	return nullptr;
}

CGameWorld *CGameContext::FindWorldWithMap(IMap *pMap)
{
	if(m_vpWorlds.size() == 0)
		return nullptr;
		
	auto i = std::find_if(m_vpWorlds.begin(), m_vpWorlds.end(), 
		[pMap](CGameWorld *pGameWorld)
		{
			return pGameWorld->Layers()->Map() == pMap;
		});

	if(i != m_vpWorlds.end())
		return *i;
	
	return nullptr;
}

CGameWorld *CGameContext::FindWorldWithName(const char *WorldName)
{
	if(m_vpWorlds.size() == 0)
		return nullptr;

	auto i = std::find_if(m_vpWorlds.begin(), m_vpWorlds.end(), 
		[WorldName](CGameWorld *pGameWorld)
		{
			return !str_comp(pGameWorld->m_aWorldName, WorldName);
		});

	if(i != m_vpWorlds.end())
		return *i;
	
	return nullptr;
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount, CClientMask Mask)
{
	float a = 3 * pi / 2 + Angle;
	//float a = get_angle(dir);
	float s = a - pi / 3;
	float e = a + pi / 3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, (i + 1) / (float)(Amount + 2));
		CNetEvent_DamageInd *pEvent = m_Events.Create<CNetEvent_DamageInd>(Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f * 256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos, CClientMask Mask)
{
	// create the event
	CNetEvent_HammerHit *pEvent = m_Events.Create<CNetEvent_HammerHit>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, CClientMask Mask)
{
	// create the event
	CNetEvent_Explosion *pEvent = m_Events.Create<CNetEvent_Explosion>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	// deal damage
	CGameWorld *pGameWorld = FindWorldWithClientID(Owner);

	if(!pGameWorld)
		return;

	float Radius = 135.0f;
	float InnerRadius = 48.0f;

	for(CCharacter *pChr = (CCharacter *) pGameWorld->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChr; pChr = (CCharacter *) pChr->TypeNext())
	{
		vec2 Diff = pChr->m_Pos - Pos;
		vec2 ForceDir(0, 1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		float Strength;
		Strength = pGameWorld->m_Core.m_Tuning.m_ExplosionStrength;

		float Dmg = Strength * l;
		if(!(int)Dmg)
			continue;

		pChr->TakeDamage(ForceDir * Dmg * 2, NoDamage ? 0 : (int)Dmg, Owner, Weapon);
	}
}

void CGameContext::CreatePlayerSpawn(vec2 Pos, CClientMask Mask)
{
	// create the event
	CNetEvent_Spawn *pEvent = m_Events.Create<CNetEvent_Spawn>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID, CClientMask Mask)
{
	// create the event
	CNetEvent_Death *pEvent = m_Events.Create<CNetEvent_Death>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, CClientMask Mask)
{
	if(Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = m_Events.Create<CNetEvent_SoundWorld>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}

void CGameContext::SendMotd(int To, const char *pText)
{
	if(m_apPlayers[To])
	{
		CNetMsg_Sv_Motd Msg;
		
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	// only for demo record
	if(To < 0)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, To);
}

void CGameContext::SendChatTarget_Localization(int To, const char *pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);

	if(To >= MAX_CLIENTS)
		return;
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	std::string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			
			Msg.m_pMessage = Buffer.c_str();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendBroadcast_Localization(const char *pText, int ClientID, ...)
{
	CNetMsg_Sv_Broadcast Msg;
	int Start = (ClientID < 0 ? 0 : ClientID);
	int End = (ClientID < 0 ? MAX_CLIENTS : ClientID+1);

	if(ClientID >= MAX_CLIENTS)
		return;
	
	std::string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, ClientID);
	
	// only for server demo record
	if(ClientID < 0)
	{
		Server()->Localization()->Format_VL(Buffer, "en", _(pText), VarArgs);
		Msg.m_pMessage = Buffer.c_str();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);
	}

	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), _(pText), VarArgs);
			
			Msg.m_pMessage = Buffer.c_str();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
	}
	
	va_end(VarArgs);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc);
	str_copy(m_aVoteCommand, pCommand);
	str_copy(m_aVoteReason, pReason);
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;
}

void CGameContext::SendFakeTuningParams(int ClientID)
{
	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(m_apPlayers[i])
			{
				SendFakeTuningParams(i);
			}
		}
		return;
	}

	CheckPureTuning();

	SendTuningParams(ClientID, FindWorldWithClientID(ClientID) ? FindWorldWithClientID(ClientID)->m_Core.m_Tuning : m_Tuning);
}

void CGameContext::SendTuningParams(int ClientID, const CTuningParams &Params)
{
	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	const int *pParams = (const int *)&Params;

	for(unsigned i = 0; i < sizeof(m_Tuning) / sizeof(int); i++)
	{
		Msg.AddInt(pParams[i]);
	}
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnTick()
{
	// update datapack
	Datas()->Tick();

	// check tuning
	CheckPureTuning();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	for(int i = 0; i < (int) m_vpBotPlayers.size(); i++)
	{
		if(m_vpBotPlayers[i])
		{
			m_vpBotPlayers[i]->Tick();
		}else
		{
			m_vpBotPlayers.erase(m_vpBotPlayers.begin() + i);
			i --;
		}
	}

	for(int i = 0; i < (int) m_vpWorlds.size(); i ++)
	{
		m_vpWorlds[i]->m_Core.m_Tuning = m_Tuning;
		m_vpWorlds[i]->Tick();
	}
	
	m_pBotController->Tick();

	m_pController->Tick();

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChatTarget_Localization(-1, _("Vote aborted"));
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand, -1);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChatTarget_Localization(-1, _("Vote passed"));

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChatTarget_Localization(-1, _("Vote failed"));
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientEnter(int ClientID)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	if(FindWorldIDWithWorld((m_apPlayers[ClientID]->GameWorld())) == 0)
	{
		SendChatTarget_Localization(-1, _("'{STR}' entered the server"), Server()->ClientName(ClientID));

		SendChatTarget_Localization(ClientID, _("===Welcome to LunarTee==="));
		SendChatTarget_Localization(ClientID, _("Call vote is game menu"));
		SendChatTarget_Localization(ClientID, _("Show clan plate to show health bar"));
	}else
	{
		SendChatTarget_Localization(-1, _("'{STR}' entered other world"), Server()->ClientName(ClientID));
	}
	m_VoteUpdate = true;
	Server()->ExpireServerInfo();
}

void CGameContext::OnClientConnected(int ClientID, const char *WorldName)
{
	CGameWorld *pGameWorld = FindWorldWithName(WorldName);
	if(!pGameWorld)
		pGameWorld = CreateNewWorld(Server()->GetClientMap(ClientID), WorldName);

	if(m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->SetGameWorld(pGameWorld);
		m_apPlayers[ClientID]->Reset();
	}	
	else 
		m_apPlayers[ClientID] = new CPlayer(pGameWorld, ClientID, TEAM_SPECTATORS);
	

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

	Server()->ExpireServerInfo();
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(pReason);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = nullptr;

	Datas()->Item()->ClearInv(ClientID, false);

	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
		{
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}

	Server()->ExpireServerInfo();
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
			if(!str_utf8_check(pMsg->m_pMessage))
			{
				return;
			}
			int Team = pMsg->m_Team ? pPlayer->GetTeam() : CGameContext::CHAT_ALL;

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
 			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
 			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 16 characters per second)
			if(Length == 0 || (pMsg->m_pMessage[0] != '/' && g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed()*((15+Length)/16) > Server()->Tick()))
				return;
			
			if(pMsg->m_pMessage[0] == '/' || pMsg->m_pMessage[0] == '\\')
			{
				switch(m_apPlayers[ClientID]->m_Authed)
				{
					case IServer::AUTHED_ADMIN:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
						break;
					case IServer::AUTHED_MOD:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
						break;
					default:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
				}	
				m_ChatResponseTargetID = ClientID;

				Console()->ExecuteLineFlag(pMsg->m_pMessage + 1, ClientID, CFGFLAG_CHAT);
				
				m_ChatResponseTargetID = -1;
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
			}
			else
			{
				SendChat(ClientID, Team, pMsg->m_pMessage);
				pPlayer->m_LastChat = Server()->Tick();
			}
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			
			if(Menu()->UseOptions(pMsg->m_pValue, pMsg->m_pReason, ClientID))
				return;

			int64_t Now = Server()->Tick();
			pPlayer->m_LastVoteTry = Now;
			if(pPlayer->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget_Localization(ClientID, _("Spectators aren't allowed to start a vote."));
				return;
			}

			if(m_VoteCloseTime)
			{
				SendChatTarget_Localization(ClientID, _("Wait for current vote to end before calling a new one."));
				return;
			}

			int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
			if(pPlayer->m_LastVoteCall && Timeleft > 0)
			{
				int Seconds = (Timeleft/Server()->TickSpeed())+1;
				SendChatTarget_Localization(ClientID, _("You must wait {INT} seconds before making another vote"), &Seconds, NULL);
				return;
			}

			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			const char *pReason = pMsg->m_pReason[0] ? pMsg->m_pReason : "No reason given";

			if(str_comp_nocase(pMsg->m_pType, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_pValue, pOption->m_aDescription) == 0)
					{
						SendChatTarget_Localization(-1, _("'{STR}' called vote to change server option '{STR}' ({STR})"),
									Server()->ClientName(ClientID), pOption->m_aDescription,
									pReason, NULL);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
				{
					SendChatTarget_Localization(ClientID, _("'{STR}' isn't an option on this server"), pMsg->m_pValue);
					return;
				}
			}
			else if(str_comp_nocase(pMsg->m_pType, "kick") == 0)
			{
				if(!g_Config.m_SvVoteKick)
				{
					SendChatTarget_Localization(ClientID, _("Server does not allow voting to kick players"));
					return;
				}

				if(g_Config.m_SvVoteKickMin)
				{
					int PlayerNum = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
						if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
							++PlayerNum;

					if(PlayerNum < g_Config.m_SvVoteKickMin)
					{
						SendChatTarget_Localization(ClientID, _("Kick voting requires {INT} players on the server"), g_Config.m_SvVoteKickMin);
						return;
					}
				}

				int KickID = str_toint(pMsg->m_pValue);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget_Localization(ClientID, _("Invalid client id to kick"));
					return;
				}
				if(KickID == ClientID)
				{
					SendChatTarget_Localization(ClientID, _("You can't kick yourself"));
					return;
				}

				if (!Server()->ReverseTranslate(KickID, ClientID))
					return;

				if(Server()->IsAuthed(KickID))
				{
					SendChatTarget_Localization(ClientID, _("You can't kick admins"));
					SendChatTarget_Localization(KickID, _("'{STR}' called for vote to kick you"), Server()->ClientName(ClientID));
					return;
				}

				SendChatTarget_Localization(-1, "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
				str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
				if (!g_Config.m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);\
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
				}
			}
			else if(str_comp_nocase(pMsg->m_pType, "spectate") == 0)
			{
				if(!g_Config.m_SvVoteSpectate)
				{
					SendChatTarget_Localization(ClientID, _("Server does not allow voting to move players to spectators"));
					return;
				}

				int SpectateID = str_toint(pMsg->m_pValue);
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget_Localization(ClientID, _("Invalid client id to move"));
					return;
				}
				if(SpectateID == ClientID)
				{
					SendChatTarget_Localization(ClientID, _("You can't move yourself"));
					return;
				}
				if (!Server()->ReverseTranslate(SpectateID, ClientID))
					return;

				SendChatTarget_Localization(-1, "'%s' called for vote to move '%s' to spectators (%s)", 
					Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
				str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
			}

			if(aCmd[0])
			{
				StartVote(aDesc, aCmd, pReason);
				pPlayer->m_Vote = 1;
				pPlayer->m_VotePos = m_VotePos = 1;
				m_VoteCreator = ClientID;
				pPlayer->m_LastVoteCall = Now;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(!m_VoteCloseTime)
				return;

			if(pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
			{
				m_VoteUpdate = true;
			}
			if(pPlayer->IsLogin())
			{
				pPlayer->SetTeam(pMsg->m_Team);
				pPlayer->m_TeamChangeTick = Server()->Tick();
			}else
			{
				SendChatTarget_Localization(ClientID, _("Please login!"));
			}
		}
		else if(MsgID == NETMSGTYPE_CL_ISDDNETLEGACY)
		{
			IServer::CClientInfo Info;
			if(Server()->GetClientInfo(ClientID, &Info) && Info.m_GotDDNetVersion)
			{
				return;
			}
			int DDNetVersion = pUnpacker->GetInt();
			if(pUnpacker->Error() || DDNetVersion < 0)
			{
				DDNetVersion = VERSION_DDRACE;
			}
			Server()->SetClientDDNetVersion(ClientID, DDNetVersion);
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*0.5 > Server()->Tick())
				return;

			if(pMsg->m_SpectatorID != SPEC_FREEVIEW)
				if (!Server()->ReverseTranslate(pMsg->m_SpectatorID, ClientID))
					return;

			if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID)
				return;

			if(pMsg->m_SpectatorID >= MAX_CLIENTS)
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
				SendChatTarget_Localization(ClientID, _("Invalid spectator id {INT} used"), pMsg->m_SpectatorID);
			else
			{
				pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
				return;

			CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set infos
			char aOldName[MAX_NAME_LENGTH];
			str_copy(aOldName, Server()->ClientName(ClientID));
			Server()->SetClientName(ClientID, pMsg->m_pName);
			if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
			{
				SendChatTarget_Localization(-1, _("'{STR}' changed name to '{STR}'"), aOldName, Server()->ClientName(ClientID));
			}
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin);
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);

			Server()->ExpireServerInfo();
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
		}
		else if (MsgID == NETMSGTYPE_CL_KILL)
		{
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReady)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin);
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);

			// send vote options
			Menu()->GetMenuPage("MAIN")->m_pfnCallback(ClientID, "SHOW", "", 
				Menu()->GetMenuPage("MAIN")->m_pUserData);
			
			// send fake tuning
			SendFakeTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);

			Server()->ExpireServerInfo();
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		//pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	//pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->TogglePause();
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription);
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription);
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				pSelf->SendChatTarget_Localization(-1, _("admin forced server option '{STR}' ({STR})"), pValue, pReason);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand, -1);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}
		pSelf->SendChatTarget_Localization(-1, _("admin moved '{STR}' to spectator ({STR})"), pSelf->Server()->ClientName(SpectateID), pReason);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf, -1);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
	{
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
		pSelf->SendChatTarget_Localization(-1, _("admin forced vote yes"));
	}
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
	{
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
		pSelf->SendChatTarget_Localization(-1, _("admin forced vote no"));	
	}
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConNextWorld(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext* pSelf = (CGameContext*) pUserData;
	int ClientID = pResult->GetClientID();
	if(!pSelf->m_apPlayers[ClientID])
		return;
	
	int MapID = pSelf->FindWorldIDWithWorld(pSelf->m_apPlayers[ClientID]->GameWorld()) + 1;

	if(MapID >= pSelf->Server()->GetLoadedMapNum())
		MapID = 0;

	pSelf->m_apPlayers[ClientID]->KillCharacter();
	pSelf->m_apPlayers[ClientID]->m_LoadingMap = true;
	
	pSelf->Server()->ChangeClientMap(ClientID, MapID);
}

void CGameContext::ConMapRegenerate(IConsole::IResult *pResult, void *pUserData)
{
	// CGameContext* pSelf = (CGameContext*) pUserData;
	// pSelf->Server()->RegenerateMap();
	// pSelf->SendChatTarget_Localization(-1, "Map will regenerate!");
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::ConAbout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext* pSelf = (CGameContext*) pUserData;
	int ClientID = pResult->GetClientID();

	char aThanksList[256];

	str_copy(aThanksList, "necropotame, DDNet, Kurosio");
	// necropotame made this frame, Localization from Kurosio.

	pSelf->SendChatTarget_Localization(ClientID, "====={STR}=====", MOD_NAME);
	pSelf->SendChatTarget_Localization(ClientID, _("{STR} by {STR}"), 
		MOD_NAME , "RemakePower");
	pSelf->SendChatTarget_Localization(ClientID, _("Thanks {STR}"), aThanksList);
	
}

void CGameContext::ConLanguage(IConsole::IResult *pResult, void *pUserData)
{
}

void CGameContext::ConEmote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	int EmoteType = 0;
	if(!str_comp(pResult->GetString(0), "angry"))
		EmoteType = EMOTE_ANGRY;
	else if(!str_comp(pResult->GetString(0), "blink"))
		EmoteType = EMOTE_BLINK;
	else if(!str_comp(pResult->GetString(0), "close"))
		EmoteType = EMOTE_BLINK;
	else if(!str_comp(pResult->GetString(0), "happy"))
		EmoteType = EMOTE_HAPPY;
	else if(!str_comp(pResult->GetString(0), "pain"))
		EmoteType = EMOTE_PAIN;
	else if(!str_comp(pResult->GetString(0), "surprise"))
		EmoteType = EMOTE_SURPRISE;
	else if(!str_comp(pResult->GetString(0), "normal"))
		EmoteType = EMOTE_NORMAL;
	else return;

	pPlayer->SetEmote(EmoteType);
}

static bool CheckStringSQL(const char *string)
{
	for(int i = 0;i < str_length(string); i ++)
	{
		bool Check = (((string[i] >= '0') && (string[i] <= '9')) || ((string[i] >= 'A') && (string[i] <= 'Z')) || ((string[i] >= 'a') && (string[i] <= 'z')));
		if(!Check)
			return false;
	}
	return true;
}

void CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	if(pResult->NumArguments() < 2)
	{
		pSelf->SendChatTarget_Localization(ClientID, _("Use /register <username> <password> to register"));
		return;
	}

	if(!CheckStringSQL(pResult->GetString(0)) || !CheckStringSQL(pResult->GetString(1)))
	{
		pSelf->SendChatTarget_Localization(ClientID, _("Invalid char"));

		pSelf->SendChatTarget_Localization(ClientID, _("Please input 0-9, A-Z, a-z for your username and password"));
		return;
	}

	if(str_length(pResult->GetString(0)) > MAX_ACCOUNTS_NAME_LENTH || str_length(pResult->GetString(0)) < MIN_ACCOUNTS_NAME_LENTH)
	{
		pSelf->SendChatTarget_Localization(ClientID, _("The length of the <Username> should be between {INT}-{INT}"), MIN_ACCOUNTS_NAME_LENTH, MAX_ACCOUNTS_NAME_LENTH);
		return;
	}

	if(str_length(pResult->GetString(1)) > MAX_ACCOUNTS_PASSWORD_LENTH || str_length(pResult->GetString(0)) < MIN_ACCOUNTS_PASSWORD_LENTH)
	{
		pSelf->SendChatTarget_Localization(ClientID, _("The length of the <Password> should be between {INT}-{INT}"), MIN_ACCOUNTS_PASSWORD_LENTH, MAX_ACCOUNTS_PASSWORD_LENTH);
		return;
	}

    char Username[MAX_ACCOUNTS_NAME_LENTH];
    char Password[MAX_ACCOUNTS_PASSWORD_LENTH];
    str_copy(Username, pResult->GetString(0));
    str_copy(Password, pResult->GetString(1));

    char aHash[64];
	Crypt(Password, (const unsigned char*) "d9", 1, 14, aHash);

	pSelf->Register(Username, aHash, ClientID);
}

void CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	if(pResult->NumArguments() < 2)
	{
		pSelf->SendChatTarget_Localization(ClientID, _("Use /login <username> <password> to login"));
		return;
	}

	if(!CheckStringSQL(pResult->GetString(0)) || !CheckStringSQL(pResult->GetString(1)))
	{
		pSelf->SendChatTarget_Localization(ClientID, _("Invalid char"));

		pSelf->SendChatTarget_Localization(ClientID, _("Please input 0-9, A-Z, a-z for your username and password"));
		return;
	}

    char Username[MAX_ACCOUNTS_NAME_LENTH];
    char Password[MAX_ACCOUNTS_PASSWORD_LENTH];
    str_copy(Username, pResult->GetString(0));
    str_copy(Password, pResult->GetString(1));

    char aHash[64];
	Crypt(Password, (const unsigned char*) "d9", 1, 14, aHash);

	pSelf->Login(Username, aHash, ClientID);
}

void CGameContext::SetClientLanguage(int ClientID, const char *pLanguage)
{
	Server()->SetClientLanguage(ClientID, pLanguage);
	if(m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->SetLanguage(pLanguage);
	}
}

const char *CGameContext::Localize(const char *pLanguageCode, const char *pText) const
{
	if(str_comp(pLanguageCode, "en") == 0)
		return pText;

	return Server()->Localization()->Localize(pLanguageCode, pText);
}

void CGameContext::ConsoleOutputCallback_Chat(const char *pLine, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	int ClientID = pSelf->m_ChatResponseTargetID;

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	const char *pLineOrig = pLine;

	static volatile int ReentryGuard = 0;

	if(ReentryGuard)
		return;
	ReentryGuard++;

	if(*pLine == '[')
	do
		pLine++;
	while((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0); // remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChatTarget(ClientID, pLine);

	ReentryGuard--;
}

void CGameContext::OnMenuOptionsInit()
{
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	m_ChatPrintCBIndex = Console()->RegisterPrintCallback(IConsole::OUTPUT_LEVEL_CHAT, ConsoleOutputCallback_Chat, this);
	
	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
	Console()->Register("next_world", "", CFGFLAG_SERVER, ConNextWorld, this, "go to next world");
	Console()->Register("regenerate_map", "", CFGFLAG_SERVER, ConMapRegenerate, this, "regenerate map");
	
	Console()->Register("about", "", CFGFLAG_CHAT, ConAbout, this, "Show information about the mod");

	Console()->Register("emote", "s?i", CFGFLAG_CHAT, ConEmote, this, "change emote");

	Console()->Register("register", "?s?s", CFGFLAG_CHAT, ConRegister, this, "register");
	Console()->Register("login", "?s?s", CFGFLAG_CHAT, ConLogin, this, "login");

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_Events.SetGameServer(this);

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_pMenu = new CMenu(this);

	Datas()->Init(m_pServer, m_pStorage, this);

	Sql()->CreateTables();

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	m_pController = new CGameController(this);
	m_pBotController = new CBotController(this);

	OnMenuOptionsInit();
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	if(!m_apPlayers[ClientID])
	{
		for(int i = 0; i < (int) m_vpWorlds.size(); i ++)
			m_vpWorlds[i]->Snap(ClientID);
	}
	else
	{
		UpdatePlayerMaps(ClientID);
		m_apPlayers[ClientID]->GameWorld()->Snap(ClientID);
	}

	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < (int) m_vpBotPlayers.size(); i ++)
	{
		m_vpBotPlayers[i]->SnapBot(ClientID);
	}

	for(int i = 0; i < MAX_CLIENTS; i ++)
	{
		if(!m_apPlayers[i])
			continue;
		m_apPlayers[i]->Snap(ClientID);
	}
}

void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

int CGameContext::GetClientVersion(int ClientID) const
{
	return Server()->GetClientVersion(ClientID);
}

const char *CGameContext::GameType() { return MOD_NAME; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::CraftItem(int ClientID, const char *pItemName)
{
	Datas()->Item()->Craft()->CraftItem(pItemName, ClientID);
}

int CGameContext::GetPlayerNum() const
{
	int Num = 0;
	for(int i = 0; i < MAX_CLIENTS; i ++)
	{
		if(!m_apPlayers[i])
			continue;
		if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
			continue;
		Num ++;
	}
	return Num;
}

CPlayer *CGameContext::GetBotWithCID(int ClientID)
{
	for(int i = 0; i < (int) m_vpBotPlayers.size(); i ++)
	{
		if(m_vpBotPlayers[i]->GetCID() == ClientID)
			return m_vpBotPlayers[i];
	}
	return 0x0;
}

int CGameContext::GetBotNum(CGameWorld *pGameWorld) const
{
	int Num = 0;
	for(int i = 0; i < (int) m_vpBotPlayers.size(); i ++)
	{
		if(m_vpBotPlayers[i]->GameWorld() == pGameWorld && 
			( m_vpBotPlayers[i]->GetCharacter() && !m_vpBotPlayers[i]->GetCharacter()->Pickable()))
			Num++;
	}
	return Num;
}

int CGameContext::GetBotNum() const
{
	return (int) m_vpBotPlayers.size();
}

void CGameContext::OnBotDead(int ClientID)
{
	for(int i = 0;i < (int) m_vpBotPlayers.size(); i ++)
	{
		if(m_vpBotPlayers[i]->GetCID() == ClientID)
		{
			delete m_vpBotPlayers[i];
			m_vpBotPlayers.erase(m_vpBotPlayers.begin() + i);
		}
	}

	m_VoteUpdate = true;
}

void CGameContext::UpdateBot()
{
	if(m_BiggestBotID < MAX_CLIENTS) // do not to update when create first bot
	{
		m_BiggestBotID = m_FirstFreeBotID;
		return;
	}

	bool BotIDs[m_BiggestBotID-MAX_CLIENTS+1];
	
	for(int i = 0; i < m_BiggestBotID-MAX_CLIENTS+1; i ++)
	{
		BotIDs[i] = false;
	}
	
	for(int i = 0; i < (int) m_vpBotPlayers.size(); i ++)
	{
		BotIDs[m_vpBotPlayers[i]->GetCID() - MAX_CLIENTS] = true;
	}

	int FirstFree = -1;
	int Biggest = -1;
	for(int i = 0; i < m_BiggestBotID-MAX_CLIENTS+1; i ++)
	{
		if(BotIDs[i])
		{
			Biggest = i;
		}else if(FirstFree == -1)
		{
			FirstFree = i;
		}
	}

	if(FirstFree == -1)
	{
		m_FirstFreeBotID = MAX_CLIENTS + Biggest + 1;
		m_BiggestBotID = MAX_CLIENTS + Biggest + 1;
	}else
	{
		m_FirstFreeBotID = MAX_CLIENTS + FirstFree;
		m_BiggestBotID = MAX_CLIENTS + Biggest;
	}
}

void CGameContext::CreateBot(CGameWorld *pGameWorld, CBotData BotData)
{
	UpdateBot();

	m_vpBotPlayers.push_back(new CPlayer(pGameWorld, m_FirstFreeBotID, 0, BotData));
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

void CGameContext::OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int ID)
{
	if(!m_apPlayers[ID])
		return;

	char aCSkinName[64];

	CPlayer::CTeeInfo &TeeInfo = m_apPlayers[ID]->m_TeeInfos;

	char aJsonSkin[400];
	aJsonSkin[0] = '\0';
	// 0.6
	if(TeeInfo.m_UseCustomColor)
	{
		str_format(aJsonSkin, sizeof(aJsonSkin),
			"\"name\":\"%s\","
			"\"color_body\":%d,"
			"\"color_feet\":%d",
			EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_SkinName),
			TeeInfo.m_ColorBody,
			TeeInfo.m_ColorFeet);
	}
	else
	{
		str_format(aJsonSkin, sizeof(aJsonSkin),
			"\"name\":\"%s\"",
			EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_SkinName));
	}
	str_format(aBuf, BufSize,
		",\"skin\":{"
		"%s"
		"},"
		"\"afk\":false,"
		"\"team\":%d",
		aJsonSkin,
		m_apPlayers[ID]->GetTeam());
}

int CGameContext::GetOneWorldPlayerNum(CGameWorld *pGameWorld) const
{
	return GetPlayerNum() + GetBotNum(pGameWorld);
}

int CGameContext::GetOneWorldPlayerNum(int ClientID) const
{
	return GetOneWorldPlayerNum(FindWorldWithClientID(ClientID));
}

void CGameContext::UpdatePlayerMaps(int ClientID)
{
	if(Server()->Tick() % g_Config.m_SvMapUpdateRate != 0)
		return;

	if (!Server()->ClientIngame(ClientID)) 
		return;

	if(!m_apPlayers[ClientID])
		return;

	int *pMap = Server()->GetIdMap(ClientID);
	int MaxClients = (Server()->Is64Player(ClientID) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS);

	std::vector<std::pair<float, int>> Dist;

	// compute distances
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		Dist.push_back(std::pair<float, int>(1e10, i));

		if (!Server()->ClientIngame(i) || !m_apPlayers[i])
			continue;

		if(i == ClientID)
		{
			Dist[Dist.size()-1].first = -1;
		}
		else if(m_apPlayers[i]->GameWorld() == m_apPlayers[ClientID]->GameWorld())
		{
			Dist[Dist.size()-1].first = minimum(3000.f, distance(m_apPlayers[ClientID]->m_ViewPos, m_apPlayers[i]->m_ViewPos));
		}
		else
		{
			Dist[Dist.size()-1].first = 3e3 + 1;
		}
	}

	for (int i = 0; i < (int) m_vpBotPlayers.size(); i++)
	{
		if(m_vpBotPlayers[i]->GameWorld() != m_apPlayers[ClientID]->GameWorld())
			continue;

		if(distance(m_apPlayers[ClientID]->m_ViewPos, m_vpBotPlayers[i]->m_ViewPos) > 6e3)
			continue;
			
		std::pair<float,int> temp;
		temp.first = distance(m_apPlayers[ClientID]->m_ViewPos, m_vpBotPlayers[i]->m_ViewPos);
		temp.second = m_vpBotPlayers[i]->GetCID();

		Dist.push_back(temp);
	}

	for(int i = 1; i < MaxClients - 1; i++)
	{
		if(pMap[i] == -1)
			continue;
		
		bool Found = false;
		int FoundID;

		for(FoundID = 0; FoundID < minimum((int) Dist.size(), MaxClients - 1); FoundID ++)
		{
			if(Dist[FoundID].second == pMap[i])
			{
				Found = true;
				break;
			}
		}
		
		if(Found)
		{
			Dist.erase(Dist.begin() + FoundID);
		}
		else
		{
			pMap[i] = -1;
		}
	}

	int Index = 0;
	for(int i = 1; i < MaxClients - 1; i++)
	{
		if(Index >= (int) Dist.size())
			break;

		if(pMap[i] == -1 || !GetPlayer(pMap[i]))
		{
			pMap[i] = Dist[Index].second;
			Index ++;
		}
	}

	pMap[MaxClients - 1] = -1;
	pMap[0] = ClientID;
}

static std::mutex s_RegisterMutex;
void CGameContext::Register(const char* pUsername, const char* pPassHash, int ClientID)
{
	if(!m_apPlayers[ClientID])
	{
		return;
	}

	std::string Username(pUsername);
	std::string PassHash(pPassHash);

	std::thread Thread([this, Username, PassHash, ClientID]()
	{
		s_RegisterMutex.lock();
	
		std::string Buffer;

		Buffer.append("WHERE Username='");
		Buffer.append(Username);
		Buffer.append("';");

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_playerdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			s_RegisterMutex.unlock();
			return;
		}

		if(!pSqlResult->size())
		{
			nlohmann::json Json = {{"Password", PassHash.c_str()}, 
				{"Nickname", Server()->ClientName(ClientID)}};
			
			Buffer.clear();
			Buffer.append("(Username, Data) VALUES ('");
			Buffer.append(Username);
			Buffer.append("', '");
			Buffer.append(Json.dump());
			Buffer.append("');");

			Sql()->Execute<SqlType::INSERT>("lt_playerdata", Buffer.c_str());
			SendChatTarget_Localization(ClientID, _("You are now registered."));
			SendChatTarget_Localization(ClientID, _("Use /login <username> <password> to login"));
		}else
		{
			SendChatTarget_Localization(ClientID, _("User already exists!"));
		}

		s_RegisterMutex.unlock();
	});
	Thread.detach();
	
	return;
}

static std::mutex s_LoginMutex;
void CGameContext::Login(const char* pUsername, const char* pPassHash, int ClientID)
{
	if(!m_apPlayers[ClientID])
	{
		return;
	}

	std::string Username(pUsername);
	std::string PassHash(pPassHash);

	std::thread Thread([this, Username, PassHash, ClientID]()
	{
		s_LoginMutex.lock();

		std::string Buffer;

		Buffer.append("WHERE Username='");
		Buffer.append(Username);
		Buffer.append("';");

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_playerdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			s_LoginMutex.unlock();
			return;
		}

		if(!pSqlResult->size())
		{
			SendChatTarget_Localization(ClientID, _("No such account"));
					
			s_LoginMutex.unlock();
			return;
		}

		for(SqlResult::const_iterator Iter = pSqlResult->begin(); Iter != pSqlResult->end(); ++ Iter)
		{
			nlohmann::json Json = nlohmann::json::parse(Iter["Data"].as<const char*>());

			std::string Password = Json["Password"];
			std::string Nickname = Json["Nickname"];

			if(Password == PassHash)
			{
				if(Nickname == Server()->ClientName(ClientID))
				{
					SendChatTarget_Localization(ClientID, _("You are now logged in."));
					m_apPlayers[ClientID]->Login(Iter["UserID"].as<int>());
					m_apPlayers[ClientID]->m_Datas = Json;

					Datas()->Item()->SyncInvItem(ClientID);
					
					s_LoginMutex.unlock();
					return;
				}else
				{
					SendChatTarget_Localization(ClientID, _("Wrong nickname!"));

					s_LoginMutex.unlock();
					return;
				}
			}
		}

		SendChatTarget_Localization(ClientID, _("Wrong password!"));
		s_LoginMutex.unlock();
	});
	Thread.detach();

	return;
}

static std::mutex s_UpdateMutex;
void CGameContext::UpdatePlayerData(int ClientID)
{
	if(!m_apPlayers[ClientID])
	{
		return;
	}

	if(m_apPlayers[ClientID]->GetUserID() == -1)
	{
		return;
	}

	std::thread Thread([this, ClientID]()
	{
		s_UpdateMutex.lock();

		std::string Buffer;

		int ID = m_apPlayers[ClientID]->GetUserID();

		Buffer.clear();
		Buffer.append("Data = '");
		Buffer.append(m_apPlayers[ClientID]->m_Datas.dump());
		Buffer.append("'");
		Buffer.append(" WHERE UserID = ");
		Buffer.append(std::to_string(ID));
		Buffer.append(";");

		Sql()->Execute<SqlType::UPDATE>("lt_playerdata", Buffer.c_str());

		s_UpdateMutex.unlock();
	});
	Thread.detach();

	return;
}