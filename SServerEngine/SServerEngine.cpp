#include "SServerEngine.h"
#include <event2\thread.h>
//////////////////////////////////////////////////////////////////////////
SServerEngine::SServerEngine()
{
	m_pBvEvent = NULL;
	m_pEventBase = NULL;
	m_pConnListener = NULL;
	m_uPort = 0;
	memset(&m_stThreadId, 0, sizeof(m_stThreadId));
	memset(m_arraySocketPair, 0, sizeof(m_arraySocketPair));
	m_uMaxConn = DEF_DEFAULT_MAX_CONN;
	m_pConnArray = NULL;
	pthread_mutex_init(&m_xSendMutex, NULL);
	m_xSendBuffer.AllocBuffer(DEF_DEFAULT_ENGINE_WRITEBUFFERSIZE);
}

SServerEngine::~SServerEngine()
{
	if(NULL != m_pConnArray)
	{
		delete[] m_pConnArray;
		m_pConnArray = NULL;
	}
	pthread_mutex_destroy(&m_xSendMutex);
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

	m_pConnArray = new SServerConn*[m_uMaxConn + 1];
	memset(m_pConnArray, 0, sizeof(SServerConn*) * (m_uMaxConn + 1));

	//	init index manager
	m_xIndexMgr.Init(m_uMaxConn);

	//	init callback functions
	m_pFuncOnConnected = _pDesc->pFuncOnConnected;
	m_pFuncOnDisconnected = _pDesc->pFuncOnDisconncted;
	m_pFuncOnRecv = _pDesc->pFuncOnRecv;

	return kSServerResult_Ok;
}

int SServerEngine::Start()
{
	if(0 == m_uPort ||
		m_xAddr.empty())
	{
		return kSServerResult_InvalidParam;
	}

	//	create worker thread
	int nRet = pthread_create(&m_stThreadId, NULL, &SServerEngine::__threadEntry, this);
	if(0 != nRet)
	{
		return kSServerResult_CreateThreadFailed;
	}

	return kSServerResult_Ok;
}

int SServerEngine::SendPacket(unsigned int _uConnIndex, char* _pData, size_t _uLength)
{
	SServerConn* pConn = GetConn(_uConnIndex);
	if(NULL == pConn)
	{
		return -1;
	}

	LockSendBuffer();

	SServerAction action = {0};
	action.uAction = kSServerAction_Send;
	action.uIndex = (unsigned short)_uConnIndex;
	action.uTag = (unsigned short)_uLength;
	size_t uRet = m_xSendBuffer.Write((char*)&action, sizeof(SServerAction));
	m_xSendBuffer.Write(_pData, _uLength);

	UnlockSendBuffer();

	//	awake and process event
	awake();

	return int(uRet);
}

int SServerEngine::CloseConnection(unsigned int _uConnIndex)
{
	SServerAction action = {0};
	action.uAction = kSServerAction_Close;
	action.uIndex = (unsigned short)_uConnIndex;

	SServerAutoLocker locker(&m_xSendMutex);

	m_xSendBuffer.Write((char*)&action, sizeof(SServerAction));
	awake();

	return 0;
}

void SServerEngine::Callback_OnConnected(unsigned int _uIndex)
{
	if(NULL == m_pFuncOnConnected)
	{
		return;
	}
	m_pFuncOnConnected(_uIndex);
}

void SServerEngine::Callback_OnDisconnected(unsigned int _uIndex)
{
	if(NULL == m_pFuncOnDisconnected)
	{
		return;
	}
	m_pFuncOnDisconnected(_uIndex);
}

void SServerEngine::Callback_OnRecv(unsigned int _uIndex, char* _pData, unsigned int _uLength)
{
	if(NULL == m_pFuncOnRecv)
	{
		return;
	}
	m_pFuncOnRecv(_uIndex, _pData, _uLength);
}

void SServerEngine::onConnectionClosed(SServerConn* pConn)
{
	LOGPRINT("Con %d closed", pConn->uConnIndex);

	//	callback
	Callback_OnDisconnected(pConn->uConnIndex);

	//	free index
	SetConn(pConn->uConnIndex, NULL);
	m_xIndexMgr.Push(pConn->uConnIndex);

	//	free bufferevent
	bufferevent_free(pConn->pEv);
	pConn->pEv = NULL;
	delete pConn;
	pConn = NULL;
}

