#ifndef ENGINE_SHARED_NETCLIENT_H
#define ENGINE_SHARED_NETCLIENT_H

// from teeuniverse-old-draft by necropotame

#include <base/system.h>
#include "packer.h"

enum
{
	NET_TOKEN_MAX = 0xffffffff,
	NET_TOKEN_NONE = NET_TOKEN_MAX,
	NET_TOKEN_MASK = NET_TOKEN_MAX,
};

typedef void(*FSendCallback)(int TrackID, void *pUser);
struct CSendCBData
{
	FSendCallback m_pfnCallback;
	void *m_pCallbackUser;
	int m_TrackID;
};

struct CNetChunk
{
	// -1 means that it's a connless packet
	// 0 on the client means the server
	int m_ClientID;
	NETADDR m_Address; // only used when cid == -1
	int m_Flags;
	int m_DataSize;
	const void *m_pData;
};

class CNetChunkHeader
{
public:
	int m_Flags;
	int m_Size;
	int m_Sequence;

	unsigned char *Pack(unsigned char *pData);
	unsigned char *Unpack(unsigned char *pData);
};

class CNetChunkResend
{
public:
	int m_Flags;
	int m_DataSize;
	unsigned char *m_pData;

	int m_Sequence;
	int64 m_LastSendTime;
	int64 m_FirstSendTime;
};

typedef unsigned int TOKEN;

class CMainNetClient
{
public:
	typedef void (*FProcessPacket)(CNetChunk *pPacket, void *pUser);

	enum
	{
		DST_SERVER07 = 0,
		DST_SERVER06,
		DST_MASTER07,
		DST_MASTER06,
		NUM_DST,
		DST_SERVER,
	};

    class INetClient
    {
    protected:
        CMainNetClient *m_pMainNetClient;
		int m_NetClientID;
	    NETADDR m_ServerAddr;

        int m_Flags;

	public:
		FProcessPacket m_fProcessServerPacket;
		FProcessPacket m_fProcessConnlessPacket;

    public:
        virtual void InitMainClient(CMainNetClient* pMainNetClient, int NetClientID)
        {
            m_pMainNetClient = pMainNetClient;
            m_NetClientID = m_NetClientID;
        }
        // openness
        virtual bool Open(class CConfig *pConfig, NETADDR BindAddr, class IConsole *pConsole, class IEngine *pEngine, int Flags) = 0;
        virtual void InitProcessPacket(FProcessPacket fProcessServerPacket, FProcessPacket fProcessConnlessPacket)
        {
            m_fProcessServerPacket = fProcessServerPacket;
            m_fProcessConnlessPacket = fProcessConnlessPacket;
        }
        // connection state
        virtual int Disconnect(const char *Reason) = 0;
        virtual int Connect(NETADDR *Addr) = 0;

        // communication
        virtual int RecvLoop() = 0;
        virtual int Send(CNetChunk *pChunk, TOKEN Token = NET_TOKEN_NONE, CSendCBData *pCallbackData = 0) = 0;
        virtual void PurgeStoredPacket(int TrackID) = 0;

		virtual int Update() = 0;
		virtual bool GotProblems() const = 0;
		virtual int State() const = 0;
		virtual int NetType() const = 0;
		virtual const char* ErrorString() const = 0;
		virtual int ResetErrorString() = 0;
    };
public:
	INetClient* m_apNetClient[NUM_DST];
	
private:
	class IMasterServer *m_pMasterServer;
	class IConsole *m_pConsole;
    class IEngine *m_pEngine;
    class CConfig *m_pConfig;

public:
	void* m_pData;
	int m_DstServerID;

public:
	CMainNetClient(class CConfig *pConfig, class IEngine *pEngine);
	~CMainNetClient();
	
	void Init(IMasterServer *pMasterServer, IConsole *pConsole);
	void SetCallbacks(void* pData);
	
	bool OpenNetClient(int Dst, INetClient* pNetClient, NETADDR BindAddr, int Flags);
	bool Connect(int Dst, NETADDR *pAddr);
	bool Disconnect(int Dst, const char* pReason);
	bool Update();
	bool RecvLoop();
	bool Send(int Dst, CNetChunk *pChunk, TOKEN Token = NET_TOKEN_NONE, CSendCBData *pCallbackData = 0);
	bool GotProblems(int Dst) const;
	int State(int Dst) const;
	int NetType(int Dst) const;
	const char* ErrorString(int Dst) const;
	void ResetErrorString(int Dst);
	
	class IMasterServer* MasterServer() { return m_pMasterServer; }
	class IConsole* Console() { return m_pConsole; }
};

#endif