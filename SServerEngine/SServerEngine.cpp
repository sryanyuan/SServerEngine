#include "SServerEngine.h"
#include <event2\thread.h>
#include <assert.h>
#include "Logger.h"
//////////////////////////////////////////////////////////////////////////
#ifdef WIN32

#define LIBPATH_PTHREAD "lib/pthread/"
#define LIBPATH_LIBEVENT "lib/libevent/"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, LIBPATH_PTHREAD"pthreadVC2.lib")

#if _MSC_VER == 1700
#ifdef _DEBUG
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_vc11_d.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_core_vc11_d.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_extras_vc11_d.lib")
#else
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_vc11.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_core_vc11.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_extras_vc11.lib")
#endif
#elif _MSC_VER == 1500
#ifdef _DEBUG
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_vc9_d.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_core_vc9_d.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_extras_vc9_d.lib")
#else
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_vc9.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_core_vc9.lib")
#pragma comment(lib, LIBPATH_LIBEVENT"libevent_extras_vc9.lib")
#endif
#else
#error VS version not support
#endif

#endif
//////////////////////////////////////////////////////////////////////////
SServerEngine::SServerEngine()
{
	m_eStatus = kSServerStatus_Stop;
	m_nWorkingTid = 0;

	m_pBvEvent = NULL;
	m_pEventBase = NULL;
	m_pConnListener = NULL;
	m_uPort = 0;
	memset(&m_stThreadId, 0, sizeof(m_stThreadId));
	memset(m_arraySocketPair, 0, sizeof(m_arraySocketPair));
	m_pUserConnArray = NULL;
	pthread_mutex_init(&m_xSendMutex, NULL);
	pthread_mutex_init(&m_xTimerMutex, NULL);
	m_xEventBuffer.AllocBuffer(DEF_DEFAULT_ENGINE_WRITEBUFFERSIZE);

	m_pFuncOnAcceptUser = NULL;
	m_pFuncOnDisconnectedUser = NULL;
	m_pFuncOnRecvUser = NULL;

	m_pFuncOnAcceptServer = NULL;
	m_pFuncOnDisconnectedServer = NULL;
	m_pFuncOnRecvServer = NULL;

	m_nConnectedServerCount = m_nConnectedUserCount = 0;

	m_pTimerEvent = NULL;

	// options
	m_bUseIOCP = false;
	m_uMaxConnUser = DEF_DEFAULT_MAX_CONN;
	m_uMaxConnServer = DEF_DEFAULT_MAX_CONN;
	m_uMaxPacketLength = DEF_DEFAULT_MAX_PACKET_LENGTH;
}

SServerEngine::~SServerEngine()
{
	if (NULL != m_pUserConnArray) {
		delete[] m_pUserConnArray;
		m_pUserConnArray = NULL;
	}
	pthread_mutex_destroy(&m_xSendMutex);
	pthread_mutex_destroy(&m_xTimerMutex);
}

//////////////////////////////////////////////////////////////////////////
//
int SServerEngine::Init(const SServerInitDesc* _pDesc)
{
	m_uMaxConnUser = _pDesc->uMaxConnUser;
	if (0 == m_uMaxConnUser) {
		m_uMaxConnUser = DEF_DEFAULT_MAX_CONN;
	}
	if (0 == m_uMaxConnServer) {
		m_uMaxConnServer = DEF_DEFAULT_MAX_CONN;
	}

	LOGINFO("Create with %d,%d", m_uMaxConnUser, m_uMaxConnServer);
	m_pUserConnArray = new SServerConn*[m_uMaxConnUser + 1];
	LOGINFO("Create with %d,%d done", m_uMaxConnUser, m_uMaxConnServer);
	memset(m_pUserConnArray, 0, sizeof(SServerConn*) * (m_uMaxConnUser + 1));
	m_pServerConnArray = new SServerConn*[m_uMaxConnServer + 1];
	memset(m_pServerConnArray, 0, sizeof(SServerConn*) * (m_uMaxConnServer + 1));

	//	init index manager
	m_xUserIndexMgr.Init(m_uMaxConnUser);
	m_xServerIndexMgr.Init(m_uMaxConnServer);

	//	init callback functions
	m_pFuncOnAcceptUser = _pDesc->pFuncOnAcceptUser;
	m_pFuncOnDisconnectedUser = _pDesc->pFuncOnDisconnctedUser;
	m_pFuncOnRecvUser = _pDesc->pFuncOnRecvUser;

	m_pFuncOnAcceptServer = _pDesc->pFuncOnAcceptServer;
	m_pFuncOnDisconnectedServer = _pDesc->pFuncOnDisconnctedServer;
	m_pFuncOnRecvServer = _pDesc->pFuncOnRecvServer;

	//	use iocp
	m_bUseIOCP = _pDesc->bUseIOCP;

	// limit max packet length
	m_uMaxPacketLength = (size_t)_pDesc->uMaxPacketLength;

	return kSServerResult_Ok;
}

