#include "IndexManager.h"
//////////////////////////////////////////////////////////////////////////
unsigned int IndexManager::s_uInvalidIndex = 0xffffffff;
//////////////////////////////////////////////////////////////////////////
IndexManager::IndexManager()
{
	m_uHead = 0;
	m_uFreePtr = 0;
}

IndexManager::~IndexManager()
{

}

void IndexManager::Init(size_t _uSize)
{
	m_xContainer.resize(_uSize);
	m_xFreeIndexes.resize(_uSize);

	//	initialize all index
	for(size_t i = 0; i < _uSize; ++i)
	{
		m_xContainer[i] = i + 1;
		m_xFreeIndexes[i] = 0;
	}
}

unsigned int IndexManager::Pop()
{
	if(m_uHead >= m_xContainer.size())
	{
		//	move free indexes
		if(m_uFreePtr != 0)
		{
			size_t freeSize = m_uFreePtr;
			for(size_t i = 0; i < freeSize; ++i)
			{
				if(0 == m_xFreeIndexes[i])
				{
					continue;
				}

				if(m_uHead == 0)
				{
					LOGPRINT("Index queue already full.");
					break;
				}

				m_xContainer[--m_uHead] = m_xFreeIndexes[i];
				--m_uFreePtr;
			}
		}
		else
		{
			return s_uInvalidIndex;
		}
	}

	if(m_uHead >= m_xContainer.size())
	{
		return s_uInvalidIndex;
	}

	unsigned int uIndex = m_xContainer[m_uHead];
	m_xContainer[m_uHead] = 0;
	++m_uHead;
	return uIndex;
}

int IndexManager::Push(unsigned int _uIndex)
{
#ifdef _DEBUG
	//	check all index are unique
	for(size_t i = m_uHead; i < m_xContainer.size(); ++i)
	{
		if(m_xContainer[i] == _uIndex)
		{
			LOGPRINT("Find same index");
			return -1;
		}
	}
#endif
	//	push to free indexes queue
	if(m_uFreePtr >= m_xFreeIndexes.size())
	{
		LOGPRINT("Free indexes queue overflow...");
		return -1;
	}
	m_xFreeIndexes[m_uFreePtr++] = _uIndex;

	return 0;
}