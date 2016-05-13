#ifndef _INC_SSERVERCONN_
#define _INC_SSERVERCONN_
//////////////////////////////////////////////////////////////////////////
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include "SServerBuffer.h"
#include "Def.h"
//////////////////////////////////////////////////////////////////////////
class SServerEngine;
//////////////////////////////////////////////////////////////////////////
class SServerConn
{
	friend class SServerEngine;

public:
	SServerConn();
	~SServerConn();

private:
	void readHead();
	void readBody();

private:
	SServerEngine* pEng;
	bufferevent* pEv;
	unsigned int uConnIndex;
	evutil_socket_t fd;

	unsigned int m_uPacketHeadLength;

	SServerBuffer m_xReadBuffer;
};
//////////////////////////////////////////////////////////////////////////
#endif