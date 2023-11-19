
#include <mastersrv/mastersrv.h>
#include "netclient.h"

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/masterserver.h>

#include <engine/shared/config.h>

CMainNetClient::CMainNetClient() :
	m_pData(0),
	m_DstServerID(-1)
{
	for(auto &NetClient : m_apNetClient)
		NetClient = nullptr;
}

CMainNetClient::~CMainNetClient()
{
	for(int i=0; i<NUM_DST; i++)
	{
		if(m_apNetClient[i])
			delete m_apNetClient[i];
	}
}

void CMainNetClient::Init(class CConfig *pConfig, class IEngine *pEngine, IMasterServer *pMasterServer, IConsole *pConsole)
{
	m_pConfig = pConfig;
	m_pEngine = pEngine;
	m_pMasterServer = pMasterServer;
	m_pConsole = pConsole;
}

void CMainNetClient::SetCallbacks(void* pData)
{
	m_pData = pData;
}

bool CMainNetClient::OpenNetClient(int Dst, INetClient* pNetClient, NETADDR BindAddr, int Flags)
{
	if(Dst >= NUM_DST || Dst < 0)
		return false;
	
	bool res = pNetClient->Open(m_pConfig, BindAddr, m_pConsole, m_pEngine, Flags);
	if(res)
	{
		if(m_apNetClient[Dst])
			delete m_apNetClient[Dst];
		
		m_apNetClient[Dst] = pNetClient;
	}
	
	return res;
}

bool CMainNetClient::Connect(int Dst, NETADDR *pAddr)
{
	if(m_apNetClient[Dst])
	{
		if(m_apNetClient[Dst]->Connect(pAddr))
		{
			m_DstServerID = Dst;
			return true;
		}
	}
	
	return false;
}

bool CMainNetClient::Disconnect(int Dst, const char* pReason)
{
	if(Dst == DST_SERVER && m_DstServerID == -1)
		return false;

	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->Disconnect(pReason);
	
	return false;
}

bool CMainNetClient::Update()
{
	for(int i = 0; i < NUM_DST; i ++)
	{
		m_apNetClient[i]->Update();
	}
	return true;
}

bool CMainNetClient::RecvLoop()
{
	for(int i = 0; i < NUM_DST; i ++)
	{
		if(m_apNetClient[i])
			m_apNetClient[i]->RecvLoop();
	}
	return true;
}

bool CMainNetClient::Send(int Dst, CNetChunk *pChunk, TOKEN Token, CSendCBData *pCallbackData)
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->Send(pChunk, Token, pCallbackData);
	
	return false;
}

bool CMainNetClient::GotProblems(int Dst) const
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->GotProblems();
	
	return false;
}

int CMainNetClient::State(int Dst) const
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->State();
	
	return 0; // OFFLINE
}

int CMainNetClient::NetType(int Dst) const
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->NetType();
	
	return 0;
}

const char* CMainNetClient::ErrorString(int Dst) const
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		return m_apNetClient[Dst]->ErrorString();
	
	return 0;
}

void CMainNetClient::PurgeStoredPacket(int Dst, int TrackID)
{
	if(m_apNetClient[Dst])
		m_apNetClient[Dst]->PurgeStoredPacket(TrackID);
}

void CMainNetClient::ResetErrorString(int Dst)
{
	if(Dst == DST_SERVER)
		Dst = m_DstServerID;
	
	if(m_apNetClient[Dst])
		m_apNetClient[Dst]->ResetErrorString();
}