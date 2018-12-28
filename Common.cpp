////////////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2016-2018 Le Hoang Quyen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//		http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////////////

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

#	define RT_POLICY SCHED_RR

#	ifdef __ANDROID__
#		include <android/log.h>

#		include "android/JniUtils.h"
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
#elif defined __ANDROID__
		auto env = getCurrenThreadJEnv();

		setCurrentThreadName(env, threadName);
#endif//#ifdef WIN32
	}

	void HQ_APICALL SetCurrentThreadPriority(int priority) {
#ifdef WIN32
		// TODO
#else
		if (priority == 0)
			return;
		sched_param sch_params;
		sch_params.sched_priority = priority;
		auto re = pthread_setschedparam(pthread_self(), RT_POLICY, &sch_params);
		LogErr("pthread_setschedparam(priority=%d) return %d\n", priority, re);

		int policy;
		if (!pthread_getschedparam(pthread_self(), &policy, &sch_params)) {
			Log("Current thread policy=%d priority=%d\n", policy, sch_params.sched_priority);
		}
#endif
	}

	void HQ_APICALL SetCurrentThreadRTPriority() {
		SetCurrentThreadPriority(GetThreadDefaultRTPriority());
	}

	int HQ_APICALL GetThreadDefaultRTPriority() {
#ifdef WIN32
		// TODO
		return THREAD_PRIORITY_NORMAL;
#else
		int max_prio = sched_get_priority_max(RT_POLICY);
		int min_prio = sched_get_priority_min(RT_POLICY);

		if (min_prio < 0)
			min_prio = 0;
		if (max_prio < 0)
			max_prio = 0;

		return (min_prio + max_prio) / 2;
#endif
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
#	else
		fputs(buffer, stdout);
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
#	else
		fputs(buffer, stdout);
#	endif

#else
		vfprintf(stderr, format, arg);
#endif
	}
}