int SServerEngine::Start(const char* _pszAddr, unsigned short _uPort)
{
	m_uPort = _uPort;
	m_xAddr = _pszAddr;

	//	create worker thread
	int nRet = pthread_create(&m_stThreadId, NULL, &SServerEngine::__threadEntry, this);
	if (0 != nRet) {
		return kSServerResult_CreateThreadFailed;
	}

	return kSServerResult_Ok;
}

int SServerEngine::Stop()
{
	if(m_eStatus != kSServerStatus_Running)
	{
		return 1;
	}

#ifdef _DEBUG
	int nTid = (int)GetCurrentThreadId();

	if (nTid != m_nWorkingTid)
	{
		assert("Tid conflict");
	}
#endif

	if (NULL != m_pEventBase)
	{
		return event_base_loopbreak(m_pEventBase);
	}

	return 2;
}

int SServerEngine::Connect(const char* _pszAddr, unsigned short _sPort, FUNC_ONCONNECTSUCCESS _fnSuccess, FUNC_ONCONNECTFAILED _fnFailed, void* _pArg)
{
	SServerAction action = {0};
	SServerActionConnectContext ctx = {0};
	action.uAction = kSServerAction_Connect;
	ctx.addr.sin_family = AF_INET;
	ctx.addr.sin_port = htons(_sPort);
	ctx.addr.sin_addr.s_addr = inet_addr(_pszAddr);
	ctx.fnSuccess =_fnSuccess;
	ctx.fnFailed = _fnFailed;

	LockSendBuffer();

	m_xEventBuffer.Write((char*)&action, sizeof(SServerAction));
	m_xEventBuffer.Write((char*)&ctx, sizeof(SServerActionConnectContext));

	UnlockSendBuffer();

	//	awake and process event
	awake();

	return 0;
}

int SServerEngine::SyncConnect(const char* _pszAddr, unsigned short _sPort, FUNC_ONCONNECTSUCCESS _fnSuccess, FUNC_ONCONNECTFAILED _fnFailed, void* _pArg)
{
	SServerAction action = {0};
	SServerActionConnectContext ctx = {0};
	action.uAction = kSServerAction_Connect;
	ctx.addr.sin_family = AF_INET;
	ctx.addr.sin_port = htons(_sPort);
	ctx.addr.sin_addr.s_addr = inet_addr(_pszAddr);
	ctx.fnSuccess =_fnSuccess;
	ctx.fnFailed = _fnFailed;

	processConnectAction(&ctx);
	return 0;
}

int SServerEngine::SendPacketToServer(unsigned int _uConnIndex, char* _pData, size_t _uLength)
{
	SServerConn* pConn = GetServerConn(_uConnIndex);
	if (NULL == pConn) {
		return -1;
	}

	LockSendBuffer();

	SServerAction action = {0};
	action.uAction = kSServerAction_SendToServer;
	action.uIndex = (unsigned short)_uConnIndex;
	action.uTag = (unsigned short)_uLength;
	size_t uRet = m_xEventBuffer.Write((char*)&action, sizeof(SServerAction));
	m_xEventBuffer.Write(_pData, _uLength);

	UnlockSendBuffer();

	//	awake and process event
	awake();

	return int(uRet);
}

