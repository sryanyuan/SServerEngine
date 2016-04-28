#include "SServerEngine.h"
//////////////////////////////////////////////////////////////////////////
SServerEngine::SServerEngine()
{
	m_pEventBase = NULL;
	m_uPort = 0;
}

SServerEngine::~SServerEngine()
{

}

//////////////////////////////////////////////////////////////////////////
//
int SServerEngine::Start()
{
	return kSServerResult_Ok;
}

//////////////////////////////////////////////////////////////////////////
//	static handlers
void* SServerEngine::__threadEntry(void*)
{
	return NULL;
}

void SServerEngine::__onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr)
{

}

void SServerEngine::__onAcceptErr(struct evconnlistener *pEvListener, void *ptr)
{

}