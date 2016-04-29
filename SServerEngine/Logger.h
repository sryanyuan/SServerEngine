#ifndef _INC_LOGGER_
#define _INC_LOGGER_
//////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdarg.h>
//////////////////////////////////////////////////////////////////////////

inline void logPrint(const char* _pszFunction, int _nLine, const char* _pszFormat, ...)
{
	char szLogBuffer[512];
	szLogBuffer[0] = 0;
	szLogBuffer[sizeof(szLogBuffer) / sizeof(szLogBuffer[0]) - 1] = 0;
	int nWrite = _snprintf(szLogBuffer, sizeof(szLogBuffer) / sizeof(szLogBuffer[0]) - 1, "%s:%d ", _pszFunction, _nLine);
	if(nWrite < 0)
	{
		//	this log is truncated
		return;
	}
	size_t uPtr = strlen(szLogBuffer);
	if(uPtr >= sizeof(szLogBuffer) / sizeof(szLogBuffer[0] - 1))
	{
		return;
	}

	va_list args;
	va_start(args, _pszFormat);
	int nRet = vsnprintf(szLogBuffer + uPtr, sizeof(szLogBuffer) / sizeof(szLogBuffer[0]) - uPtr, _pszFormat, args);
	va_end(args);

	if(nRet <= 0 ||
		nRet >= sizeof(szLogBuffer) / sizeof(szLogBuffer[0]) - uPtr - 1)
	{
		return;
	}

	szLogBuffer[nRet + uPtr] = '\n';
	szLogBuffer[nRet + uPtr + 1] = '\0';
	printf(szLogBuffer);
}

#define LOGPRINT(FORMAT, ...)	logPrint(__FUNCTION__, __LINE__, FORMAT, __VA_ARGS__)
//////////////////////////////////////////////////////////////////////////
#endif