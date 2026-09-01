
#include <stdlib.h>

static inline void* Arena_Create(size_t Size) {
	return malloc(Size);
}

static inline void Arena_Destroy(void* pArena) {
	free(pArena);
}

static inline void* Arena_Push(void* pArena, size_t* pCounter, size_t Size) {
	void* pResult = (unsigned char*)pArena + *pCounter;
	*pCounter += Size;
	return pResult;
}