void SServerEngine::processConnEvent()
{
	SServerAction action = {0};

	SServerAutoLocker locker(&m_xSendMutex);

	while(0 != m_xSendBuffer.GetReadableSize())
	{
		if(sizeof(SServerAction) != m_xSendBuffer.Read((char*)&action, sizeof(SServerAction)))
		{
			LOGERROR("Process conn event failed.");
			m_xSendBuffer.Reset();
			return;
		}

		if(kSServerAction_Send == action.uAction)
		{
			SServerConn* pConn = GetConn((unsigned int)action.uIndex);
			if(NULL == pConn)
			{
				LOGERROR("Failed to get conn, index:%d", action.uIndex);
			}
			else
			{
				//	write head
				unsigned int uNetLength = DEF_NETPROTOCOL_HEADER_LENGTH + action.uTag;
				uNetLength = htonl(uNetLength);
				bufferevent_write(pConn->pEv, &uNetLength, DEF_NETPROTOCOL_HEADER_LENGTH);
				bufferevent_write(pConn->pEv, m_xSendBuffer.GetReadableBufferPtr(), action.uTag);
			}
			m_xSendBuffer.Read(NULL, action.uTag);
		}
		else if(kSServerAction_Close == action.uAction)
		{
			SServerConn* pConn = GetConn((unsigned int)action.uIndex);
			if(NULL == pConn)
			{
				LOGERROR("Failed to close conn, index:%d", action.uIndex);
			}
			else
			{
				onConnectionClosed(pConn);
			}
		}
		else
		{
			LOGERROR("Invalid conn event %d", action.uAction);
			m_xSendBuffer.Reset();
		}
	}

	//	reset the buffer
	m_xSendBuffer.Reset();
}

void SServerEngine::awake()
{
	int nRet = send(m_arraySocketPair[1], "1", 1, 0);
	if (nRet < 1)
	{
		LOGERROR("Write notify fail, err(%d): %s", errno, strerror(errno));
	}
}

//////////////////////////////////////////////////////////////////////////
//	static handlers
void* SServerEngine::__threadEntry(void* _pArg)
{
	SServerEngine* pIns = (SServerEngine*)_pArg;

	//	initialize
	//evthread_use_windows_threads();
	pIns->m_pEventBase = event_base_new();

	//	create socket pair
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pIns->m_arraySocketPair))
	{
		LOGERROR("evutil_socketpair fail");
		return (void*)-1;
	}

	evutil_make_socket_nonblocking(pIns->m_arraySocketPair[0]);
	evutil_make_socket_nonblocking(pIns->m_arraySocketPair[1]);
	pIns->m_pBvEvent = bufferevent_socket_new(pIns->m_pEventBase, pIns->m_arraySocketPair[0], 0);
	if (NULL == pIns->m_pBvEvent)
	{
		LOGERROR("Create bufferevent fail");
		return (void*)-1;
	}

	// set callback
	bufferevent_setcb(pIns->m_pBvEvent, &SServerEngine::__onThreadRead, NULL, NULL, pIns);
	bufferevent_setwatermark(pIns->m_pBvEvent, EV_READ, 1, 0);
	bufferevent_enable(pIns->m_pBvEvent, EV_READ);

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
		BEV_OPT_CLOSE_ON_FREE
		//| BEV_OPT_THREADSAFE
		);
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

	//	callback
	pIns->Callback_OnConnected(uConnIndex);
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
		pConn->pEng->onConnectionClosed(pConn);
		return;
	}

	//	read head or body
	if(0 == pConn->m_uPacketHeadLength)
	{
		pConn->readHead();
	}
	else
	{
		pConn->readBody();
	}
}

void SServerEngine::__onConnEvent(struct bufferevent* pEv, short what, void* pCtx)
{
	SServerConn* pConn = (SServerConn*)pCtx;
	SServerEngine* pEng = pConn->pEng;

	//	process event
	if(what & BEV_EVENT_CONNECTED)
	{
		return;
	}
	else if(what & BEV_EVENT_EOF)
	{
		LOGPRINT("Conn %d closed", pConn->uConnIndex);
	}
	else
	{
		LOGPRINT("Conn %d error:%d", pConn->uConnIndex, what);
	}

	pEng->onConnectionClosed(pConn);
}

void SServerEngine::__onThreadRead(struct bufferevent* pEv, void* pCtx)
{
	SServerEngine* pEng = (SServerEngine*)pCtx;

	struct evbuffer *pInput = bufferevent_get_input(pEv);
	evbuffer_drain(pInput, evbuffer_get_length(pInput));

	pEng->processConnEvent();
}