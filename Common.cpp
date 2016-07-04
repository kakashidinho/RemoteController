#include "Common.h"

#include <stdio.h>

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

#	ifdef __ANDROID__
#		include <android/log.h>
#	endif

#endif//#ifdef WIN32

namespace HQRemote {
	/*-------helper ---------------*/
	void HQ_APICALL SetCurrentThreadName(const char* threadName)
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

	void HQ_APICALL Log(const char* format, ...)
	{
		va_list arg;
		va_start(arg, format);
		
		LogV(format, arg);

		va_end(arg);
	}

	void HQ_APICALL LogErr(const char* format, ...)
	{
		va_list arg;
		va_start(arg, format);

		LogErrV(format, arg);

		va_end(arg);
	}

	void HQ_APICALL LogV(const char* format, va_list arg)
	{
#ifdef __ANDROID__
		
		__android_log_vprint(ANDROID_LOG_DEBUG, "HQRemote", format, arg);
#elif defined WIN32

		char buffer[1024];
		int chars = vsnprintf(buffer, sizeof(buffer) - 2, format, arg);
		if (chars < 0)
			return;

		buffer[chars] = '\n';
		buffer[chars + 1] = '\0';

#	if defined _DEBUG || defined DEBUG
		OutputDebugStringA(buffer);
#	endif

#else
		vfprintf(stdout, format, arg);
#endif
	}

	void HQ_APICALL LogErrV(const char* format, va_list arg)
	{
#ifdef __ANDROID__

		__android_log_vprint(ANDROID_LOG_ERROR, "HQRemote", format, arg);
#elif defined WIN32

		char buffer[1024];
		int chars = vsnprintf(buffer, sizeof(buffer) - 2, format, arg);
		if (chars < 0)
			return;

		buffer[chars] = '\n';
		buffer[chars + 1] = '\0';

#	if defined _DEBUG || defined DEBUG
		OutputDebugStringA(buffer);
#	endif

#else
		vfprintf(stderr, format, arg);
#endif
	}
}