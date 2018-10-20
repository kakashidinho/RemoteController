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

#ifndef REMOTE_FRAME_CAPTURER_GL_H
#define REMOTE_FRAME_CAPTURER_GL_H

#include "FrameCapturer.h"

#ifdef WIN32
#include "GL/glew.h"
#elif defined __APPLE__
#include <OpenGL/gl.h>
#else
#include "GL/gl.h"
#endif

namespace HQRemote {
	//frame buffer capturer
	class HQREMOTE_API FrameCapturerGL: public IFrameCapturer {
	public:
		FrameCapturerGL(size_t queueSize, uint32_t frameWidth, uint32_t frameHeight);
		virtual ~FrameCapturerGL();

		virtual size_t getFrameSize() override;
		virtual unsigned int getNumColorChannels() override;
	protected:
		size_t getFrameSizeImpl();
		unsigned int getNumColorChannelsImpl();

		//capture current frame
		virtual void captureFrameImpl(unsigned char * prevFrameDataToCopy) override;

		GLuint *m_pboQueue;
	};
}


#endif
