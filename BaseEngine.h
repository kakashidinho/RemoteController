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

		float getSendRate() const {
			return m_connHandler->getSendRate();
		}

		std::shared_ptr<const CString> getDesc() const {
			return m_connHandler->getDesc();
		}

		void setTag(size_t tag) {
			m_connHandler->setTag(tag);
		}

		size_t getTag() {
			return m_connHandler->getTag();
		}

		//set the description, it can be used as identifier for server discovery. Doesn't need to be unique.
		void setDesc(const char* desc) {
			m_connHandler->setDesc(desc);
		}

		std::shared_ptr<IConnectionHandler> getConnHandler() {
			return m_connHandler;
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

		void updateCapturedAudioSettingsIfNeeded(bool notifyRemoteSide);

		typedef std::list<ConstFrameEventRef> AudioQueue;

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
		std::list<ConstDataRef> m_audioRawPackets;

		uint64_t m_totalSentAudioPackets;
		bool m_totalSentAudioPacketsCounterReset;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif


#endif
