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

#include "BaseEngine.h"
#include "Timer.h"

#include <opus.h>
#include <opus_defines.h>

#include <future>

#define MAX_PENDING_RCV_AUDIO_PACKETS 6
#define MAX_ADDITIONAL_RCV_AUDIO_PACKETS_BEFORE_PROCEED 3
#define DEFAULT_RCV_AUDIO_BUFFER_SIZE_MS 100 //ms

#define MAX_PENDING_SND_AUDIO_PACKETS 30
#define DEFAULT_SND_AUDIO_FRAME_SIZE_MS 20
#define DEFAULT_SND_AUDIO_FRAME_BUNDLE 3


#ifndef max
#	define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#	define min(a,b) ((a) < (b) ? (a) : (b))
#endif

namespace HQRemote {
	template <class type>
	class OpusTypeFactory {
	};

	template <class type>
	class OpusWrapper {
	public:
		OpusWrapper(int32_t sampleRate, int numChannels)
			:m_sampleRate(sampleRate), m_numChannels(numChannels)
		{
			int error;

			m_inst = OpusTypeFactory<type>::create(sampleRate, numChannels, &error);

			if (error != OPUS_OK)
			{
				auto errorStr = opus_strerror(error);
				throw std::runtime_error(errorStr);
			}
		}

		~OpusWrapper() {
			OpusTypeFactory<type>::destroy(m_inst);
		}

		operator type* () { return m_inst; }

		int32_t getSampleRate() const { return m_sampleRate; }
		int getNumChannels() const { return m_numChannels; }
	private:
		type* m_inst;

		int32_t m_sampleRate;
		int m_numChannels;
	};

	/*----- BaseEngine::AudioDecoder ----*/
	template <>
	class OpusTypeFactory<OpusDecoder> {
	public:
		static OpusDecoder* create(opus_int32 sampleRate, int numChannels, int *error) {
			return opus_decoder_create(sampleRate, numChannels, error);
		}

		static void destroy(OpusDecoder *decoder) {
			opus_decoder_destroy(decoder);
		}
	};

	typedef OpusWrapper<OpusDecoder> BaseAudioDecoder;

	class BaseEngine::AudioDecoder: public BaseAudioDecoder {
	public:
		AudioDecoder(int32_t sampleRate, int numChannels)
			:BaseAudioDecoder(sampleRate, numChannels)
		{}

		double remoteFrameSizeSeconds;
		int32_t remoteFramesBundleSize;
	};

	/*----- BaseEngine::AudioEncoder ----*/
	template <>
	class OpusTypeFactory<OpusEncoder> {
	public:
		static OpusEncoder* create(opus_int32 sampleRate, int numChannels, int *error) {
			return opus_encoder_create(sampleRate, numChannels, OPUS_APPLICATION_AUDIO, error);
		}

		static void destroy(OpusEncoder *encoder) {
			opus_encoder_destroy(encoder);
		}
	};

	typedef OpusWrapper<OpusEncoder> BaseAudioEncoder;

	class BaseEngine::AudioEncoder: public BaseAudioEncoder {
	public:
		AudioEncoder(int32_t sampleRate, int numChannels)
			:BaseAudioEncoder(sampleRate, numChannels)
		{}

	};

	/*---------- BaseEngine ------------*/
	BaseEngine::BaseEngine(std::shared_ptr<IConnectionHandler> connHandler, std::shared_ptr<IAudioCapturer> audioCapturer)
		: m_connHandler(connHandler), m_audioCapturer(audioCapturer), 
		m_running(false), m_sendAudio(false),
		m_lastDecodedAudioPacketId(0), m_totalRecvAudioPackets(0)
	{
		if (!m_connHandler)
		{
			throw std::runtime_error("Null connection handler is not allowed");
		}

		m_connHandler->registerDelegate(this);
	}

	BaseEngine::~BaseEngine() {
		m_connHandler->unregisterDelegate(this);
	}

	void BaseEngine::sendEvent(const ConstEventRef& event) {
		m_connHandler->sendData(*event);
	}

	void BaseEngine::sendEventUnreliable(const ConstEventRef& event) {
		m_connHandler->sendDataUnreliable(*event);
	}

	void BaseEngine::sendEvent(const PlainEvent& event) {
		m_connHandler->sendData(event);
	}

