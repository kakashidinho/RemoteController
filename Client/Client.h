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

		std::atomic<bool> m_running;
	};
}

#endif
