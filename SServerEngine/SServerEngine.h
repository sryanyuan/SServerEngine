#ifndef _INC_SSERVERENGINE_
#define _INC_SSERVERENGINE_
//////////////////////////////////////////////////////////////////////////
#include <event2/bufferevent.h>
#include <pthread/pthread.h>
#include <string>
//////////////////////////////////////////////////////////////////////////
enum SServerResultType
{
	kSServerResult_Ok	=	0,
};
//////////////////////////////////////////////////////////////////////////
class SServerEngine
{
public:
	SServerEngine();
	~SServerEngine();

public:
	int Start();

public:
	static void* PTW32_CDECL __threadEntry(void*);
	static void __onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr);
	static void __onAcceptErr(struct evconnlistener *pEvListener, void *ptr);

private:
	event_base* m_pEventBase;

	std::string m_xAddr;
	unsigned short m_uPort;
};
//////////////////////////////////////////////////////////////////////////
#endif