	void BaseEngine::sendEventUnreliable(const PlainEvent& event)
	{
		m_connHandler->sendDataUnreliable(event);
	}

	bool BaseEngine::start(bool preprocessEventAsync) {
		stop();

		m_running = true;
		m_sendAudio = false;

		m_eventQueue.clear();
		m_audioEncodedPackets.clear();
		m_audioDecodedPackets.clear();

		m_audioDecodedBufferInitSize = 0;
		m_lastQueriedAudioTime64 = 0;
		m_lastQueriedAudioPacketId = 0;

		m_audioRawPackets.clear();

		if (!m_connHandler->start())
			return false;

		if (m_audioCapturer)
			updateCapturedAudioSettingsIfNeeded(false);

		if (preprocessEventAsync)
		{
			//start background threads to handle async tasks
			auto numAsyncThreads = std::thread::hardware_concurrency();
			m_taskThreads.reserve(numAsyncThreads);

			for (unsigned int i = 0; i < numAsyncThreads; ++i) {
				auto thread = std::unique_ptr<std::thread>(new std::thread([this] {
					handleAsyncTaskProc();
				}));

				m_taskThreads.push_back(std::move(thread));
			}
		}//if (preprocessEventAsync)

		 //start background thread to process audio from remote side
		m_audioProcThread = std::unique_ptr<std::thread>(new std::thread([this] {
			audioProcessingProc();
		}));

		//start background thread to poll incoming network data
		if (preprocessEventAsync) {
			m_dataPollingThread = std::unique_ptr<std::thread>(new std::thread([this] {
				dataPollingProc();
			}));
		}

		//start background thread to process audio and send to remote side
		m_audioSndThread = std::unique_ptr<std::thread>(new std::thread([this] {
			audioSendingProc();
		}));

		return true;
	}

	void BaseEngine::stop() {
		m_running = false;

		m_connHandler->stop();

		//wake up all threads
		{
			std::lock_guard<std::mutex> lg(m_taskLock);
			m_taskCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_audioEncodedPacketsLock);
			m_audioEncodedPacketsCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_audioSndLock);
			m_audioSndCv.notify_all();
		}

		//wait for all tasks to finish
		for (auto& thread : m_taskThreads) {
			if (thread->joinable())
				thread->join();
		}

		m_taskThreads.clear();

		if (m_audioProcThread && m_audioProcThread->joinable())
			m_audioProcThread->join();
		m_audioProcThread = nullptr;

		if (m_audioSndThread && m_audioSndThread->joinable())
			m_audioSndThread->join();
		m_audioSndThread = nullptr;

#ifdef DEBUG
		Log("BaseEngine::stop() waiting for data polling thread\n");
#endif
		if (m_dataPollingThread && m_dataPollingThread->joinable())
			m_dataPollingThread->join();
		m_dataPollingThread = nullptr;

#ifdef DEBUG
		Log("BaseEngine::stop() finished\n");
