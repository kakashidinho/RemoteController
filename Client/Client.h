#ifndef HQ_REMOTE_CLIENT_H
#define HQ_REMOTE_CLIENT_H

#include "../BaseEngine.h"

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API Client: public BaseEngine {
	public:
		Client(std::shared_ptr<IConnectionHandler> connHandler, 
			float frameInterval, 
			std::shared_ptr<IAudioCapturer> audioCapturer = nullptr);
		~Client();

		void setFrameInterval(float t);
		float getFrameInterval() const { return m_frameInterval; }

		//query rendered frame event
		ConstFrameEventRef getFrameEvent();

		virtual bool start(bool preprocessEventAsync = true) override;
		virtual void stop() override;
	private:
		virtual bool handleEventInternalImpl(const EventRef& event) override;

		typedef BaseEngine::TimedDataQueue FrameQueue;

		std::mutex m_frameQueueLock;
		FrameQueue m_frameQueue;

		float m_frameInterval;
		uint64_t m_lastRcvFrameTime64;
		uint64_t m_lastRcvFrameId;
		uint64_t m_numRcvFrames;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
