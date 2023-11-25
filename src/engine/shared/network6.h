/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_NETWORK6_H
#define ENGINE_SHARED_NETWORK6_H

#include <base/system.h>

#include "ringbuffer.h"
#include "huffman.h"

#include "netclient.h"

namespace network6
{

/*

CURRENT:
	packet header: 7 bytes
		unsigned char flags_ack; // 6bit flags, 2bit ack
		unsigned char ack; // 8 bit ack
		unsigned char num_chunks; // 8 bit chunks
		(unsigned char token[4];) // 32 bit token if flags contains NET_PACKETFLAG_TOKEN

		(unsigned char padding[3];) // 24 bit extra in case it's a connection less packet
		                            // this is to make sure that it's compatible with the
		                            // old protocol

	chunk header: 2-3 bytes
		unsigned char flags_size; // 2bit flags, 6 bit size
		unsigned char size_seq; // 4bit size, 4bit seq
		(unsigned char seq;) // 8bit seq, if vital flag is set
*/

enum
{
	NETFLAG_ALLOWSTATELESS=1,
	NETSENDFLAG_VITAL=1,
	NETSENDFLAG_CONNLESS=2,
	NETSENDFLAG_FLUSH=4,
	NETSENDFLAG_EXTENDED = 8,

	NETSTATE_OFFLINE=0,
	NETSTATE_CONNECTING,
	NETSTATE_ONLINE,

	NETBANTYPE_SOFT=1,
	NETBANTYPE_DROP=2,

	NETCREATE_FLAG_RANDOMPORT=1
};


enum
{
	NET_VERSION = 2,

	NET_MAX_PACKETSIZE = 1400,
	NET_MAX_PAYLOAD = NET_MAX_PACKETSIZE-10,
	NET_MAX_CHUNKHEADERSIZE = 5,
	NET_PACKETHEADERSIZE = 7,
	NET_PACKETHEADERSIZE_WITHOUT_TOKEN = 3,
	NET_MAX_CLIENTS = 16,
	NET_MAX_CONSOLE_CLIENTS = 4,
	NET_MAX_SEQUENCE = 1<<10,
	NET_SEQUENCE_MASK = NET_MAX_SEQUENCE-1,

	NET_CONNSTATE_OFFLINE=0,
	NET_CONNSTATE_CONNECT=1,
	NET_CONNSTATE_ONLINE=3,
	NET_CONNSTATE_ERROR=4,

	NET_PACKETFLAG_UNUSED=1<<0,
	NET_PACKETFLAG_TOKEN=1<<1,
	NET_PACKETFLAG_CONTROL=1<<2,
	NET_PACKETFLAG_CONNLESS=1<<3,
	NET_PACKETFLAG_RESEND=1<<4,
	NET_PACKETFLAG_COMPRESSION=1<<5,

	NET_CHUNKFLAG_VITAL=1,
	NET_CHUNKFLAG_RESEND=2,

	NET_CTRLMSG_KEEPALIVE=0,
	NET_CTRLMSG_CONNECT=1,
	NET_CTRLMSG_CONNECTACCEPT=2,
	NET_CTRLMSG_CLOSE=4,

	NET_CONN_BUFFERSIZE=1024*32,

	NET_COMPATIBILITY_SEQ=2,

	NET_ENUM_TERMINATOR
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

class CNetPacketConstruct
{
public:
	int m_Flags;
	int m_Ack;
	int m_NumChunks;
	int m_DataSize;
	unsigned m_Token;
	unsigned char m_aChunkData[NET_MAX_PAYLOAD];
	unsigned char m_aExtraData[4];
};


class CNetConnection
{
	// TODO: is this needed because this needs to be aware of
	// the ack sequencing number and is also responible for updating
	// that. this should be fixed.
	friend class CNetRecvUnpacker;
private:
	unsigned short m_Sequence;
	bool m_UnknownAck; // ack not known due to the backward compatibility hack
	unsigned short m_Ack;
	unsigned short m_PeerAck;
	unsigned m_State;

	bool m_UseToken;
	unsigned m_Token;
	int m_RemoteClosed;
	bool m_BlockCloseMsg;

	TStaticRingBuffer<CNetChunkResend, NET_CONN_BUFFERSIZE> m_Buffer;

	int64 m_LastUpdateTime;
	int64 m_LastRecvTime;
	int64 m_LastSendTime;

	char m_ErrorString[256];

	CNetPacketConstruct m_Construct;

