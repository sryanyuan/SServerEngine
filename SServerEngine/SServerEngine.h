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
#include "Logger.h"
#include "SServerConn.h"
#include "Def.h"
//////////////////////////////////////////////////////////////////////////
#ifdef WIN32

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "lib/pthread/pthreadVC2.lib")

#if _MSC_VER == 1700
#ifdef _DEBUG
#pragma comment(lib, "lib/libevent/libevent_vc11_d.lib")
#pragma comment(lib, "lib/libevent/libevent_core_vc11_d.lib")
#pragma comment(lib, "lib/libevent/libevent_extras_vc11_d.lib")
#else
#pragma comment(lib, "lib/libevent/libevent_vc11.lib")
#pragma comment(lib, "lib/libevent/libevent_core_vc11.lib")
#pragma comment(lib, "lib/libevent/libevent_extras_vc11.lib")
#endif
#elif _MSC_VER == 1500
#ifdef _DEBUG
#pragma comment(lib, "lib/libevent/libevent_vc9_d.lib")
#pragma comment(lib, "lib/libevent/libevent_core_vc9_d.lib")
#pragma comment(lib, "lib/libevent/libevent_extras_vc9_d.lib")
#else
#pragma comment(lib, "lib/libevent/libevent_vc9.lib")
#pragma comment(lib, "lib/libevent/libevent_core_vc9.lib")
#pragma comment(lib, "lib/libevent/libevent_extras_vc9.lib")
#endif
#else
#error VS version not support
#endif

#endif
//////////////////////////////////////////////////////////////////////////
typedef void (*FUNC_ONCONNECTED)(unsigned int);
typedef void (*FUNC_ONDISCONNECTED)(unsigned int);
typedef void (*FUNC_ONRECV)(unsigned int, char*, unsigned int);
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
	kSServerAction_Close,
	kSServerAction_Send,
};

struct SServerAction
{
	unsigned short uAction;
	unsigned short uIndex;
	unsigned short uTag;
};

struct SServerInitDesc
{
	std::string xAddr;
	unsigned short uPort; 
	size_t uMaxConn;

	//	callbacks
	FUNC_ONCONNECTED pFuncOnConnected;
	FUNC_ONDISCONNECTED pFuncOnDisconncted;
	FUNC_ONRECV pFuncOnRecv;
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
	int Start();

	//	thread-safe
	int SendPacket(unsigned int _uConnIndex, char* _pData, size_t _uLength);
	int CloseConnection(unsigned int _uConnIndex);

public:
	inline unsigned int GetMaxConn()						{return m_uMaxConn;}
	inline void SetMaxConn(unsigned int _uConn)				{m_uMaxConn = _uConn;}

	inline SServerConn* GetConn(unsigned int _uConnIndex)
	{
		if(_uConnIndex > m_uMaxConn ||
			0 == _uConnIndex)
		{
			LOGERROR("Invalid conn index %d", _uConnIndex);
			return NULL;
		}
		return m_pConnArray[_uConnIndex];
	}
	inline void SetConn(unsigned int _uConnIndex, SServerConn* conn)
	{
		if(_uConnIndex > m_uMaxConn ||
			0 == _uConnIndex)
		{
			LOGERROR("Invalid conn index %d", _uConnIndex);
			return;
		}
		m_pConnArray[_uConnIndex] = conn;
	}

	inline void LockSendBuffer()
	{
		pthread_mutex_lock(&m_xSendMutex);
	}
	inline void UnlockSendBuffer()
	{
		pthread_mutex_unlock(&m_xSendMutex);
	}

	void Callback_OnConnected(unsigned int _uIndex);
	void Callback_OnDisconnected(unsigned int _uIndex);
	void Callback_OnRecv(unsigned int _uIndex, char* _pData, unsigned int _uLength);

protected:
	void onConnectionClosed(SServerConn* pConn);
	void processConnEvent();
	void awake();

public:
	static void* PTW32_CDECL __threadEntry(void*);
	static void __onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr);
	static void __onAcceptErr(struct evconnlistener *pEvListener, void *ptr);
	static void __onConnErr();

	static void __onConnRead(struct bufferevent* pEv, void* pCtx);
	static void __onConnEvent(struct bufferevent* pEv, short what, void* pCtx);

	static void __onThreadRead(struct bufferevent* pEv, void* pCtx);

private:
	pthread_t m_stThreadId;
	event_base* m_pEventBase;
	evconnlistener* m_pConnListener;

	std::string m_xAddr;
	unsigned short m_uPort;
	size_t m_uMaxConn;

	IndexManager m_xIndexMgr;
	SServerConn** m_pConnArray;

	evutil_socket_t m_arraySocketPair[2];
	bufferevent* m_pBvEvent;

	pthread_mutex_t m_xSendMutex;
	SServerBuffer m_xSendBuffer;

	//	callbacks
	FUNC_ONCONNECTED m_pFuncOnConnected;
	FUNC_ONDISCONNECTED m_pFuncOnDisconnected;
	FUNC_ONRECV m_pFuncOnRecv;
};
//////////////////////////////////////////////////////////////////////////
#endif