int SServerEngine::SendPacketToUser(unsigned int _uConnIndex, char* _pData, size_t _uLength)
{
	SServerConn* pConn = GetUserConn(_uConnIndex);
	if (NULL == pConn) {
		return -1;
	}

	LockSendBuffer();

	SServerAction action = {0};
	action.uAction = kSServerAction_SendToUser;
	action.uIndex = (unsigned short)_uConnIndex;
	action.uTag = (unsigned short)_uLength;
	size_t uRet = m_xEventBuffer.Write((char*)&action, sizeof(SServerAction));
	m_xEventBuffer.Write(_pData, _uLength);

	UnlockSendBuffer();

	//	awake and process event
	awake();

	return int(uRet);
}

int SServerEngine::SyncSendPacketToServer(unsigned int _uConnIndex, char* _pData, size_t _uLength)
{
	SServerConn* pConn = GetServerConn(_uConnIndex);
	if (NULL == pConn) {
		LOGERROR("Failed to get conn, index:%d", _uConnIndex);
		return 1;
	}
	if (pConn->eConnState != kSServerConnState_Connected)
	{
		LOGERROR("Trying to send to a disconnected connection");
		return 2;
	}

	//	write head
	unsigned int uNetLength = DEF_NETPROTOCOL_HEADER_LENGTH + _uLength;
	uNetLength = htonl(uNetLength);

	//	try send
	int nDataSended = bufferevent_write(pConn->pEv, &uNetLength, DEF_NETPROTOCOL_HEADER_LENGTH);
	if (0 != nDataSended) {
		return nDataSended;
	}
	nDataSended = bufferevent_write(pConn->pEv, _pData, _uLength);
	if (0 != nDataSended) {
		return nDataSended;
	}

	return 0;
}

int SServerEngine::SyncSendPacketToUser(unsigned int _uConnIndex, char* _pData, size_t _uLength)
{
	SServerConn* pConn = GetUserConn(_uConnIndex);
	if (NULL == pConn) {
		LOGERROR("Failed to get conn, index:%d", _uConnIndex);
		return 1;
	}
	if (pConn->eConnState != kSServerConnState_Connected)
	{
		LOGERROR("Trying to send to a disconnected connection");
		return 2;
	}

	//	write head
	unsigned int uNetLength = DEF_NETPROTOCOL_HEADER_LENGTH + _uLength;
	uNetLength = htonl(uNetLength);

	//	try send
	int nDataSended = bufferevent_write(pConn->pEv, &uNetLength, DEF_NETPROTOCOL_HEADER_LENGTH);
	if (0 != nDataSended) {
		return nDataSended;
	}
	nDataSended = bufferevent_write(pConn->pEv, _pData, _uLength);
	if (0 != nDataSended) {
		return nDataSended;
	}

	return 0;
}

int SServerEngine::CloseUserConnection(unsigned int _uConnIndex)
{
	SServerAction action = {0};
	action.uAction = kSServerAction_CloseUserConn;
	action.uIndex = (unsigned short)_uConnIndex;

	SServerAutoLocker locker(&m_xSendMutex);

	m_xEventBuffer.Write((char*)&action, sizeof(SServerAction));
	awake();

	return 0;
}

SServerConn* SServerEngine::GetUserConn(unsigned int _uConnIndex)
{
	if (_uConnIndex > m_uMaxConnUser ||
		0 == _uConnIndex) {
		LOGERROR("Invalid conn index %d", _uConnIndex);
		return NULL;
	}
	return m_pUserConnArray[_uConnIndex];
}

void SServerEngine::SetUserConn(unsigned int _uConnIndex, SServerConn* conn)
{
	if (_uConnIndex > m_uMaxConnUser ||
		0 == _uConnIndex) {
		LOGERROR("Invalid conn index %d", _uConnIndex);
		return;
	}
	m_pUserConnArray[_uConnIndex] = conn;
}

SServerConn* SServerEngine::GetServerConn(unsigned int _uConnIndex)
{
	if (_uConnIndex > m_uMaxConnServer ||
		0 == _uConnIndex) {
		LOGERROR("Invalid conn index %d", _uConnIndex);
		return NULL;
	}
	return m_pServerConnArray[_uConnIndex];
}

