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
