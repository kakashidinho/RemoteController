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

#ifndef REMOTE_COMMON_H
#define REMOTE_COMMON_H

#ifndef WIN32
#	ifdef _WIN32
#		define WIN32
#	endif
#endif

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif//WIN32

#include <stdarg.h>

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

#ifndef HQ_APICALL
#	if defined WIN32
#		define HQ_APICALL __cdecl
#	else
#		define HQ_APICALL
#	endif
#endif

namespace HQRemote {
#ifdef WIN32
	typedef SSIZE_T _ssize_t;
#else
	typedef ssize_t _ssize_t;
#endif
	
	HQREMOTE_API void HQ_APICALL SetCurrentThreadName(const char* threadName);

	HQREMOTE_API void HQ_APICALL Log(const char* format, ...);
	HQREMOTE_API void HQ_APICALL LogErr(const char* format, ...);

	HQREMOTE_API void HQ_APICALL LogV(const char* format, va_list args);
	HQREMOTE_API void HQ_APICALL LogErrV(const char* format, va_list args);
}

#endif
