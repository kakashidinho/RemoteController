#ifndef REMOTE_COMMON_H
#define REMOTE_COMMON_H

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif//WIN32

#ifndef HQREMOTE_API
#	if defined WIN32
#		ifdef REMOTECONTROLLER_EXPORTS
#			define HQREMOTE_API __declspec(dllexport)
#		else
#			define HQREMOTE_API __declspec(dllimport)
#		endif
#	else
#			define HQREMOTE_API __attribute__ ((visibility("default")))
#	endif
#endif

#ifndef HQREMOTE_API_TYPEDEF
#	if defined WIN32
#		define HQREMOTE_API_TYPEDEF HQREMOTE_API
#	else
#		define HQREMOTE_API_TYPEDEF
#	endif
#endif

#ifndef HQ_FASTCALL
#	if defined WIN32
#		define HQ_FASTCALL __fastcall
#	else
#		define HQ_FASTCALL
#	endif
#endif

namespace HQRemote {
#ifdef WIN32
	typedef SSIZE_T _ssize_t;
#else
	typedef ssize_t _ssize_t;
#endif
	
	void SetCurrentThreadName(const char* threadName);
}

#endif
