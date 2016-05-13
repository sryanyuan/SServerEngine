#include "SServerConn.h"
#include "Logger.h"
#include "SServerEngine.h"
//////////////////////////////////////////////////////////////////////////
SServerConn::SServerConn()
{
	pEng = NULL;
	pEv = NULL;
	uConnIndex = 0;
	fd = 0;
	m_uPacketHeadLength = 0;
	m_xReadBuffer.AllocBuffer(5 * 1024);
}

SServerConn::~SServerConn()
{

}


void SServerConn::readHead()
{
	evbuffer* pInput = bufferevent_get_input(pEv);
	size_t uLen = evbuffer_get_length(pInput);

	if(uLen < DEF_NETPROTOCOL_HEADER_LENGTH)
	{
		LOGERROR("Head length invalid %d", uLen);
		pEng->CloseConnection(uConnIndex);
		return;
	}

	unsigned int uHeadLength = 0;
	evbuffer_copyout(pInput, &uHeadLength, DEF_NETPROTOCOL_HEADER_LENGTH);
	evbuffer_drain(pInput, DEF_NETPROTOCOL_HEADER_LENGTH);
	m_uPacketHeadLength = (unsigned int)ntohl(uHeadLength);

	//	next we should read body
	if(m_uPacketHeadLength <= DEF_NETPROTOCOL_HEADER_LENGTH)
	{
		LOGERROR("Head length content invalid %d", m_uPacketHeadLength);
		pEng->CloseConnection(uConnIndex);
		return;
	}

	//	is data full to read body?
	if(uLen >= m_uPacketHeadLength)
	{
		readBody();
	}
	else
	{
		//	wait read body
		bufferevent_setwatermark(pEv, EV_READ, m_uPacketHeadLength - DEF_NETPROTOCOL_HEADER_LENGTH, 0);
	}
}

void SServerConn::readBody()
{
	//	make sure we had read packet length
	if(m_uPacketHeadLength <= DEF_NETPROTOCOL_HEADER_LENGTH)
	{
		LOGERROR("Invalid body length %d", m_uPacketHeadLength);
		pEng->CloseConnection(uConnIndex);
		return;
	}

	unsigned int uBodyLength = m_uPacketHeadLength - DEF_NETPROTOCOL_HEADER_LENGTH;

	evbuffer* pInput = bufferevent_get_input(pEv);
	size_t uLen = evbuffer_get_length(pInput);

	if(uLen < uBodyLength)
	{
		LOGERROR("Invalid read body length %d", uLen);
		pEng->CloseConnection(uConnIndex);
		return;
	}

	//	read the buffer to read buffer
	if(m_xReadBuffer.GetAvailableSize() < uBodyLength)
	{
		m_xReadBuffer.ReallocBuffer();
	}

	evbuffer_copyout(pInput, m_xReadBuffer.GetFreeBufferPtr(), uBodyLength);
	evbuffer_drain(pInput, uBodyLength);
	m_xReadBuffer.Rewind();

	//	callback
	pEng->Callback_OnRecv(uConnIndex, m_xReadBuffer.GetReadableBufferPtr(), uBodyLength);
	m_xReadBuffer.Reset();

	//	continue read head
	m_uPacketHeadLength = 0;

	//	check next packet
	uLen -= uBodyLength;
	if(uLen >= DEF_NETPROTOCOL_HEADER_LENGTH)
	{
		readHead();
	}
	else
	{
		//	wait read head
		bufferevent_setwatermark(pEv, EV_READ, DEF_NETPROTOCOL_HEADER_LENGTH, 0);
	}
}