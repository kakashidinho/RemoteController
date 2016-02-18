#include "Client.h"
#include "../Timer.h"

#include <opus.h>
#include <opus_defines.h>

#include <future>

#define MAX_PENDING_FRAMES 10
#define MAX_PENDING_AUDIO_PACKETS 60

namespace HQRemote {
	/*----- Engine::AudioEncoder ----*/
	class Client::AudioDecoder {
	public:
		AudioDecoder(int32_t sampleRate, int numChannels)
			:m_sampleRate(sampleRate), m_numChannels(numChannels)
		{
			int error;

			m_dec = opus_decoder_create(sampleRate, numChannels, &error);

			if (error != OPUS_OK)
				throw std::runtime_error(opus_strerror(error));
		}

		~AudioDecoder() {
			opus_decoder_destroy(m_dec);
		}

		operator OpusDecoder* () { return m_dec; }

		int32_t getSampleRate() const { return m_sampleRate; }
		int getNumChannels() const { return m_numChannels; }
	private:
		OpusDecoder* m_dec;

		int32_t m_sampleRate;
		int m_numChannels;
	};

	/*---------- Client ------------*/
	Client::Client(std::shared_ptr<IConnectionHandler> connHandler, float frameInterval)
		: m_connHandler(connHandler), m_running(false), m_frameInterval(frameInterval), m_lastRcvFrameTime64(0), m_lastRcvFrameId(0),
		m_lastRcvAudioPacketId(0)
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

		m_lastRcvAudioPacketId = 0;

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
			std::lock_guard<std::mutex> lg(m_audioLock);
			m_audioCv.notify_all();
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

	ConstEventRef Client::getEvent() {
		auto data = m_connHandler->receiveData();
		if (data != nullptr)
		{
			runAsync([=] {
				auto _data = data;
				auto event = deserializeEvent(std::move(_data));
				if (event != nullptr) {
					handleEventInternal(event);
				}
			});
		}

		ConstEventRef event = nullptr;
		std::lock_guard<std::mutex> lg(m_eventLock);

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

		//no frame event available for rendering
		if (event == nullptr)
		{
			if (m_eventQueue.size() > 0)
			{
				event = m_eventQueue.front();
				m_eventQueue.pop_front();
			}
		}//if (event == nullptr)

		return event;
	}

	void Client::handleEventInternal(const EventRef& eventRef) {
		auto& event = eventRef->event;

		switch (event.type) {
		case RENDERED_FRAME:
		{
			std::lock_guard<std::mutex> lg(m_eventLock);

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

			try {
				//make sure we processed all pending audio data
				std::lock_guard<std::mutex> lg(m_audioLock);

				try {
					m_audioEncodedPackets.clear();

					m_audioCv.notify_all();
				}
				catch (...) {
				}

				//recreate new encoder
				m_audioDecoder = std::make_shared<AudioDecoder>(sampleRate, numChannels);
			}
			catch (...) {
				//TODO: print error message
			}
		}
			break;//AUDIO_STREAM_INFO
		case AUDIO_ENCODED_PACKET:
		{
			std::lock_guard<std::mutex> lg(m_audioLock);

			try {
				if (m_audioEncodedPackets.size() >= MAX_PENDING_AUDIO_PACKETS)
					m_audioEncodedPackets.erase(m_audioEncodedPackets.begin());

				m_audioEncodedPackets[event.renderedFrameData.frameId] = eventRef;

				m_audioCv.notify_all();
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

		//TODO: some frame size may not work in opus_encode
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_audioLock);

			//wait until we have at least one captured frame
			m_audioCv.wait(lk, [this] {return !(m_running && m_audioEncodedPackets.size() == 0); });

			if (m_audioEncodedPackets.size() > 0) {
				auto audioDecoder = m_audioDecoder;
				auto encodedPacketIte = m_audioEncodedPackets.begin();
				auto encodedPacketId = encodedPacketIte->first;
				auto encodedPacketEventRef = encodedPacketIte->second; 
				m_audioEncodedPackets.erase(encodedPacketIte);
				lk.unlock();

				if (audioDecoder != nullptr) {
					
				}//if (m_audioDecoder != nullptr)
			}//if (m_audioRawPackets.size() > 0)
		}//while (m_running)
	}
}