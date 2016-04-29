// SServerEngine.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "SServerEngine.h"

int _tmain(int argc, _TCHAR* argv[])
{
#ifdef WIN32
	WSADATA wsa_data;
	WSAStartup(0x0202, &wsa_data);
#endif

	SServerInitDesc desc;
	desc.uMaxConn = 5;
	desc.uPort = 4444;
	desc.xAddr = "127.0.0.1";

	SServerEngine eng;
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

