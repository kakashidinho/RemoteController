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
