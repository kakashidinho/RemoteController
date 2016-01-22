#include "FrameCapturer.h"

namespace HQRemote {
	IFrameCapturer::IFrameCapturer(size_t queueSize, uint32_t frameWidth, uint32_t frameHeight)
		: m_queueSize(queueSize),
		m_currentFrameIdx(0),
		m_totalFrames(0),
		m_frameWidth(frameWidth),
		m_frameHeight(frameHeight)
	{
	}

	IFrameCapturer::~IFrameCapturer() {
	}

	//capture current frame
	ConstDataRef IFrameCapturer::beginCaptureFrame() {
		DataRef frameptr;
		try {
			frameptr = std::make_shared<CData>(getFrameSize());
		}
		catch (...) {
			frameptr = nullptr;
		}

		//implementation
		if (m_totalFrames < 3)
		{
			//if total frames rendered so far is less than 3 we won't read frame data directly from capturer since the frame capturer may not finish its capture of the frame yet
			if (frameptr != nullptr)
				memset(frameptr->data(), 0, getFrameSize());
			captureFrameImpl(nullptr);
		}
		else
		{
			if (frameptr != nullptr)
				captureFrameImpl(frameptr->data());
			else
				captureFrameImpl(nullptr);
		}

		m_totalFrames++;
		m_currentFrameIdx = (m_totalFrames) % m_queueSize;

		return frameptr;
	}
}