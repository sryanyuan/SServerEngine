#include "SServerEngine.h"
//////////////////////////////////////////////////////////////////////////
SServerEngine::SServerEngine()
{
	m_pEventBase = NULL;
	m_pConnListener = NULL;
	m_uPort = 0;
	memset(&m_stThreadId, 0, sizeof(m_stThreadId));
	memset(m_arraySocketPair, 0, sizeof(m_arraySocketPair));
	m_uMaxConn = DEF_DEFAULT_MAX_CONN;
	m_pConnArray = NULL;
}

SServerEngine::~SServerEngine()
{
	if(NULL != m_pConnArray)
	{
		delete[] m_pConnArray;
		m_pConnArray = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////
//
int SServerEngine::Init(const SServerInitDesc* _pDesc)
{
	m_xAddr = _pDesc->xAddr;
	m_uPort = _pDesc->uPort;
	m_uMaxConn = _pDesc->uMaxConn;
	if(0 == m_uMaxConn)
	{
		m_uMaxConn = DEF_DEFAULT_MAX_CONN;
	}

	m_pConnArray = new SServerConn*[m_uMaxConn];
	memset(m_pConnArray, 0, sizeof(SServerConn*) * m_uMaxConn);

	return kSServerResult_Ok;
}

int SServerEngine::Start()
{
	if(0 == m_uPort ||
		m_xAddr.empty())
	{
		return kSServerResult_InvalidParam;
	}

	//	init index manager
	m_xIndexMgr.Init(m_uMaxConn);

	//	create worker thread
	int nRet = pthread_create(&m_stThreadId, NULL, &SServerEngine::__threadEntry, this);
	if(0 != nRet)
	{
		return kSServerResult_CreateThreadFailed;
	}

	return kSServerResult_Ok;
}

void SServerEngine::onConnectionClosed(SServerConn* pConn)
{
	LOGPRINT("Con %d closed", pConn->uConnIndex);

	//	free index
	m_xIndexMgr.Push(pConn->uConnIndex);
	SetConn(pConn->uConnIndex, NULL);

	//	free bufferevent
	bufferevent_free(pConn->pEv);
	pConn->pEv = NULL;
	delete pConn;
	pConn = NULL;
}

//////////////////////////////////////////////////////////////////////////
//	static handlers
void* SServerEngine::__threadEntry(void* _pArg)
{
	SServerEngine* pIns = (SServerEngine*)_pArg;

	//	initialize
	pIns->m_pEventBase = event_base_new();

	struct sockaddr_in sin = {0};
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(pIns->m_xAddr.c_str());
	sin.sin_port = htons(pIns->m_uPort);

	//	listen
	pIns->m_pConnListener = evconnlistener_new_bind(pIns->m_pEventBase,
		&SServerEngine::__onAcceptConn,
		pIns,
		LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
		-1,
		(sockaddr*)&sin,
		sizeof(sin));
	if(NULL == pIns->m_pConnListener)
	{
		LOGPRINT("Failed to listen on host:%s port:%d", pIns->m_xAddr.c_str(), pIns->m_uPort);
		exit(kSServerResult_ListenFailed);
	}

	LOGPRINT("Thread working...");

	//	event loop
	event_base_dispatch(pIns->m_pEventBase);
	event_base_free(pIns->m_pEventBase);
	pIns->m_pEventBase = NULL;

	return NULL;
}

void SServerEngine::__onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr)
{
	SServerEngine* pIns = (SServerEngine*)ptr;
	event_base* pEventBase = evconnlistener_get_base(pEvListener);

	//	get new conn index
	unsigned int uConnIndex = pIns->m_xIndexMgr.Pop();
	if(0 == uConnIndex ||
		IndexManager::s_uInvalidIndex == uConnIndex)
	{
		LOGPRINT("Reach max connection, close new connection.");
		evutil_closesocket(sock);
		return;
	}

#ifdef _DEBUG
	LOGPRINT("Accept conn[%d]", uConnIndex);
#endif

	//	register event
	bufferevent* pEv = bufferevent_socket_new(pEventBase,
		sock,
		BEV_OPT_CLOSE_ON_FREE);
	if(NULL == pEv)
	{
		LOGPRINT("Failed to bind bufferevent");
		evutil_closesocket(sock);
		return;
	}

	//	create new conn context
	SServerConn* pConn = new SServerConn;
	pConn->pEng = pIns;
	pConn->fd = sock;
	pConn->pEv = pEv;
	pConn->uConnIndex = uConnIndex;

	if(pConn->uConnIndex == IndexManager::s_uInvalidIndex)
	{
		LOGPRINT("Reach max fd.");
		evutil_closesocket(sock);
		bufferevent_free(pEv);
		pEv = NULL;
		return;
	}

	bufferevent_setcb(pEv,
		&SServerEngine::__onConnRead,
		NULL,
		&SServerEngine::__onConnEvent,
		pConn);
	bufferevent_setwatermark(pEv, EV_READ, DEF_NETPROTOCOL_HEADER_LENGTH, 0);
	bufferevent_setwatermark(pEv, EV_WRITE, 0, 0);
	bufferevent_enable(pEv, EV_READ);
	pIns->SetConn(uConnIndex, pConn);
}

void SServerEngine::__onAcceptErr(struct evconnlistener *pEvListener, void *ptr)
{
	LOGPRINT("Accept error");
}

void SServerEngine::__onConnRead(struct bufferevent* pEv, void* pCtx)
{
	SServerConn* pConn = (SServerConn*)pCtx;
	evbuffer* pInput = bufferevent_get_input(pEv);
	size_t uRead = evbuffer_get_length(pInput);

	if(0 == uRead)
	{
		//	connection closed
		LOGPRINT("Conn %d closed", pConn->uConnIndex);
	}

	if(1 == pConn->uConnIndex)
	{
		pConn->pEng->onConnectionClosed(pConn);
	}
}

void SServerEngine::__onConnEvent(struct bufferevent* pEv, short what, void* pCtx)
{
	SServerConn* pConn = (SServerConn*)pCtx;
	SServerEngine* pEng = pConn->pEng;

	//	process event
	if(what & BEV_EVENT_EOF)
	{
		LOGPRINT("Conn %d closed", pConn->uConnIndex);
	}
	else
	{
		LOGPRINT("Conn %d error:%d", pConn->uConnIndex, what);
	}

	pEng->onConnectionClosed(pConn);
}