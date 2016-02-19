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

		//query generic event
		ConstEventRef getEvent();
		ConstFrameEventRef getFrameEvent();
		ConstFrameEventRef getAudioEvent();

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

		uint32_t getRemoteAudioSampleRate() const;
		uint32_t getNumRemoteAudioChannels() const;

		bool start(bool preprocessEventAsync = true);
		void stop();
	private:
		class AudioDecoder;

		void tryRecvEvent(EventType eventToDiscard = NO_EVENT, bool consumeAllAvailableData = false);
		void handleEventInternal(const DataRef& data, EventType eventToDiscard);
		void handleEventInternal(const EventRef& event);
		void runAsync(std::function<void()> task);

		void handleAsyncTaskProc();
		void audioProcessingProc();
		void dataPollingProc();

		void pushDecodedAudioPacket(uint64_t packetId, const void* data, size_t size, float duration);
		void flushEncodedAudioPackets();

		typedef std::map<uint64_t, ConstFrameEventRef> FrameQueue;
		typedef std::list<ConstFrameEventRef> AudioQueue;

		std::shared_ptr<IConnectionHandler> m_connHandler;

		std::mutex m_eventLock;
		std::mutex m_frameQueueLock;
		std::list<ConstEventRef> m_eventQueue;
		FrameQueue m_frameQueue;

		std::unique_ptr<std::thread> m_dataPollingThread;

		std::mutex m_taskLock;
		std::condition_variable m_taskCv;
		std::list<std::function<void()> > m_taskQueue;
		std::vector<std::unique_ptr<std::thread> > m_taskThreads;

		float m_frameInterval;
		uint64_t m_lastRcvFrameTime64;
		uint64_t m_lastRcvFrameId;

		std::mutex m_audioEncodedPacketsLock;
		std::mutex m_audioDecodedPacketsLock;
		std::condition_variable m_audioEncodedPacketsCv;
		std::unique_ptr<std::thread> m_audioThread;
		std::shared_ptr<AudioDecoder> m_audioDecoder;
		FrameQueue m_audioEncodedPackets;
		AudioQueue m_audioDecodedPackets;

		uint64_t m_lastDecodedAudioPacketId;
		uint64_t m_totalRecvAudioPackets;
		uint64_t m_lastQueriedAudioTime64;
		uint64_t m_lastQueriedAudioPacketId;
		float m_audioDecodedBufferInitSize;//the size in bytes of pending decoded audio data before allowing audio rendering

		std::atomic<bool> m_running;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