void SServerEngine::SetServerConn(unsigned int _uConnIndex, SServerConn* conn)
{
	if (_uConnIndex > m_uMaxConnServer ||
		0 == _uConnIndex) {
		LOGERROR("Invalid conn index %d", _uConnIndex);
		return;
	}
	m_pServerConnArray[_uConnIndex] = conn;
}

void SServerEngine::LockSendBuffer()
{
	pthread_mutex_lock(&m_xSendMutex);
}

void SServerEngine::UnlockSendBuffer()
{
	pthread_mutex_unlock(&m_xSendMutex);
}

int SServerEngine::CloseServerConnection(unsigned int _uConnIndex)
{
	SServerAction action = {0};
	action.uAction = kSServerAction_CloseServerConn;
	action.uIndex = (unsigned short)_uConnIndex;

	SServerAutoLocker locker(&m_xSendMutex);

	m_xEventBuffer.Write((char*)&action, sizeof(SServerAction));
	awake();

	return 0;
}

void SServerEngine::Callback_OnAcceptUser(unsigned int _uIndex)
{
	if (NULL == m_pFuncOnAcceptUser) {
		return;
	}
	m_pFuncOnAcceptUser(_uIndex);
}

void SServerEngine::Callback_OnAcceptServer(unsigned int _uIndex)
{
	if (NULL == m_pFuncOnAcceptServer) {
		return;
	}
	m_pFuncOnAcceptServer(_uIndex);
}

void SServerEngine::Callback_OnDisconnectedUser(unsigned int _uIndex)
{
	if (NULL == m_pFuncOnDisconnectedUser) {
		return;
	}
	m_pFuncOnDisconnectedUser(_uIndex);
}

void SServerEngine::Callback_OnDisconnectedServer(unsigned int _uIndex)
{
	if (NULL == m_pFuncOnDisconnectedServer) {
		return;
	}
	m_pFuncOnDisconnectedServer(_uIndex);
}

void SServerEngine::Callback_OnRecvUser(unsigned int _uIndex, char* _pData, unsigned int _uLength)
{
	if (NULL == m_pFuncOnRecvUser) {
		return;
	}
	m_pFuncOnRecvUser(_uIndex, _pData, _uLength);
}

void SServerEngine::Callback_OnRecvServer(unsigned int _uIndex, char* _pData, unsigned int _uLength)
{
	if (NULL == m_pFuncOnRecvServer) {
		return;
	}
	m_pFuncOnRecvServer(_uIndex, _pData, _uLength);
}

void SServerEngine::onConnectionClosed(SServerConn* _pConn)
{
	if (_pConn->bServerConn) {
		onServerConnectionClosed(_pConn);
	} else {
		onUserConnectionClosed(_pConn);
	}
}

void SServerEngine::onUserConnectionClosed(SServerConn* _pConn)
{
	//	after connection is closed (recv return error)
	LOGPRINT("User connection %d closed", _pConn->uConnIndex);

	//	callback
	Callback_OnDisconnectedUser(_pConn->uConnIndex);

	//	free index
	SetUserConn(_pConn->uConnIndex, NULL);
	m_xUserIndexMgr.Push(_pConn->uConnIndex);

	//	free bufferevent
	bufferevent_free(_pConn->pEv);
	_pConn->pEv = NULL;
	delete _pConn;
	_pConn = NULL;

	--m_nConnectedUserCount;
}

void SServerEngine::onServerConnectionClosed(SServerConn* _pConn)
{
	LOGINFO("Server connection %d closed", _pConn->uConnIndex);

	//	callback
	//	2 possibilities, 1 : connect failed and remove conn 2 : connected and remove conn
	if (_pConn->eConnState == kSServerConnState_Connected) {
		Callback_OnDisconnectedServer(_pConn->uConnIndex);
		_pConn->eConnState = kSServerConnState_Disconnected;
	}

	//	free index
	SetServerConn(_pConn->uConnIndex, NULL);
	m_xServerIndexMgr.Push(_pConn->uConnIndex);

	//	free bufferevent
	bufferevent_free(_pConn->pEv);
	_pConn->pEv = NULL;
	delete _pConn;
	_pConn = NULL;

	--m_nConnectedServerCount; 
}

