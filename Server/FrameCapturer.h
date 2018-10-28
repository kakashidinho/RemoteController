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

#ifndef REMOTE_FRAME_CAPTURER_H
#define REMOTE_FRAME_CAPTURER_H

#include "../Common.h"
#include "../Data.h"
#include "../Event.h"

#include <vector>
#include <stdint.h>
#include <memory>
#include <functional>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	//frame buffer capturer
	class HQREMOTE_API IFrameCapturer {
	public:
		virtual ~IFrameCapturer();

		//size of one frame data in bytes
		virtual size_t getFrameSize() = 0;
		virtual unsigned int getNumColorChannels() = 0;

		//get number of frames the has been issued a capture
		uint64_t getTotalFrames() { return m_totalFrames; }

		HQ_DEPRECATED
		virtual uint32_t getFrameWidth() const { return m_frameWidth; }

		HQ_DEPRECATED
		virtual uint32_t getFrameHeight() const { return m_frameHeight; }

		virtual void getFrameDimens(uint32_t& width, uint32_t & height) const {
			width = m_frameWidth;
			height = m_frameHeight;
		}

		//capture current frame.
		// Note: if you override this method, then captureFrameImpl() doesn't need to be implemented.
		// Since the default implementation just call captureFrameImpl() internally.
		virtual ConstDataRef beginCaptureFrame();
	protected:
		IFrameCapturer(size_t queueSize, uint32_t frameWidth, uint32_t frameHeight);

		//capture current frame
		virtual void captureFrameImpl(unsigned char * prevFrameDataToCopy) = 0;

		uint32_t m_frameWidth;
		uint32_t m_frameHeight;

		size_t m_queueSize;
		size_t m_currentFrameIdx;
		uint64_t m_totalFrames;
	};
}


#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
