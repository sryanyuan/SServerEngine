#include "IndexManager.h"
//////////////////////////////////////////////////////////////////////////
unsigned int IndexManager::s_uInvalidIndex = 0;
//////////////////////////////////////////////////////////////////////////
IndexManager::IndexManager()
{
	
}

IndexManager::~IndexManager()
{

}

void IndexManager::Init(size_t _uSize)
{
	m_xIndexCreator.Initialize(DWORD(_uSize));
}

unsigned int IndexManager::Pop()
{
	unsigned int uIndex = ICAllocIndex(&m_xIndexCreator);
	return uIndex;
}

void IndexManager::Push(unsigned int _uIndex)
{
	ICFreeIndex(&m_xIndexCreator, DWORD(_uIndex));
}