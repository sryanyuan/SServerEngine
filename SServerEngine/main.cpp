// SServerEngine.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "SServerEngine.h"

SServerEngine eng;

void printThreadId()
{
	DWORD dwTid = GetCurrentThreadId();
	printf("Thread id %d \n", dwTid);
}

void onConnected(unsigned int _index)
{
	printThreadId();
	LOGINFO("%d connected", _index);
}

void onDisconnected(unsigned int _index)
{
	printThreadId();
	LOGINFO("%d disconnected", _index);
}

void onRecv(unsigned int _index, char* _data, unsigned int _len)
{
	static char tx[1024];
	strncpy(tx, _data, _len);
	printThreadId();
	LOGINFO("%d recv %s length %d", _index, tx, _len);

	if('Q' == _data[0])
	{
		eng.CloseConnection(_index);
	}
	else if('T' == _data[0])
	{
		eng.SendPacket(_index, tx, _len);
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0202, &wsa_data);
#endif
	printThreadId();

	SServerInitDesc desc;
	desc.uMaxConn = 5;
	desc.uPort = 4444;
	desc.xAddr = "127.0.0.1";
	desc.pFuncOnConnected = onConnected;
	desc.pFuncOnDisconncted = onDisconnected;
	desc.pFuncOnRecv = onRecv;

	eng.Init(&desc);
	int nRet = eng.Start();
	
	if(nRet != kSServerResult_Ok)
	{
		LOGPRINT("Start server failed.Error:%d", nRet);
		exit(1);
	}

	for(;;)
	{
		Sleep(10);
	}

	return 0;
}

