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
		if (m_totalFrames < m_queueSize)
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
		if (m_queueSize)
			m_currentFrameIdx = (m_totalFrames) % m_queueSize;
		else
			m_currentFrameIdx = 0;

		return frameptr;
	}
}