#endif
	}

	void BaseEngine::onConnected() {
		//reset counters
		std::unique_lock<std::mutex> rcv_packet_lg(m_audioEncodedPacketsLock);//guard m_totalRecvAudioPackets && m_totalRecvAudioPackets 
		std::unique_lock<std::mutex> snd_packet_lg(m_audioSndLock);//guard m_totalSentAudioPacketsCounterReset

		m_lastDecodedAudioPacketId = 0;

		m_totalRecvAudioPackets = 0;

		m_totalSentAudioPacketsCounterReset = true;
	}

	void BaseEngine::tryRecvEvent(EventType eventToDiscard, bool consumeAllAvailableData) {
		DataRef data = nullptr;
		bool isReliable;
		do {
			data = m_connHandler->receiveData(isReliable);
			if (data != nullptr)
			{
				handleEventInternal(data, isReliable, eventToDiscard);
			}//if (data != nullptr)
		} while (consumeAllAvailableData && data != nullptr);
	}

	ConstEventRef BaseEngine::getEvent() {
		if (m_dataPollingThread == nullptr)//if we don't have dedicated polling thread then retrieve the event directly here
			tryRecvEvent();

		ConstEventRef event = nullptr;
		std::lock_guard<std::mutex> lg(m_eventLock);

		//query generic event
		if (m_eventQueue.size() > 0)
		{
			event = m_eventQueue.front();
			m_eventQueue.pop_front();
		}

		return event;
	}

	ConstFrameEventRef BaseEngine::getAudioEvent() {
		if (m_dataPollingThread == nullptr)
			tryRecvEvent();

		std::lock_guard<std::mutex> lg(m_audioDecodedPacketsLock);

		if (m_audioDecodedPackets.size() > 0 && m_audioDecodedBufferInitSize >= DEFAULT_RCV_AUDIO_BUFFER_SIZE_MS)
		{
			double elapsedSinceLastPacket = 0;
			double expectedElapsedSinceLastPacket = 0;
			auto curTime64 = getTimeCheckPoint64();
			auto packet = m_audioDecodedPackets.front();
			auto packetId = packet->event.renderedFrameData.frameId;

			m_audioDecodedPackets.pop_front();

			if (m_lastQueriedAudioTime64 == 0)
			{
				m_lastQueriedAudioTime64 = curTime64;
				m_lastQueriedAudioPacketId = packetId;
			}
			else {
				elapsedSinceLastPacket = getElapsedTime64(m_lastQueriedAudioTime64, curTime64);
				expectedElapsedSinceLastPacket = (packetId - m_lastQueriedAudioPacketId) * m_audioDecoder->remoteFrameSizeSeconds;
			}

			//skip packets if it arrived too late
			bool validPacket = elapsedSinceLastPacket < expectedElapsedSinceLastPacket + 0.01;

			if (!validPacket) {
				//reset packet's counters
				m_lastQueriedAudioTime64 = 0;
				m_audioDecodedBufferInitSize = 0;

				m_audioDecodedPackets.clear();

				flushEncodedAudioPackets();

				return nullptr;
			}

			return packet;
		}

		return nullptr;
	}

	void BaseEngine::pushDecodedAudioPacket(uint64_t packetId, const void* data, size_t size, float duration) {
		std::lock_guard<std::mutex> lg(m_audioDecodedPacketsLock);

		auto sizeMs = duration * 1000.f;

		try {
			if (m_audioDecodedPackets.size() >= MAX_PENDING_RCV_AUDIO_PACKETS)
			{
				m_audioDecodedPackets.pop_front();
			}

			auto audioDecodedEventRef = std::make_shared<FrameEvent>(size, packetId, AUDIO_DECODED_PACKET);
			memcpy(audioDecodedEventRef->event.renderedFrameData.frameData, data, size);

			m_audioDecodedPackets.push_back(audioDecodedEventRef);

			if (m_audioDecodedBufferInitSize < DEFAULT_RCV_AUDIO_BUFFER_SIZE_MS)
				m_audioDecodedBufferInitSize += sizeMs;
		}
		catch (...) {
		}
	}

	void BaseEngine::flushEncodedAudioPackets() {
		//clear all pending packets for decoding and pending packets from network
		std::unique_lock<std::mutex> packet_lg(m_audioEncodedPacketsLock);

		if (m_audioEncodedPackets.size()) {
			TimedDataQueue::iterator ite = m_audioEncodedPackets.end();
			--ite;
			auto lastPacketId = ite->first;
			if (lastPacketId > m_lastDecodedAudioPacketId)
				m_lastDecodedAudioPacketId = lastPacketId;

			m_audioEncodedPackets.clear();

			m_audioEncodedPacketsCv.notify_all();
		}

		//prevent data polling thread from polling further data
		bool locked = m_dataPollingLock.try_lock();//TODO: use guaranteed lock instead of try_lock. perhap using connHandler to wake up the polling thread
		packet_lg.unlock();
		
		tryRecvEvent(AUDIO_ENCODED_PACKET, true);
		if (locked)
			m_dataPollingLock.unlock();
	}

	uint32_t BaseEngine::getRemoteAudioSampleRate() const {
		if (!m_audioDecoder)
			return 0;

		return m_audioDecoder->getSampleRate();
	}

	uint32_t BaseEngine::getNumRemoteAudioChannels() const {
		if (!m_audioDecoder)
			return 0;

		return m_audioDecoder->getNumChannels();
	}

	void BaseEngine::updateCapturedAudioSettingsIfNeeded(bool notifyRemoteSide) {
		auto sampleRate = m_audioCapturer->getAudioSampleRate();
		auto numChannels = m_audioCapturer->getNumAudioChannels();

		if (m_audioEncoder && m_audioEncoder->getSampleRate() == sampleRate && m_audioEncoder->getNumChannels() == numChannels)
			return;

		try {
			//make sure we processed all pending audio data
			std::lock_guard<std::mutex> lg(m_audioSndLock);

			try {
				m_audioRawPackets.clear();

				m_audioSndCv.notify_all();
			}
			catch (...) {
			}

			//recreate new encoder
			m_audioEncoder = std::make_shared<AudioEncoder>(sampleRate, numChannels);
		}
		catch (...) {
			//TODO: print message
			return;
		}


		//notify remote side about the audio settings
		if (notifyRemoteSide)
			sendCapturedAudioInfo();
	}

	void BaseEngine::sendCapturedAudioInfo() {
		if (!m_audioCapturer)
			return;

		auto sampleRate = m_audioCapturer->getAudioSampleRate();
		auto numChannels = m_audioCapturer->getNumAudioChannels();

		//notify remote side about the audio settings
		PlainEvent event(AUDIO_STREAM_INFO);
		event.event.audioStreamInfo.sampleRate = sampleRate;
		event.event.audioStreamInfo.numChannels = numChannels;
		event.event.audioStreamInfo.framesBundleSize = DEFAULT_SND_AUDIO_FRAME_BUNDLE;
		event.event.audioStreamInfo.frameSizeMs = DEFAULT_SND_AUDIO_FRAME_SIZE_MS;

		sendEvent(event);
	}

	void BaseEngine::captureAndSendAudio() {
		if (!m_running || !m_audioCapturer)
			return;

		updateCapturedAudioSettingsIfNeeded(true);

		if (!m_audioEncoder)
			return;

		auto pcmData = m_audioCapturer->beginCaptureAudio();
		if (pcmData == nullptr)
			return;

		std::lock_guard<std::mutex> lg(m_audioSndLock);

		try {
			if (m_audioRawPackets.size() >= MAX_PENDING_SND_AUDIO_PACKETS)
				m_audioRawPackets.pop_front();

			m_audioRawPackets.push_back(pcmData);

			m_audioSndCv.notify_all();
		}
		catch (...) {
		}
	}

	void BaseEngine::handleEventInternal(const DataRef& data, bool isReliable, EventType eventToDiscard) {
		auto eventType = peekEventType(data);

		if (eventToDiscard != eventType) {
			auto handler = [=] {
				auto _data = data;
				auto event = deserializeEvent(std::move(_data));
				if (event != nullptr) {
					handleEventInternal(event);
				}
			};

			//if this is reliable event, we should handle it immediately to retain the order of arrival
			if (isReliable)
				handler();
			else
				runAsync(handler);
		}//if (eventToDiscard != eventType)
	}

	void BaseEngine::handleEventInternal(const EventRef& eventRef) {
		if (handleEventInternalImpl(eventRef))
			return;

		auto& event = eventRef->event;

		switch (event.type) {
		case AUDIO_STREAM_INFO:
		{
			auto sampleRate = event.audioStreamInfo.sampleRate;
			auto numChannels = event.audioStreamInfo.numChannels;
			auto frameSizeMs = event.audioStreamInfo.frameSizeMs;
			auto frameSizeSeconds = frameSizeMs / 1000.f;
			auto framesBundleSize = event.audioStreamInfo.framesBundleSize;

			try {
				if (m_audioDecoder == nullptr ||
					m_audioDecoder->getSampleRate() != sampleRate ||
					m_audioDecoder->getNumChannels() != numChannels ||
					m_audioDecoder->remoteFrameSizeSeconds != frameSizeSeconds ||
					m_audioDecoder->remoteFramesBundleSize != framesBundleSize)
				{
					//make sure we processed all pending audio data
					{
						std::lock_guard<std::mutex> lg2(m_audioDecodedPacketsLock);//lock decoded packets queue

						m_audioDecodedPackets.clear();
					}
					
					flushEncodedAudioPackets();

					//recreate new decoder
					m_audioDecoder = std::make_shared<AudioDecoder>(sampleRate, numChannels);
					m_audioDecoder->remoteFrameSizeSeconds = frameSizeSeconds;
					m_audioDecoder->remoteFramesBundleSize = framesBundleSize;
				}
			}
			catch (...) {
				//TODO: print error message
			}

			//forward the event to user
			pushEvent(eventRef);
		}
		break;//AUDIO_STREAM_INFO
		case AUDIO_ENCODED_PACKET:
		{
			std::lock_guard<std::mutex> lg(m_audioEncodedPacketsLock);

			m_totalRecvAudioPackets++;
			if (m_audioDecoder) {
				try {
					if (m_audioEncodedPackets.size() >= MAX_PENDING_RCV_AUDIO_PACKETS)
					{
						auto oldestPacketIte = m_audioEncodedPackets.begin();
						auto oldestPacketId = oldestPacketIte->first;
						m_audioEncodedPackets.erase(oldestPacketIte);

						if (oldestPacketId > m_lastDecodedAudioPacketId)
							m_lastDecodedAudioPacketId = oldestPacketId;
					}

					m_audioEncodedPackets[event.renderedFrameData.frameId] = std::static_pointer_cast<const FrameEvent> (eventRef);

					m_audioEncodedPacketsCv.notify_all();
				}
				catch (...) {
				}
			}//if (m_audioDecoder)
		}
		break;
		case COMPRESSED_EVENTS:
		{
			//this is events bundle
			auto compressedEventRef = std::static_pointer_cast<CompressedEvents>(eventRef);
			for (auto &eRef : *compressedEventRef)
			{
				handleEventInternal(eRef);
			}
		}
		break;
		case MESSAGE:
		{
			//send acknowledge message back to sender
			PlainEvent ackEvent(MESSAGE_ACK);
			ackEvent.event.messageAck.messageId = event.renderedFrameData.frameId;

			sendEvent(ackEvent);

			//forward the event to user
			pushEvent(eventRef);
		}
		break;
		default:
		{
			//generic envent is forwarded to user
			pushEvent(eventRef);
		}
		break;
		}//switch (event.type)
	}

	void BaseEngine::pushEvent(const EventRef& eventRef)
	{
		//envent is forwarded to user
		std::lock_guard<std::mutex> lg(m_eventLock);
		try {
			m_eventQueue.push_back(eventRef);
		}
		catch (...) {
			//TODO
		}
	}

	void BaseEngine::runAsync(std::function<void()> task) {
		if (m_taskThreads.size() == 0)//run immediately
		{
			task();
			return;
		}

		//push to task queue
		std::lock_guard<std::mutex> lg(m_taskLock);

		try {
			m_taskQueue.push_back(task);

			m_taskCv.notify_one();
		}
		catch (...) {

		}
	}

	void BaseEngine::handleAsyncTaskProc() {
		SetCurrentThreadName("BaseEngine's async task thread");

		while (m_running) {
			std::unique_lock<std::mutex> lk(m_taskLock);

			m_taskCv.wait(lk, [this] { return !m_running || m_taskQueue.size() > 0; });

			if (m_taskQueue.size()) {
				auto task = m_taskQueue.front();
				m_taskQueue.pop_front();
				lk.unlock();

				//run task
				task();
			}//if (m_taskQueue.size())
		}//while (m_running)
	}

	//thread that processes audio data received from remote side
	void BaseEngine::audioProcessingProc() {
		//TODO: we support only 16 bit PCM for now
		SetCurrentThreadName("audioProcessingProc");

		const int max_frame_size = 48000 * 2;//TODO: not sure about this
		const int max_buffer_samples = max_frame_size * 2;

		opus_int16* buffer;

		try {
			buffer = new opus_int16[max_buffer_samples];
		}
		catch (...) {
			//TODO: print message
			return;
		}
		uint64_t lastNumRecvPackets = 0;

		//TODO: some frame size may not work in opus_encode
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_audioEncodedPacketsLock);

			//wait until we have at least one captured frame
			m_audioEncodedPacketsCv.wait(lk, [this] {return !(m_running && m_audioEncodedPackets.size() == 0); });

			if (m_audioEncodedPackets.size() > 0) {
				auto audioDecoder = m_audioDecoder;
				auto encodedPacketIte = m_audioEncodedPackets.begin();
				auto encodedPacketId = encodedPacketIte->first;
				auto encodedPacketEventRef = encodedPacketIte->second;
				auto l_lastDecodedAudioPacketId = m_lastDecodedAudioPacketId;

				//either this is a subsequent packet to the previous decoded packet or there are too many packets arrived since we started waiting for the subsequent packet
				if (encodedPacketId == l_lastDecodedAudioPacketId + 1 || l_lastDecodedAudioPacketId == 0 || encodedPacketId <= l_lastDecodedAudioPacketId
					|| (lastNumRecvPackets != 0 && m_totalRecvAudioPackets - lastNumRecvPackets > MAX_ADDITIONAL_RCV_AUDIO_PACKETS_BEFORE_PROCEED))
				{
					m_audioEncodedPackets.erase(encodedPacketIte);
					if (encodedPacketId > m_lastDecodedAudioPacketId)
						m_lastDecodedAudioPacketId = encodedPacketId;
					lk.unlock();

					if (audioDecoder != nullptr && encodedPacketId > l_lastDecodedAudioPacketId) {
						auto out_max_samples = max_buffer_samples / audioDecoder->getNumChannels();

						if (l_lastDecodedAudioPacketId != 0)
						{
							//decode lost packets
							int lost_samples;
							for (auto i = l_lastDecodedAudioPacketId + 1; i < encodedPacketId; ++i)
							{
								opus_decoder_ctl(*audioDecoder, OPUS_GET_LAST_PACKET_DURATION(&lost_samples));
								auto samples = opus_decode(
									*audioDecoder,
									NULL, 0,
									buffer, lost_samples, 0);

#if 1//don't render lost packet
								if (samples > 0)
								{
									pushDecodedAudioPacket(i, buffer, samples * audioDecoder->getNumChannels(), (float)samples / audioDecoder->getSampleRate());
								}
#endif
							}
						}//if (m_lastDecodedAudioPacketId != 0)

						 //decode packet
						auto samples = opus_decode(
							*audioDecoder,
							(const unsigned char*)encodedPacketEventRef->event.renderedFrameData.frameData, encodedPacketEventRef->event.renderedFrameData.frameSize,
							buffer, out_max_samples, 0);

						if (samples > 0)
						{
							pushDecodedAudioPacket(encodedPacketId, buffer, samples * audioDecoder->getNumChannels() * sizeof(opus_int16), (float)samples / audioDecoder->getSampleRate());
						}
					}//if (m_audioDecoder != nullptr)

					lastNumRecvPackets = 0;
				}
				else {
					if (lastNumRecvPackets == 0) {
						//we will wait for a while, hopefully the expected package will arrive in time
						lastNumRecvPackets = m_totalRecvAudioPackets;
					}

					std::this_thread::yield();
				}
			}//if (m_audioRawPackets.size() > 0)
		}//while (m_running)

		delete[] buffer;
	}

	void BaseEngine::dataPollingProc() {
		SetCurrentThreadName("BaseEngine::dataPollingProc");

		while (m_running) {
			m_dataPollingLock.lock();
			bool isReliable;
			auto data = m_connHandler->receiveDataBlock(isReliable);
			m_dataPollingLock.unlock();
			if (data)
				handleEventInternal(data, isReliable, NO_EVENT);
		}//while (m_running)
	}

	void BaseEngine::audioSendingProc() {
		//TODO: we support only 16 bit PCM for now
		SetCurrentThreadName("audioSendingProc");

		//TODO: some frame size may not work in opus_encode
		size_t batchIdealSamplesPerChannel = 480 * 2;
		size_t batchIdealSize = 0;
		std::vector<opus_int16> batchBuffers[2];
		int nextBatchBufferIdx = 0;
		auto batchEncodeBuffer = std::make_shared<GrowableData>(batchIdealSamplesPerChannel * 2);
		batchBuffers[0].reserve(batchIdealSamplesPerChannel * 2);
		batchBuffers[1].reserve(batchIdealSamplesPerChannel * 2);

#if DEFAULT_SND_AUDIO_FRAME_BUNDLE > 1
		std::list<EventRef> packetsBundle;
#endif

		while (m_running) {
			std::unique_lock<std::mutex> lk(m_audioSndLock);

			//wait until we have at least one captured frame
			m_audioSndCv.wait(lk, [this] {return !(m_running && m_audioRawPackets.size() == 0); });

			if (m_audioRawPackets.size() > 0) {
				auto audioEncoder = m_audioEncoder;
				auto& rawData = m_audioRawPackets.front();
				auto rawPacket = rawData;
				m_audioRawPackets.pop_front();

				//reset total sent packets counter if requested
				if (m_totalSentAudioPacketsCounterReset)
				{
					m_totalSentAudioPackets = 0;
					m_totalSentAudioPacketsCounterReset = false;
				}

				lk.unlock();

				if (audioEncoder != nullptr && m_sendAudio.load(std::memory_order_relaxed)) {
					auto &batchBuffer = batchBuffers[nextBatchBufferIdx];
					auto totalRawSamples = rawPacket->size() / sizeof(opus_int16);
					auto numRawSamplesPerChannel = totalRawSamples / audioEncoder->getNumChannels();
					size_t currentBatchSamplesPerChannel = batchBuffer.size() / audioEncoder->getNumChannels();
					auto raw_samples = (opus_int16*)rawPacket->data();

					//frame size ideally should be 20ms
					batchIdealSamplesPerChannel = audioEncoder->getSampleRate() * DEFAULT_SND_AUDIO_FRAME_SIZE_MS / 1000;
					batchIdealSize = batchIdealSamplesPerChannel * sizeof(opus_int16) * audioEncoder->getNumChannels();

					if (batchIdealSize > batchEncodeBuffer->size())
					{
						batchEncodeBuffer->expand(batchIdealSize - batchEncodeBuffer->size());
					}

					try {
						batchBuffer.insert(batchBuffer.end(), raw_samples, raw_samples + totalRawSamples);

						currentBatchSamplesPerChannel += numRawSamplesPerChannel;
					}
					catch (...) {
						//TODO
					}

					size_t batchBufferOffset = 0;
					int packet_len = 0;

					while (currentBatchSamplesPerChannel >= batchIdealSamplesPerChannel && packet_len >= 0) {
						auto input = batchBuffer.data() + batchBufferOffset;
						auto output = batchEncodeBuffer->data();
						auto outputMaxSize = batchEncodeBuffer->size();

						packet_len = opus_encode(*audioEncoder,
							input, (int)batchIdealSamplesPerChannel,
							output, (opus_int32)outputMaxSize);

						if (packet_len > 0) {
							//send to client
							auto packetId = m_totalSentAudioPackets++;
							ConstDataRef packet = std::make_shared<DataSegment>(batchEncodeBuffer, 0, packet_len);
#if DEFAULT_SND_AUDIO_FRAME_BUNDLE > 1
							auto audioPacketEvent = std::make_shared<FrameEvent>(packet, packetId, AUDIO_ENCODED_PACKET);
							packetsBundle.push_back(audioPacketEvent);
							if (packetsBundle.size() == DEFAULT_SND_AUDIO_FRAME_BUNDLE)
							{
								CompressedEvents bundleEvent(-1, packetsBundle);
								sendEventUnreliable(bundleEvent);

								packetsBundle.clear();
							}
#else//DEFAULT_AUDIO_FRAME_BUNDLE > 1
							FrameEvent audioPacketEvent(packet, packetId, AUDIO_ENCODED_PACKET);
							sendEventUnreliable(audioPacketEvent);
#endif//DEFAULT_AUDIO_FRAME_BUNDLE > 1

							//store the remaining unencoded samples
							size_t encoded_samples_per_channel = opus_packet_get_samples_per_frame(output, audioEncoder->getSampleRate()) * opus_packet_get_nb_frames(output, packet_len);

							if (encoded_samples_per_channel <= currentBatchSamplesPerChannel)
								currentBatchSamplesPerChannel -= encoded_samples_per_channel;
							else
								currentBatchSamplesPerChannel = 0;

							batchBufferOffset += encoded_samples_per_channel * audioEncoder->getNumChannels();

#if 0
							char buf[1024];
							sprintf(buf, "sent packet id=%llu\n", packetId);
							OutputDebugStringA(buf);
#endif
						}//if (packet_len > 0)
					}//while (currentBatchSamplesPerChannel >= batchIdealSamplesPerChannel)

					if (currentBatchSamplesPerChannel > 0)
					{
						nextBatchBufferIdx = (nextBatchBufferIdx + 1) % 2;
						auto& nextBatchBuffer = batchBuffers[nextBatchBufferIdx];
						nextBatchBuffer.clear();
						nextBatchBuffer.insert(nextBatchBuffer.begin(), batchBuffer.begin() + batchBufferOffset, batchBuffer.end());
					}

					batchBuffer.clear();
				}//if (m_audioEncoder != nullptr)
			}//if (m_audioRawPackets.size() > 0)
		}//while (m_running)

	}
}
