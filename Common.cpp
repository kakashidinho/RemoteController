#include "Common.h"

#ifdef WIN32
#	include <windows.h>

//for setting thread name
#	define MS_VC_EXCEPTION 0x406D1388

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

#else//#ifdef WIN32
#include <pthread.h>
#endif//#ifdef WIN32

namespace HQRemote {
	/*-------helper ---------------*/
	void SetCurrentThreadName(const char* threadName)
	{
#ifdef WIN32
		DWORD dwThreadID = ::GetCurrentThreadId();

		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = threadName;
		info.dwThreadID = dwThreadID;
		info.dwFlags = 0;

		__try
		{
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
#elif defined __APPLE__
		pthread_setname_np(threadName);
#endif//#ifdef WIN32
	}
}