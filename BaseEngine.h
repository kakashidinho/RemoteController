#ifndef REMOTE_BASE_ENGINE_H
#define REMOTE_BASE_ENGINE_H

#include "ConnectionHandler.h"
#include "AudioCapturer.h"

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
	class HQREMOTE_API BaseEngine: public IConnectionHandler::Delegate {
	public:
		BaseEngine(std::shared_ptr<IConnectionHandler> connHandler, std::shared_ptr<IAudioCapturer> audioCapturer = nullptr);
		virtual ~BaseEngine();

		//query generic event
		ConstEventRef getEvent();
		//query audio event
		ConstFrameEventRef getAudioEvent();

		void sendEvent(const PlainEvent& event);
		void sendEventUnreliable(const PlainEvent& event);
		void sendEvent(const ConstEventRef& event);
		void sendEventUnreliable(const ConstEventRef& event);

		bool connected() const {
			return m_connHandler->connected();
		}

		std::shared_ptr<const CString> getConnectionInternalError() const {
			return m_connHandler->getInternalErrorMsg();
		}

		double timeSinceStart() const {
			return m_connHandler->timeSinceStart();
		}

		float getReceiveRate() const {
			return m_connHandler->getReceiveRate();
		}

		uint32_t getRemoteAudioSampleRate() const;
		uint32_t getNumRemoteAudioChannels() const;

		virtual bool start(bool preprocessEventAsync);
		virtual void stop();

		//audio streaming
		//TODO: only support 16 bit PCM, and sample rate (8000, 12000, 16000, 24000, or 48000) for now
		void captureAndSendAudio();
		void sendCapturedAudioInfo();//send captured audio info (i.e. sample rate, channels, etc) to remote side

	protected:
		typedef std::map<uint64_t, ConstFrameEventRef> TimedDataQueue;

		IConnectionHandler* getConnHandler() { return m_connHandler.get(); }
		const std::thread* getDataPollingThread() { return m_dataPollingThread.get(); }

		void tryRecvEvent(EventType eventToDiscard = NO_EVENT, bool consumeAllAvailableData = false);//try to parse & process the received data if available 
		void handleEventInternal(const DataRef& data, bool isReliable, EventType eventToDiscard);
		void handleEventInternal(const EventRef& event);
		virtual bool handleEventInternalImpl(const EventRef& event) = 0;//subclass should implement this, return false to let base class handle the event itself

		void pushEvent(const EventRef& eventRef);

		void runAsync(std::function<void()> task);

		std::atomic<bool> m_sendAudio;
		std::atomic<bool> m_running;
	private:
		class AudioDecoder;
		class AudioEncoder;

		virtual void onConnected() override;

		void handleAsyncTaskProc();
		void audioProcessingProc();
		void dataPollingProc();
		void audioSendingProc();

		void pushDecodedAudioPacket(uint64_t packetId, const void* data, size_t size, float duration);
		void flushEncodedAudioPackets();

		void updateCapturedAudioSettingsIfNeeded();

		typedef std::list<ConstFrameEventRef> AudioQueue;
		struct RawAudioData {
			RawAudioData(uint64_t _id, const ConstDataRef& _data)
				: id(_id), data(_data) 
			{}

			uint64_t id;
			ConstDataRef data;
		};

		std::shared_ptr<IConnectionHandler> m_connHandler;
		std::shared_ptr<IAudioCapturer> m_audioCapturer;

		//event queue
		std::mutex m_eventLock;
		std::list<ConstEventRef> m_eventQueue;

		//data polling thread
		std::mutex m_dataPollingLock;
		std::unique_ptr<std::thread> m_dataPollingThread;

		//async task queue
		std::mutex m_taskLock;
		std::condition_variable m_taskCv;
		std::list<std::function<void()> > m_taskQueue;
		std::vector<std::unique_ptr<std::thread> > m_taskThreads;

		//audio receiving & decoding
		std::mutex m_audioEncodedPacketsLock;
		std::mutex m_audioDecodedPacketsLock;
		std::condition_variable m_audioEncodedPacketsCv;
		std::unique_ptr<std::thread> m_audioProcThread;
		std::shared_ptr<AudioDecoder> m_audioDecoder;
		TimedDataQueue m_audioEncodedPackets;
		AudioQueue m_audioDecodedPackets;

		uint64_t m_lastDecodedAudioPacketId;
		uint64_t m_totalRecvAudioPackets;
		uint64_t m_lastQueriedAudioTime64;
		uint64_t m_lastQueriedAudioPacketId;
		float m_audioDecodedBufferInitSize;//the size in bytes of pending decoded audio data before allowing audio rendering

		//audio sending thread
		std::mutex m_audioSndLock;
		std::condition_variable m_audioSndCv;
		std::unique_ptr<std::thread> m_audioSndThread;
		std::shared_ptr<AudioEncoder> m_audioEncoder;
		std::list<RawAudioData> m_audioRawPackets;

		uint64_t m_totalSentAudioPackets;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif


#endif
