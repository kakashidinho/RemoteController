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

		uint32_t getFrameWidth() const { return m_frameWidth; }
		uint32_t getFrameHeight() const { return m_frameHeight; }

		//capture current frame.
		ConstDataRef beginCaptureFrame();
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