	NETADDR m_PeerAddr;
	NETSOCKET m_Socket;
	NETSTATS m_Stats;

	//
	void Reset();
	void ResetStats();
	void SetError(const char *pString);
	void AckChunks(int Ack);

	int QueueChunkEx(int Flags, int DataSize, const void *pData, int Sequence);
	void SendControl(int ControlMsg, const void *pExtra, int ExtraSize);
	void ResendChunk(CNetChunkResend *pResend);
	void Resend();
	void SendConnect();

public:
	void Init(NETSOCKET Socket, bool BlockCloseMsg);
	int Connect(NETADDR *pAddr);
	int Accept(NETADDR *pAddr, unsigned Token);
	int AcceptLegacy(NETADDR *pAddr);
	void Disconnect(const char *pReason);

	int Update();
	int Flush();

	int Feed(CNetPacketConstruct *pPacket, NETADDR *pAddr);
	int QueueChunk(int Flags, int DataSize, const void *pData);

	const char *ErrorString();
	void SignalResend();
	int State() const { return m_State; }
	const NETADDR *PeerAddress() const { return &m_PeerAddr; }

	void ResetErrorString() { m_ErrorString[0] = 0; }
	const char *ErrorString() const { return m_ErrorString; }

	// Needed for GotProblems in NetClient
	int64 LastRecvTime() const { return m_LastRecvTime; }
	int64 ConnectTime() const { return m_LastUpdateTime; }

	int AckSequence() const { return m_Ack; }
};

class CNetRecvUnpacker
{
public:
	bool m_Valid;

	NETADDR m_Addr;
	CNetConnection *m_pConnection;
	int m_CurrentChunk;
	int m_ClientID;
	CNetPacketConstruct m_Data;
	unsigned char m_aBuffer[NET_MAX_PACKETSIZE];

	CNetRecvUnpacker() { Clear(); }
	void Clear();
	void Start(const NETADDR *pAddr, CNetConnection *pConnection, int ClientID);
	int FetchChunk(CNetChunk *pChunk);
};

// client side
class CNetClient : public INetClient
{
	NETADDR m_ServerAddr;
	CNetConnection m_Connection;
	CNetRecvUnpacker m_RecvUnpacker;
	NETSOCKET m_Socket;

	class CConfig *m_pConfig;
public:
	// openness
	bool Open(class CConfig *pConfig, NETADDR BindAddr, class IConsole *pConsole, class IEngine *pEngine, int Flags) override;
	int Close();

	// connection state
	int Disconnect(const char *Reason);
	int Connect(NETADDR *Addr);

	// communication
	int Recv(CNetChunk *Chunk);
	int RecvLoop() override;
	int Send(CNetChunk *pChunk, TOKEN Token = NET_TOKEN_NONE, CSendCBData *pCallbackData = 0);

	void PurgeStoredPacket(int TrackID) override {};

	// pumping
	int Update();
	int Flush();

	int ResetErrorString();

	// error and state
	int NetType() override { return m_Socket.type; }
	int State() const override;
	bool GotProblems() const override;
	const char *ErrorString() const;
};

// TODO: both, fix these. This feels like a junk class for stuff that doesn't fit anywere
class CNetBase
{
	static IOHANDLE ms_DataLogSent;
	static IOHANDLE ms_DataLogRecv;
	static CHuffman ms_Huffman;
public:
	static void OpenLog(IOHANDLE DataLogSent, IOHANDLE DataLogRecv);
	static void CloseLog();
	static void Init();
	static int Compress(const void *pData, int DataSize, void *pOutput, int OutputSize);
	static int Decompress(const void *pData, int DataSize, void *pOutput, int OutputSize);

	static void SendControlMsg(NETSOCKET Socket, NETADDR *pAddr, int Ack, int ControlMsg, const void *pExtra, int ExtraSize, TOKEN Token = NET_TOKEN_NONE);
	static void SendPacketConnless(NETSOCKET Socket, NETADDR *pAddr, const void *pData, int DataSize, bool Extended, unsigned char aExtra[4]);
	static void SendPacket(NETSOCKET Socket, NETADDR *pAddr, CNetPacketConstruct *pPacket);
	static int UnpackPacket(unsigned char *pBuffer, int Size, CNetPacketConstruct *pPacket);

	// The backroom is ack-NET_MAX_SEQUENCE/2. Used for knowing if we acked a packet or not
	static int IsSeqInBackroom(int Seq, int Ack);
};

}

#endif
