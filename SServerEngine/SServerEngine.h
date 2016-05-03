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
#define DEF_DEFAULT_MAX_CONN		100
#define DEF_NETPROTOCOL_HEADER_LENGTH	4
//////////////////////////////////////////////////////////////////////////
enum SServerResultType
{
	kSServerResult_Ok	=	0,
	kSServerResult_InvalidParam,
	kSServerResult_CreateThreadFailed,
	kSServerResult_ListenFailed,
};
//////////////////////////////////////////////////////////////////////////
class SServerEngine;
struct SServerEvent
{
	int nEventId;
	void* pData;
	size_t uLength;
};

struct SServerConn
{
	SServerEngine* pEng;
	bufferevent* pEv;
	unsigned int uConnIndex;
	evutil_socket_t fd;
};

struct SServerInitDesc
{
	std::string xAddr;
	unsigned short uPort; 
	size_t uMaxConn;
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

	int SendPacket(unsigned int _uConnIndex, char* _pData, size_t _uLength);
	int CloseConnection(unsigned int _uConnIndex);

public:
	inline unsigned int GetMaxConn()						{return m_uMaxConn;}
	inline void SetMaxConn(unsigned int _uConn)				{m_uMaxConn = _uConn;}

	inline SServerConn* GetConn(unsigned int _uConnIndex)
	{
		if(_uConnIndex >= m_uMaxConn)
		{
			return NULL;
		}
		return m_pConnArray[_uConnIndex];
	}
	inline void SetConn(unsigned int _uConnIndex, SServerConn* conn)
	{
		if(_uConnIndex >= m_uMaxConn)
		{
			return;
		}
		m_pConnArray[_uConnIndex] = conn;
	}

protected:
	void onConnectionClosed(SServerConn* pConn);

public:
	static void* PTW32_CDECL __threadEntry(void*);
	static void __onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr);
	static void __onAcceptErr(struct evconnlistener *pEvListener, void *ptr);
	static void __onConnErr();

	static void __onConnRead(struct bufferevent* pEv, void* pCtx);
	static void __onConnEvent(struct bufferevent* pEv, short what, void* pCtx);

private:
	pthread_t m_stThreadId;
	event_base* m_pEventBase;
	evconnlistener* m_pConnListener;

	std::string m_xAddr;
	unsigned short m_uPort;
	size_t m_uMaxConn;

	IndexManager m_xIndexMgr;
	SServerConn** m_pConnArray;
};
//////////////////////////////////////////////////////////////////////////
#endif