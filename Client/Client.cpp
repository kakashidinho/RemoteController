#include "Client.h"
#include "../Timer.h"

#include <opus.h>
#include <opus_defines.h>

#include <future>

#define MAX_PENDING_FRAMES 10
#define MAX_PENDING_AUDIO_PACKETS 10

#define MAX_ADDITIONAL_PACKETS_BEFORE_PROCEED 3

#define DEFAULT_AUDIO_BUFFER_SIZE_MS 100 //ms

namespace HQRemote {
	/*----- Engine::AudioEncoder ----*/
	class Client::AudioDecoder {
	public:
		AudioDecoder(int32_t sampleRate, int numChannels)
			:m_sampleRate(sampleRate), m_numChannels(numChannels), remoteFrameSizeSeconds(0)
		{
			int error;

			m_dec = opus_decoder_create(sampleRate, numChannels, &error);

			if (error != OPUS_OK)
			{
				auto errorStr = opus_strerror(error);
				throw std::runtime_error(errorStr);
			}
		}

		~AudioDecoder() {
			opus_decoder_destroy(m_dec);
		}

		operator OpusDecoder* () { return m_dec; }

		int32_t getSampleRate() const { return m_sampleRate; }
		int getNumChannels() const { return m_numChannels; }

		double remoteFrameSizeSeconds;
	private:
		OpusDecoder* m_dec;

		int32_t m_sampleRate;
		int m_numChannels;
	};

	/*---------- Client ------------*/
	Client::Client(std::shared_ptr<IConnectionHandler> connHandler, float frameInterval)
		: m_connHandler(connHandler), m_running(false), m_frameInterval(frameInterval), m_lastRcvFrameTime64(0), m_lastRcvFrameId(0),
		m_lastDecodedAudioPacketId(0), m_totalRecvAudioPackets(0)
	{
		if (!m_connHandler)
		{
			throw std::runtime_error("Null connection handler is not allowed");
		}
	}

	Client::~Client() {
		stop();
	}

	void Client::sendEvent(const ConstEventRef& event) {
		m_connHandler->sendData(*event);
	}

	void Client::sendEventUnreliable(const ConstEventRef& event) {
		m_connHandler->sendDataUnreliable(*event);
	}

	void Client::sendEvent(const PlainEvent& event) {
		m_connHandler->sendData(event);
	}

	void Client::sendEventUnreliable(const PlainEvent& event)
	{
		m_connHandler->sendDataUnreliable(event);
	}

	bool Client::start(bool preprocessEventAsync) {
		stop();

		if (!m_connHandler->start())
			return false;

		m_running = true;

		m_lastRcvFrameTime64 = 0;
		m_lastRcvFrameId = 0;

		m_lastDecodedAudioPacketId = 0;
		m_totalRecvAudioPackets = 0;
		m_audioDecodedBufferInitSize = 0;
		m_lastQueriedAudioTime64 = 0;
		m_lastQueriedAudioPacketId = 0;

		m_eventQueue.clear();
		m_frameQueue.clear();
		m_audioEncodedPackets.clear();
		m_audioDecodedPackets.clear();


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
		m_audioThread = std::unique_ptr<std::thread>(new std::thread([this] {
			audioProcessingProc();
		}));

		return true;
	}

	void Client::stop() {
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

		//wait for all tasks to finish
		for (auto& thread : m_taskThreads) {
			if (thread->joinable())
				thread->join();
		}

		m_taskThreads.clear();

		if (m_audioThread && m_audioThread->joinable())
			m_audioThread->join();
		m_audioThread = nullptr;
	}

	void Client::tryRecvEvent(EventType eventToDiscard, bool consumeAllAvailableData) {
		DataRef data = nullptr;

		do {
			data = m_connHandler->receiveData();
			if (data != nullptr)
			{
				auto eventType = peekEventType(data);

				if (eventToDiscard != eventType) {
					runAsync([=] {
						auto _data = data;
						auto event = deserializeEvent(std::move(_data));
						if (event != nullptr) {
							handleEventInternal(event);
						}
					});
				}//if (eventToDiscard != eventType)
			}//if (data != nullptr)
		} while (consumeAllAvailableData && data != nullptr);
	}

