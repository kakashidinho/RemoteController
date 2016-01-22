#ifndef REMOTE_COMMON_H
#define REMOTE_COMMON_H

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

namespace HQRemote {
	void SetCurrentThreadName(const char* threadName);
}

#endif