// process all events in libevent thread
void SServerEngine::processConnEvent()
{
	SServerAction action = {0};

	SServerAutoLocker locker(&m_xSendMutex);

	while(0 != m_xEventBuffer.GetReadableSize())
	{
		if (sizeof(SServerAction) != m_xEventBuffer.Read((char*)&action, sizeof(SServerAction))) {
			LOGERROR("Process conn event failed.");
			m_xEventBuffer.Reset();
			return;
		}

		if (kSServerAction_SendToUser == action.uAction) {
			SServerConn* pConn = GetUserConn((unsigned int)action.uIndex);
			if (NULL == pConn) {
				LOGERROR("Failed to get conn, index:%d", action.uIndex);
			} else {
				//	write head
				if (pConn->eConnState == kSServerConnState_Connected)
				{
					unsigned int uNetLength = DEF_NETPROTOCOL_HEADER_LENGTH + action.uTag;
					uNetLength = htonl(uNetLength);
					bufferevent_write(pConn->pEv, &uNetLength, DEF_NETPROTOCOL_HEADER_LENGTH);
					bufferevent_write(pConn->pEv, m_xEventBuffer.GetReadableBufferPtr(), action.uTag);
				}
			}
			m_xEventBuffer.Read(NULL, action.uTag);
		} else if(kSServerAction_SendToServer == action.uAction) {
			SServerConn* pConn = GetServerConn((unsigned int)action.uIndex);
			if(NULL == pConn) {
				LOGERROR("Failed to get conn, index:%d", action.uIndex);
			} else {
				//	write head
				if (pConn->eConnState == kSServerConnState_Connected)
				{
					unsigned int uNetLength = DEF_NETPROTOCOL_HEADER_LENGTH + action.uTag;
					uNetLength = htonl(uNetLength);
					bufferevent_write(pConn->pEv, &uNetLength, DEF_NETPROTOCOL_HEADER_LENGTH);
					bufferevent_write(pConn->pEv, m_xEventBuffer.GetReadableBufferPtr(), action.uTag);
				}
			}
			m_xEventBuffer.Read(NULL, action.uTag);
		} else if (kSServerAction_CloseUserConn == action.uAction) {
			SServerConn* pConn = GetUserConn((unsigned int)action.uIndex);
			if (NULL == pConn) {
				LOGERROR("Failed to close conn, index:%d", action.uIndex);
			} else {
				onUserConnectionClosed(pConn);
				//	User close the connection, just close socket
				/*if (INVALID_SOCKET != pConn->fd) {
					evutil_closesocket(pConn->fd);
					pConn->fd = INVALID_SOCKET;
					LOGDEBUG("Close fd of user con %d", pConn->uConnIndex);
				}*/
			}
		} else if (kSServerAction_CloseServerConn == action.uAction) {
			SServerConn* pConn = GetServerConn((unsigned int)action.uIndex);
			if (NULL == pConn) {
				LOGERROR("Failed to close conn, index:%d", action.uIndex);
			} else {
				onServerConnectionClosed(pConn);
				//	just close the socket , after recv return error, the bufferevent will be deleted
				/*if (INVALID_SOCKET != pConn->fd) {
					evutil_closesocket(pConn->fd);
					pConn->fd = INVALID_SOCKET;
					LOGDEBUG("Close fd of server con %d", pConn->uConnIndex);
				}*/
			}
		} else if (kSServerAction_Connect == action.uAction) {
			SServerActionConnectContext ctx;
			m_xEventBuffer.Read((char*)&ctx, sizeof(SServerActionConnectContext));
			processConnectAction(&ctx);
		} else {
			LOGERROR("Invalid conn event %d", action.uAction);
			m_xEventBuffer.Reset();
		}
	}

	//	reset the buffer
	m_xEventBuffer.Reset();
}

void SServerEngine::awake()
{
	int nRet = send(m_arraySocketPair[1], "1", 1, 0);
	if (nRet < 1) {
		LOGERROR("Write notify fail, err(%d): %s", errno, strerror(errno));
	}
}

