#ifndef REMOTE_AUDIO_CAPTURER_H
#define REMOTE_AUDIO_CAPTURER_H

#include "Common.h"
#include "Data.h"

#include <stdint.h>
#include <memory>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API IAudioCapturer {
	public:
		virtual ~IAudioCapturer() {}
		
		virtual uint32_t getAudioSampleRate() const = 0;
		virtual uint32_t getNumAudioChannels() const = 0;

		//capture current frame. TODO: only 16 bit PCM is supported for now
		virtual ConstDataRef beginCaptureAudio() = 0;
	};
}


#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
