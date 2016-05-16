#ifndef _INC_SSERVERENGINE_
#define _INC_SSERVERENGINE_
//////////////////////////////////////////////////////////////////////////
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <pthread/pthread.h>
#include <string>
#include <WinSock2.h>
#include <stdio.h>
#include "IndexManager.h"
#include "SServerConn.h"
#include "Def.h"
//////////////////////////////////////////////////////////////////////////
enum SServerResultType
{
	kSServerResult_Ok	=	0,
	kSServerResult_InvalidParam,
	kSServerResult_CreateThreadFailed,
	kSServerResult_ListenFailed,
};
//////////////////////////////////////////////////////////////////////////
struct SServerEvent
{
	int nEventId;
	void* pData;
	size_t uLength;
};

enum SServerActionType
{
	kSServerAction_CloseUserConn,
	kSServerAction_CloseServerConn,
	kSServerAction_SendToUser,
	kSServerAction_SendToServer,
	kSServerAction_Connect
};

struct SServerAction
{
	unsigned short uAction;
	unsigned short uIndex;
	unsigned short uTag;
};

struct SServerActionConnectContext
{
	sockaddr_in addr;
	FUNC_ONCONNECTSUCCESS fnSuccess;
	FUNC_ONCONNECTFAILED fnFailed;
	void* pArg;
};

struct SServerInitDesc
{ 
	size_t uMaxConnUser;
	size_t uMaxConnServer;
	bool bUseIOCP;

	//	callbacks
	FUNC_ONACCEPT pFuncOnAcceptUser;
	FUNC_ONDISCONNECTED pFuncOnDisconnctedUser;
	FUNC_ONRECV pFuncOnRecvUser;

	FUNC_ONACCEPT pFuncOnAcceptServer;
	FUNC_ONDISCONNECTED pFuncOnDisconnctedServer;
	FUNC_ONRECV pFuncOnRecvServer;
};

struct SServerAutoLocker
{
	SServerAutoLocker(pthread_mutex_t* _pm)
	{
		pMtx = _pm;
		pthread_mutex_lock(pMtx);
	}
	~SServerAutoLocker()
	{
		pthread_mutex_unlock(pMtx);
	}
	pthread_mutex_t* pMtx;
};
//////////////////////////////////////////////////////////////////////////
class SServerEngine
{
public:
	SServerEngine();
	~SServerEngine();

public:
	int Init(const SServerInitDesc* _pDesc);
	int Start(const char* _pszAddr, unsigned short _uPort);
	int Connect(const char* _pszAddr, unsigned short _sPort, FUNC_ONCONNECTSUCCESS _fnSuccess, FUNC_ONCONNECTFAILED _fnFailed, void* _pArg);

	//	thread-safe
	int SendPacketToUser(unsigned int _uConnIndex, char* _pData, size_t _uLength);
	int SendPacketToServer(unsigned int _uConnIndex, char* _pData, size_t _uLength);
	int CloseUserConnection(unsigned int _uConnIndex);
	int CloseServerConnection(unsigned int _uConnIndex);

public:
	inline unsigned int GetMaxConnUser()						{return m_uMaxConnUser;}
	inline void SetMaxConnUser(unsigned int _uConn)				{m_uMaxConnUser = _uConn;}

	inline int GetConnectedServerCount()					{return m_nConnectedServerCount;}
	inline int GetConnectedUserCount()						{return m_nConnectedUserCount;}

	inline SServerConn* GetUserConn(unsigned int _uConnIndex);
	inline void SetUserConn(unsigned int _uConnIndex, SServerConn* conn);

	inline SServerConn* GetServerConn(unsigned int _uConnIndex);
	inline void SetServerConn(unsigned int _uConnIndex, SServerConn* conn);

	inline void LockSendBuffer();
	inline void UnlockSendBuffer();

	void Callback_OnAcceptUser(unsigned int _uIndex);
	void Callback_OnAcceptServer(unsigned int _uIndex);
	void Callback_OnDisconnectedUser(unsigned int _uIndex);
	void Callback_OnDisconnectedServer(unsigned int _uIndex);
	void Callback_OnRecvUser(unsigned int _uIndex, char* _pData, unsigned int _uLength);
	void Callback_OnRecvServer(unsigned int _uIndex,  char* _pData, unsigned int _uLength);

protected:
	void onConnectionClosed(SServerConn* _pConn);
	void onUserConnectionClosed(SServerConn* _pConn);
	void onServerConnectionClosed(SServerConn* _pConn);
	void processConnEvent();
	void awake();

	void processConnectAction(SServerActionConnectContext* _pAction);

public:
	static void* PTW32_CDECL __threadEntry(void*);
	static void __onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr);
	static void __onAcceptErr(struct evconnlistener *pEvListener, void *ptr);
	static void __onConnErr();

	static void __onConnRead(struct bufferevent* pEv, void* pCtx);
	static void __onConnEvent(struct bufferevent* pEv, short what, void* pCtx);

	static void __onThreadRead(struct bufferevent* pEv, void* pCtx);

protected:
	pthread_t m_stThreadId;
	event_base* m_pEventBase;
	evconnlistener* m_pConnListener;
	bool m_bUseIOCP;

	std::string m_xAddr;
	unsigned short m_uPort;
	size_t m_uMaxConnUser;
	size_t m_uMaxConnServer;

	IndexManager m_xUserIndexMgr;
	IndexManager m_xServerIndexMgr;
	SServerConn** m_pUserConnArray;
	SServerConn** m_pServerConnArray;

	evutil_socket_t m_arraySocketPair[2];
	bufferevent* m_pBvEvent;

	pthread_mutex_t m_xSendMutex;
	SServerBuffer m_xEventBuffer;

	//	callbacks
	FUNC_ONACCEPT m_pFuncOnAcceptUser;
	FUNC_ONACCEPT m_pFuncOnAcceptServer;
	FUNC_ONDISCONNECTED m_pFuncOnDisconnectedUser;
	FUNC_ONDISCONNECTED m_pFuncOnDisconnectedServer;
	FUNC_ONRECV m_pFuncOnRecvUser;
	FUNC_ONRECV m_pFuncOnRecvServer;

	//	connected count
	int m_nConnectedUserCount;
	int m_nConnectedServerCount;
};
//////////////////////////////////////////////////////////////////////////
#endif