void SServerEngine::processConnectAction(SServerActionConnectContext* _pAction)
{
	//	get new conn index
	unsigned int uConnIndex = m_xServerIndexMgr.Pop();
	if (uConnIndex == IndexManager::s_uInvalidIndex) {
		LOGERROR("Reach max fd");
		_pAction->fnFailed(0, _pAction->pArg);
		return;
	}

	bufferevent* pBev = bufferevent_socket_new(m_pEventBase, -1, SSERVERCONN_FLAG);
	if ( NULL == pBev ) {
		LOGERROR("Can't create bufferevent, addr %d port %d", _pAction->addr.sin_addr.s_addr, _pAction->addr.sin_port );
		_pAction->fnFailed(uConnIndex, _pAction->pArg);
		return;
	}

	int nRet = bufferevent_socket_connect(pBev, (struct sockaddr *)&_pAction->addr, sizeof(struct sockaddr));
	if (0 != nRet) {
		bufferevent_free(pBev);
		pBev = NULL;
		LOGERROR("Connect failed. addr %s port %d", _pAction->addr.sin_addr.s_addr, _pAction->addr.sin_port);
		_pAction->fnFailed(uConnIndex, _pAction->pArg);
		return;
	}

	int nFd = bufferevent_getfd(pBev);

	//	create new conn context
	SServerConn* pConn = new SServerConn;
	pConn->pEng = this;
	pConn->fd = nFd;
	pConn->pEv = pBev;
	pConn->uConnIndex = uConnIndex;
	pConn->bServerConn = true;
	pConn->m_fnOnConnectSuccess = _pAction->fnSuccess;
	pConn->m_fnOnConnectFailed = _pAction->fnFailed;

	bufferevent_setcb(pBev,
		&SServerEngine::__onConnRead,
		NULL,
		&SServerEngine::__onConnEvent,
		pConn);
	bufferevent_setwatermark(pBev, EV_READ, DEF_NETPROTOCOL_HEADER_LENGTH, 0);
	bufferevent_setwatermark(pBev, EV_WRITE, 0, 0);
	bufferevent_enable(pBev, EV_READ);
	SetServerConn(uConnIndex, pConn);

	//	get address
	sockaddr_in addr;
	int len = sizeof(sockaddr);
	memset(&addr, 0, len);
	getpeername (nFd, (sockaddr*)&addr, &len);
	pConn->SetAddress(&addr);
	pConn->eConnState = kSServerConnState_Connecting;

	//	wait for connect result
}

int SServerEngine::AddTimerJob(unsigned int _nJobId, unsigned int _nTriggerIntervalMS, FUNC_ONTIMER _fnOnTimer)
{
	SServerAutoLocker locker(&m_xTimerMutex);

	SServerTimerJob* pJob = new SServerTimerJob;
	memset(pJob, 0, sizeof(SServerTimerJob));
	pJob->nJobId = _nJobId;
	pJob->nTriggerIntervalMS = _nTriggerIntervalMS;
	pJob->fnOnTimer = _fnOnTimer;
	m_xTimerJobs.push_back(pJob);

	return 0;
}

int SServerEngine::RemoveTimerJob(unsigned int _nJobId)
{
	SServerAutoLocker locker(&m_xTimerMutex);

	SServerTimerJobList::iterator iterB = m_xTimerJobs.begin();
	for(iterB;
		iterB != m_xTimerJobs.end();
		)
	{
		SServerTimerJob* pJob = *iterB;

		if (pJob->nJobId == _nJobId) {
			delete pJob;
			iterB = m_xTimerJobs.erase(iterB);
		} else {
			++iterB;
		}
	}

	return 0;
}

int SServerEngine::ClearTimerJob()
{
	SServerAutoLocker locker(&m_xTimerMutex);

	SServerTimerJobList::iterator iterB = m_xTimerJobs.begin();
	for(iterB;
		iterB != m_xTimerJobs.end();
		++iterB
		)
	{
		SServerTimerJob* pJob = *iterB;
		delete pJob;
	}

	m_xTimerJobs.clear();

	return 0;
}

