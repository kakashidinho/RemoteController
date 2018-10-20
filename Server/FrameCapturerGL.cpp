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

#include "FrameCapturerGL.h"

namespace HQRemote {
	FrameCapturerGL::FrameCapturerGL(size_t queueSize, uint32_t frameWidth, uint32_t frameHeight)
		:IFrameCapturer(queueSize, frameWidth, frameHeight)
	{
		m_pboQueue = new GLuint[m_queueSize];
		glGenBuffers(m_queueSize, m_pboQueue);

		int oldBoundPBO;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldBoundPBO);

		for (size_t i = 0; i < m_queueSize; ++i) {
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pboQueue[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, getFrameSizeImpl(), NULL, GL_STREAM_READ);
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, oldBoundPBO);
	}

	FrameCapturerGL::~FrameCapturerGL() {
		glDeleteBuffers(m_queueSize, m_pboQueue);
		delete[] m_pboQueue;
	}

	size_t FrameCapturerGL::getFrameSize() {
		return getFrameSizeImpl();
	}

	unsigned int FrameCapturerGL::getNumColorChannels() {
		return getNumColorChannelsImpl();
	}

	size_t FrameCapturerGL::getFrameSizeImpl() {
		return m_frameWidth * m_frameHeight * getNumColorChannelsImpl();//4 bytes per pixel
	}

	unsigned int FrameCapturerGL::getNumColorChannelsImpl() {
		return 3;
	}

	//capture current frame
	void FrameCapturerGL::captureFrameImpl(unsigned char * prevFrameDataToCopy) {
		auto pbo = m_pboQueue[m_currentFrameIdx];

		int oldPackAlignment; 
		int oldBoundPBO;
		int oldReadBuffer;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldBoundPBO);
		glGetIntegerv(GL_PACK_ALIGNMENT, &oldPackAlignment);
		glGetIntegerv(GL_READ_BUFFER, &oldReadBuffer);

		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

		//copy PBO's previous data
		if (prevFrameDataToCopy) {
			auto prevData = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
			if (prevData)
				memcpy(prevFrameDataToCopy, prevData, getFrameSizeImpl());
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}

		//copy frame buffer data to PBO
		glPixelStorei(GL_PACK_ALIGNMENT, 1);

		glReadBuffer(GL_FRONT);
		glReadPixels(0, 0, m_frameWidth, m_frameHeight, GL_RGB, GL_UNSIGNED_BYTE, 0);

		glPixelStorei(GL_PACK_ALIGNMENT, oldPackAlignment);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, oldBoundPBO);
		glReadBuffer(oldReadBuffer);

		//clear error
		auto err = glGetError();
	}

}