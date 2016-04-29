#ifndef _INC_INDEXMANAGER_
#define _INC_INDEXMANAGER_
//////////////////////////////////////////////////////////////////////////
#include <vector>
#include "Logger.h"
//////////////////////////////////////////////////////////////////////////
typedef std::vector<unsigned int> IndexContainer;
//////////////////////////////////////////////////////////////////////////
class IndexManager
{
public:
	IndexManager();
	~IndexManager();

public:
	void Init(size_t _uSize);
	unsigned int Pop();
	int Push(unsigned int _uIndex);

public:
	static unsigned int s_uInvalidIndex;

private:
	IndexContainer m_xContainer;
	IndexContainer m_xFreeIndexes;
	unsigned int m_uHead;
	unsigned int m_uFreePtr;
};
//////////////////////////////////////////////////////////////////////////
#endif