void SServerEngine::processTimerJob()
{
	SServerAutoLocker locker(&m_xTimerMutex);
	int nNowTick = int(GetTickCount());

	SServerTimerJobList::const_iterator iterB = m_xTimerJobs.begin();
	for(iterB;
		iterB != m_xTimerJobs.end();
		++iterB)
	{
		SServerTimerJob* pJob = *iterB;

		if (nNowTick > pJob->nLastTriggerTime + pJob->nTriggerIntervalMS) {
			//	trigger
			if (pJob->fnOnTimer) {
				pJob->fnOnTimer(pJob->nJobId);
			}

			//	update
			pJob->nLastTriggerTime = nNowTick;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//	static handlers
void* SServerEngine::__threadEntry(void* _pArg)
{
	SServerEngine* pIns = (SServerEngine*)_pArg;
	pIns->m_nWorkingTid = (int)GetCurrentThreadId();

	//	initialize
	//evthread_use_windows_threads();
	if (pIns->m_bUseIOCP) {
		LOGINFO("Use IOCP mode.");
		evthread_use_windows_threads();
		event_config* evcfg = event_config_new();
		event_config_set_flag(evcfg, EVENT_BASE_FLAG_STARTUP_IOCP);
		pIns->m_pEventBase = event_base_new_with_config(evcfg);
		event_config_free(evcfg);
	} else {
		pIns->m_pEventBase = event_base_new();
	}

	if (NULL == pIns->m_pEventBase) {
		LOGERROR("Create event_base failed");
		return (void*)-1;
	}

	//	create socket pair
	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pIns->m_arraySocketPair)) {
		LOGERROR("evutil_socketpair fail");
		return (void*)-1;
	}

	evutil_make_socket_nonblocking(pIns->m_arraySocketPair[0]);
	evutil_make_socket_nonblocking(pIns->m_arraySocketPair[1]);
	pIns->m_pBvEvent = bufferevent_socket_new(pIns->m_pEventBase, pIns->m_arraySocketPair[0], 0);
	if (NULL == pIns->m_pBvEvent) {
		LOGERROR("Create bufferevent failed");
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
	if (NULL == pIns->m_pConnListener) {
		LOGPRINT("Failed to listen on host:%s port:%d", pIns->m_xAddr.c_str(), pIns->m_uPort);
		exit(kSServerResult_ListenFailed);
	}

	//	timer event
	struct timeval tv = { 0, 5 * 1000 };
	pIns->m_pTimerEvent = event_new(pIns->m_pEventBase, -1, EV_PERSIST, &SServerEngine::__onEventTimer, pIns);
	if ( NULL == pIns->m_pTimerEvent ) {
		LOGERROR("Create lib event timer failed!");
		exit(1);
	}
	evtimer_add(pIns->m_pTimerEvent, &tv);

	LOGPRINT("Thread working, Id %d", GetCurrentThreadId());

	//	event loop
	pIns->m_eStatus = kSServerStatus_Running;
	int nResult = event_base_dispatch(pIns->m_pEventBase);
	pIns->m_eStatus = kSServerStatus_Stop;
	LOGINFO("event loop quit with code %d", nResult);

	// do clear up works
	// close listener
	LOGDEBUG("free listener");
	evconnlistener_free(pIns->m_pConnListener);
	pIns->m_pConnListener = NULL;
	// we should close all connections
	LOGDEBUG("free connections");
	for (int i = 0; i < (int)pIns->m_uMaxConnUser; ++i)
	{
		SServerConn* pConn = pIns->m_pUserConnArray[i];
		if (NULL != pConn)
		{
			pIns->onUserConnectionClosed(pConn);
		}
	}
	for (int i = 0; i < (int)pIns->m_uMaxConnServer; ++i)
	{
		SServerConn* pConn = pIns->m_pServerConnArray[i];
		if (NULL != pConn)
		{
			pIns->onServerConnectionClosed(pConn);
		}
	}
	// close local sockets
	LOGDEBUG("free socket pair");
	bufferevent_free(pIns->m_pBvEvent);
	pIns->m_pBvEvent = NULL;
	evutil_closesocket(pIns->m_arraySocketPair[1]);
	for (int i = 0; i < sizeof(pIns->m_arraySocketPair) / sizeof(pIns->m_arraySocketPair[0]); ++i)
	{
		pIns->m_arraySocketPair[i] = INVALID_SOCKET;
	}
	// free all timer job
	LOGDEBUG("free timer");
	evtimer_del(pIns->m_pTimerEvent);
	pIns->m_pTimerEvent = NULL;
	pIns->ClearTimerJob();
	// free the event base
	LOGDEBUG("free event base");
	event_base_free(pIns->m_pEventBase);
	pIns->m_pEventBase = NULL;

	return NULL;
}

void SServerEngine::__onAcceptConn(struct evconnlistener *pEvListener, evutil_socket_t sock, struct sockaddr *pAddr, int iLen, void *ptr)
{
	SServerEngine* pIns = (SServerEngine*)ptr;
	event_base* pEventBase = evconnlistener_get_base(pEvListener);

	//	get new conn index
	unsigned int uConnIndex = pIns->m_xUserIndexMgr.Pop();
	if (0 == uConnIndex ||
		IndexManager::s_uInvalidIndex == uConnIndex) {
		LOGPRINT("Reach max connection, close new connection.");
		evutil_closesocket(sock);
		return;
	}

#ifdef _DEBUG
	LOGPRINT("Accept conn[%d], ThreadId %d", uConnIndex, GetCurrentThreadId());
#endif

	//	register event
	bufferevent* pEv = bufferevent_socket_new(pEventBase,
		sock,
		SSERVERCONN_FLAG
		);
	if (NULL == pEv) {
		LOGPRINT("Failed to bind bufferevent");
		evutil_closesocket(sock);
		return;
	}

	++pIns->m_nConnectedUserCount;

	//	create new conn context
	SServerConn* pConn = new SServerConn;
	pConn->pEng = pIns;
	pConn->fd = sock;
	pConn->pEv = pEv;
	pConn->uConnIndex = uConnIndex;

	bufferevent_setcb(pEv,
		&SServerEngine::__onConnRead,
		NULL,
		&SServerEngine::__onConnEvent,
		pConn);
	bufferevent_setwatermark(pEv, EV_READ, DEF_NETPROTOCOL_HEADER_LENGTH, 0);
	bufferevent_setwatermark(pEv, EV_WRITE, 0, 0);
	bufferevent_enable(pEv, EV_READ);
	pIns->SetUserConn(uConnIndex, pConn);

	//	get address
	sockaddr_in addr;
	int len = sizeof(sockaddr);
	memset(&addr, 0, len);
	getpeername (sock, (sockaddr*)&addr, &len);
	pConn->SetAddress(&addr);
	pConn->eConnState = kSServerConnState_Connected;

	//	callback
	pIns->Callback_OnAcceptUser(uConnIndex);
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

	if (0 == uRead) {
		//	connection closed
		LOGPRINT("Conn %d closed", pConn->uConnIndex);
		pConn->pEng->onConnectionClosed(pConn);
		
		return;
	}

	//	read head or body
	if (0 == pConn->m_uPacketHeadLength) {
		pConn->readHead();
	} else {
		pConn->readBody();
	}
}

void SServerEngine::__onConnEvent(struct bufferevent* pEv, short what, void* pCtx)
{
	SServerConn* pConn = (SServerConn*)pCtx;
	SServerEngine* pEng = pConn->pEng;

	//	process event
	if (what & BEV_EVENT_CONNECTED) {
		if (pConn->bServerConn &&
			pConn->eConnState == kSServerConnState_Connecting) {
			pConn->Callback_OnConnectSuccess();
			pConn->eConnState = kSServerConnState_Connected;
			pEng->Callback_OnAcceptServer(pConn->uConnIndex);
			++pEng->m_nConnectedServerCount;
		}
		return;
	}
	
	if (what & BEV_EVENT_EOF) {
		LOGPRINT("Conn %d closed", pConn->uConnIndex);
	} else {
		LOGPRINT("Conn %d error:%d", pConn->uConnIndex, what);
	}

	// on connecting as client role
	if (pConn->bServerConn &&
		pConn->eConnState == kSServerConnState_Connecting) {
		pConn->Callback_OnConnectFailed();
		pConn->eConnState = kSServerConnState_ConnectFailed;
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

void SServerEngine::__onEventTimer(evutil_socket_t, short _nEvents, void * _pCxt)
{
	SServerEngine* pEng = (SServerEngine*)_pCxt;
	pEng->processTimerJob();
}