#ifndef HQ_REMOTE_CLIENT_H
#define HQ_REMOTE_CLIENT_H

#include "../ConnectionHandler.h"

#include <list>
#include <map>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <thread>
#include <vector>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API Client {
	public:
		Client(std::shared_ptr<IConnectionHandler> connHandler, float frameInterval);
		~Client();

		ConstEventRef getEvent();

		void sendEvent(const PlainEvent& event);
		void sendEventUnreliable(const PlainEvent& event);
		void sendEvent(const ConstEventRef& event);
		void sendEventUnreliable(const ConstEventRef& event);

		void setFrameInterval(float t) { m_frameInterval = t; }

		bool connected() const {
			return m_connHandler->connected();
		}

		double timeSinceStart() const {
			return m_connHandler->timeSinceStart();
		}

		float getReceiveRate() const {
			return m_connHandler->getReceiveRate();
		}

		bool start(bool preprocessEventAsync = true);
		void stop();
	private:
		class AudioDecoder;

		void handleEventInternal(const EventRef& event);
		void runAsync(std::function<void()> task);

		void handleAsyncTaskProc();

		std::shared_ptr<IConnectionHandler> m_connHandler;

		std::mutex m_eventLock;
		std::list<ConstEventRef> m_eventQueue;
		typedef std::map<uint64_t, ConstFrameEventRef> FrameQueue;
		FrameQueue m_frameQueue;

		std::mutex m_taskLock;
		std::condition_variable m_taskCv;
		std::list<std::function<void()> > m_taskQueue;
		std::vector<std::unique_ptr<std::thread> > m_taskThreads;

		float m_frameInterval;
		uint64_t m_lastRcvFrameTime64;
		uint64_t m_lastRcvFrameId;

		std::mutex m_audioLock;
		std::condition_variable m_audioCv;
		std::unique_ptr<std::thread> m_audioThread;
		FrameQueue m_audioDecodedPackets;
		FrameQueue m_audioEncodedPackets;
		std::shared_ptr<AudioDecoder> m_audioDecoder;

		uint64_t m_lastRcvAudioPacketId;

		std::atomic<bool> m_running;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