	ConstEventRef Client::getEvent() {
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

	ConstFrameEventRef Client::getFrameEvent() {
		tryRecvEvent();

		ConstFrameEventRef event = nullptr;
		std::lock_guard<std::mutex> lg(m_frameQueueLock);

		//discard out of date frames
		FrameQueue::iterator frameIte;
		while ((frameIte = m_frameQueue.begin()) != m_frameQueue.end() && frameIte->first <= m_lastRcvFrameId)
		{
			m_frameQueue.erase(frameIte);
		}

		//check if any frame can be rendered immediately
		if (m_frameQueue.size() > 0) {
			frameIte = m_frameQueue.begin();
			auto frameId = frameIte->first;
			auto frame = frameIte->second;

			auto curTime64 = getTimeCheckPoint64();
			double elapsed = 0;
			double intentedElapsed = 0;
			if (m_lastRcvFrameTime64 != 0)
			{
				elapsed = getElapsedTime64(m_lastRcvFrameTime64, curTime64);
				intentedElapsed = (frameId - m_lastRcvFrameId) * m_frameInterval;
			}

			//found renderable frame
			if (elapsed >= intentedElapsed)
			{
				if (m_lastRcvFrameTime64 == 0)
				{
					//cache first frame's id and time
					m_lastRcvFrameTime64 = curTime64;
					m_lastRcvFrameId = frameId;
				}

				m_frameQueue.erase(frameIte);

				event = frame;
			}
		}//if (m_frameQueue.size() > 0)

		return event;
	}

	ConstFrameEventRef Client::getAudioEvent() {
		tryRecvEvent();

		std::lock_guard<std::mutex> lg(m_audioDecodedPacketsLock);

		if (m_audioDecodedPackets.size() > 0 && m_audioDecodedBufferInitSize >= DEFAULT_AUDIO_BUFFER_SIZE_MS)
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
			bool validPacket = elapsedSinceLastPacket < expectedElapsedSinceLastPacket + 0.05;

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

	void Client::pushDecodedAudioPacket(uint64_t packetId, const void* data, size_t size, float duration) {
		std::lock_guard<std::mutex> lg(m_audioDecodedPacketsLock);

		auto sizeMs = duration * 1000.f;

		try {
			if (m_audioDecodedPackets.size() >= MAX_PENDING_AUDIO_PACKETS)
			{
				m_audioDecodedPackets.pop_front();
			}

			auto audioDecodedEventRef = std::make_shared<FrameEvent>(size, packetId, AUDIO_DECODED_PACKET);
			memcpy(audioDecodedEventRef->event.renderedFrameData.frameData, data, size);

			m_audioDecodedPackets.push_back(audioDecodedEventRef);

			if (m_audioDecodedBufferInitSize < DEFAULT_AUDIO_BUFFER_SIZE_MS)
				m_audioDecodedBufferInitSize += sizeMs;
		}
		catch (...) {
		}
	}

	void Client::flushEncodedAudioPackets() {
		//clear all pending packets for decoding and pending packets from network
		std::lock_guard<std::mutex> lg(m_audioEncodedPacketsLock);
		std::lock_guard<std::mutex> lg2(m_eventLock);

		m_audioEncodedPackets.clear();

		m_audioEncodedPacketsCv.notify_all();

		tryRecvEvent(AUDIO_ENCODED_PACKET, true);
	}

	uint32_t Client::getRemoteAudioSampleRate() const {
		if (!m_audioDecoder)
			return 0;

		return m_audioDecoder->getSampleRate();
	}

	uint32_t Client::getNumRemoteAudioChannels() const {
		if (!m_audioDecoder)
			return 0;

		return m_audioDecoder->getNumChannels();
	}

	void Client::handleEventInternal(const EventRef& eventRef) {
		auto& event = eventRef->event;

		switch (event.type) {
		case RENDERED_FRAME:
		{
			std::lock_guard<std::mutex> lg(m_frameQueueLock);

			while (m_frameQueue.size() >= MAX_PENDING_FRAMES)
			{
				//discard old frames
				auto ite = m_frameQueue.begin();
				m_frameQueue.erase(ite);
			}

			try {
				m_frameQueue[event.renderedFrameData.frameId] = std::static_pointer_cast<FrameEvent>(eventRef);
			}
			catch (...) {
				//TODO
			}
		}
			break;
		case AUDIO_STREAM_INFO:
		{
			auto sampleRate = event.audioStreamInfo.sampleRate;
			auto numChannels = event.audioStreamInfo.numChannels;
			auto frameSizeMs = event.audioStreamInfo.frameSizeMs;

			try {
				//make sure we processed all pending audio data
				std::lock_guard<std::mutex> lg2(m_audioDecodedPacketsLock);//lock decoded packets queue

				m_audioDecodedPackets.clear();

				flushEncodedAudioPackets();

				//recreate new encoder
				m_audioDecoder = std::make_shared<AudioDecoder>(sampleRate, numChannels);
				m_audioDecoder->remoteFrameSizeSeconds = frameSizeMs / 1000.f;
			}
			catch (...) {
				//TODO: print error message
			}
		}
			break;//AUDIO_STREAM_INFO
		case AUDIO_ENCODED_PACKET:
		{
			std::lock_guard<std::mutex> lg(m_audioEncodedPacketsLock);

			m_totalRecvAudioPackets++;

			try {
				if (m_audioEncodedPackets.size() >= MAX_PENDING_AUDIO_PACKETS)
					m_audioEncodedPackets.erase(m_audioEncodedPackets.begin());

				m_audioEncodedPackets[event.renderedFrameData.frameId] = std::static_pointer_cast<const FrameEvent> (eventRef);

				m_audioEncodedPacketsCv.notify_all();
			}
			catch (...) {
			}
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
		default:
		{
			//generic envent is forwarded to user
			std::lock_guard<std::mutex> lg(m_eventLock);
			try {
				m_eventQueue.push_back(eventRef);
			}
			catch (...) {
				//TODO
			}
		}
			break;
		}//switch (event.type)
	}

	void Client::runAsync(std::function<void()> task) {
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

	void Client::handleAsyncTaskProc() {
		SetCurrentThreadName("Client's async task thread");

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

	void Client::audioProcessingProc() {
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

				//either this is a subsequent packet to the previous decoded packet or there are too many packets arrived since we started waiting for the subsequent packet
				if (encodedPacketId == m_lastDecodedAudioPacketId + 1 || m_lastDecodedAudioPacketId == 0 || encodedPacketId <= m_lastDecodedAudioPacketId
					|| (lastNumRecvPackets != 0 && m_totalRecvAudioPackets - lastNumRecvPackets > MAX_ADDITIONAL_PACKETS_BEFORE_PROCEED))
				{
					m_audioEncodedPackets.erase(encodedPacketIte);
					lk.unlock();

					if (audioDecoder != nullptr && encodedPacketId > m_lastDecodedAudioPacketId) {
						auto out_max_samples = max_buffer_samples / audioDecoder->getNumChannels();

						if (m_lastDecodedAudioPacketId != 0)
						{
							//decode lost packets
							int lost_samples;
							for (auto i = m_lastDecodedAudioPacketId + 1; i < encodedPacketId; ++i)
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

						m_lastDecodedAudioPacketId = encodedPacketId;
					}//if (m_audioDecoder != nullptr)

					lastNumRecvPackets = 0;
				}
				else if (lastNumRecvPackets == 0) {
					//we will wait for a while, hopefully the expected package will arrive in time
					lastNumRecvPackets = m_totalRecvAudioPackets;
				}
			}//if (m_audioRawPackets.size() > 0)
		}//while (m_running)

		delete[] buffer;
	}
}