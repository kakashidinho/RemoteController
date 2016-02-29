#include "Engine.h"
#include "ImgCompressor.h"

#include <opus.h>
#include <opus_defines.h>

#include <assert.h>
#include <fstream>
#include <sstream>

#define DEBUG_CAPTURED_FRAMES 0
#define MAX_PENDING_FRAMES 60

#define MAX_NUM_COMPRESS_THREADS 4
#define DEFAULT_NUM_COMPRESS_THREADS 2

#define DEFAULT_FRAME_SEND_INTERVAL (1 / 30.0)

#ifndef max
#	define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#	define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#define DEFAULT_AUDIO_FRAME_SIZE_MS 20
#define DEFAULT_AUDIO_FRAME_BUNDLE 3

namespace HQRemote {
	/*----- Engine::AudioEncoder ----*/
	class Engine::AudioEncoder {
	public:
		AudioEncoder(int32_t sampleRate, int numChannels)
			:m_sampleRate(sampleRate), m_numChannels(numChannels)
		{
			int error;

			m_enc = opus_encoder_create(sampleRate, numChannels, OPUS_APPLICATION_AUDIO, &error);

			if (error != OPUS_OK)
			{
				auto errorStr = opus_strerror(error);
				throw std::runtime_error(errorStr);
			}
		}

		~AudioEncoder() {
			opus_encoder_destroy(m_enc);
		}

		operator OpusEncoder* () { return m_enc; }

		int32_t getSampleRate() const { return m_sampleRate; }
		int getNumChannels() const { return m_numChannels; }
	private:
		OpusEncoder* m_enc;

		int32_t m_sampleRate;
		int m_numChannels;
	};


	/*------------Engine -----------*/
	Engine::Engine(int port,
				   std::shared_ptr<IFrameCapturer> frameCapturer,
				   std::shared_ptr<IAudioCapturer> audioCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize)
	: Engine(std::make_shared<BaseUnreliableSocketHandler>(port), frameCapturer, audioCapturer, imgCompressor, frameBundleSize)
	{
		
	} 
	Engine::Engine(std::shared_ptr<IConnectionHandler> connHandler,
				   std::shared_ptr<IFrameCapturer> frameCapturer,
				   std::shared_ptr<IAudioCapturer> audioCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize)
		: m_connHandler(connHandler), m_frameCapturer(frameCapturer), m_audioCapturer(audioCapturer), m_imgCompressor(imgCompressor),
			m_processedCapturedFrames(0), m_lastSentFrameId(0), m_sendFrame(false),
			m_frameBundleSize(frameBundleSize), m_firstCapturedFrameTime64(0), m_numCapturedFrames(0),
			m_frameCaptureInterval(0), m_intendedFrameInterval(DEFAULT_FRAME_SEND_INTERVAL),
			m_videoRecording(false), m_saveNextFrame(false)
	{
		if (m_frameCapturer == nullptr) {
			throw std::runtime_error("Null frame capturer is not allowed");
		}
		if (m_connHandler == nullptr)
		{
			throw std::runtime_error("Null connection handler is not allowed");
		}

		if (m_imgCompressor == nullptr)
		{
			m_imgCompressor = std::make_shared<JpegImgCompressor>(true, false);
		}

		platformConstruct();
	}
	Engine::~Engine() {
		stop();
		
		platformDestruct();
	}

	bool Engine::start()
	{
		stop();

		if (!m_connHandler->start())
			return false;

		m_capturedFramesForCompress.clear();
		m_capturedFramesForSave.clear();
		m_capturedFramesForVideo.clear();
		m_incompleteFrameBundles.clear();
		m_frameBundles.clear();
		m_sendingFrames.clear();
		m_audioRawPackets.clear();
		m_sentAudioPackets = 0;

		m_frameCaptureInterval = m_intendedFrameInterval;
		m_firstCapturedFrameTime64 = 0;
		m_numCapturedFrames = 0;

		m_running = true;
		m_sendFrame = false;

		//start background thread to send compressed frame to remote side
		m_frameSendingThread = std::unique_ptr<std::thread>(new std::thread([this] {
			frameSendingProc();
		}));

		//start background threads compress captured frames
		auto numCompressThreads = max(std::thread::hardware_concurrency(), DEFAULT_NUM_COMPRESS_THREADS);
		numCompressThreads = min(MAX_NUM_COMPRESS_THREADS, numCompressThreads);

		m_frameCompressionThreads.reserve(numCompressThreads);
		for (unsigned int i = 0; i < numCompressThreads; ++i) {
			auto thread = std::unique_ptr<std::thread>(new std::thread([this] {
				frameCompressionProc();
			}));

			m_frameCompressionThreads.push_back(std::move(thread));
		}

		//start background threads bundle comrpessed frame together
		if (m_frameBundleSize > 1) {
			auto numBundleThreads = min(std::thread::hardware_concurrency(), m_frameBundleSize);
			m_frameBundleThreads.reserve(numBundleThreads);
			for (unsigned int i = 0; i < numBundleThreads; ++i) {
				auto thread = std::unique_ptr<std::thread>(new std::thread([this] {
					frameBundleProc();
				}));

				m_frameBundleThreads.push_back(std::move(thread));
			}
		}//if (m_frameBundleSize > 1)

		 //start background thread to record video
		m_videoThread = std::unique_ptr<std::thread>(new std::thread([this] {
			videoRecordingProc();
		}));


		//start backgroun thread to save screenshot
		m_screenshotThread = std::unique_ptr<std::thread>(new std::thread([this] {
			frameSavingProc();
		}));

		//start background thread to process audio and send to remote side
		m_audioThread = std::unique_ptr<std::thread>(new std::thread([this] {
			audioSendingProc();
		}));

		return true;
	}

	void Engine::stop()
	{
		m_running = false;

		m_connHandler->stop();

		//wake up all threads
		{
			std::lock_guard<std::mutex> lg(m_frameSendingLock);
			m_frameCompressCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_frameBundleLock);
			m_frameBundleCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_frameCompressLock);
			m_frameSendingCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_videoLock);
			m_videoCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_screenshotLock);
			m_screenshotCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_audioLock);
			m_audioCv.notify_all();
		}

		//join with all threads
		if (m_frameSendingThread && m_frameSendingThread->joinable())
			m_frameSendingThread->join();
		m_frameSendingThread = nullptr;

		for (auto& compressThread : m_frameCompressionThreads) {
			if (compressThread->joinable())
				compressThread->join();
		}
		m_frameCompressionThreads.clear();

		for (auto& bundleThread : m_frameBundleThreads) {
			if (bundleThread->joinable())
				bundleThread->join();
		}
		m_frameBundleThreads.clear();

		if (m_videoThread && m_videoThread->joinable())
			m_videoThread->join();
		m_videoThread = nullptr;

		if (m_screenshotThread && m_screenshotThread->joinable())
			m_screenshotThread->join();
		m_screenshotThread = nullptr;

		if (m_audioThread && m_audioThread->joinable())
			m_audioThread->join();
		m_audioThread = nullptr;
	}

	//capture current frame and send to remote controller
	void Engine::captureAndSendFrame() {
		auto frameRef = m_frameCapturer->beginCaptureFrame();
		if (frameRef != nullptr) {
			uint64_t time64 = getTimeCheckPoint64();
			
			if (m_firstCapturedFrameTime64 != 0)
			{
				auto intendedElapsedTime = (m_numCapturedFrames - 0.05) * m_intendedFrameInterval;
				auto elapsed = getElapsedTime64(m_firstCapturedFrameTime64, time64);
				if (elapsed < intendedElapsedTime)//skip
					return;
			
				m_frameCaptureInterval = 0.8 * m_frameCaptureInterval + 0.2 * elapsed / (m_numCapturedFrames + 1);
			}
			else
				m_firstCapturedFrameTime64 = time64;
			
			m_numCapturedFrames++;

#if defined DEBUG || defined _DEBUG
			HQRemote::Log("captured fps: %.2f\n", 1.f / m_frameCaptureInterval);
#endif

			//send to frame compression threads
			{
				std::lock_guard<std::mutex> lg(m_frameCompressLock);
				m_capturedFramesForCompress.push_back(frameRef);
				
				if (m_capturedFramesForCompress.size() > MAX_PENDING_FRAMES)
					m_capturedFramesForCompress.pop_front();

				m_frameCompressCv.notify_all();
			}
			
			if (m_saveNextFrame)
			{
				std::lock_guard<std::mutex> lg(m_screenshotLock);
				m_capturedFramesForSave.push_back(frameRef);
				
				if (m_capturedFramesForSave.size() > MAX_PENDING_FRAMES)
					m_capturedFramesForSave.pop_front();
				
				m_screenshotCv.notify_all();
				
				m_saveNextFrame = false;
			}
			
			//send to video recording thread
			{
				std::lock_guard<std::mutex> lg(m_videoLock);
				if (m_videoRecording) {
					time_checkpoint_t time;
					convertToTimeCheckPoint(time, time64);

					m_capturedFramesForVideo.insert(std::pair<time_checkpoint_t, ConstDataRef> (time, frameRef));
					
					m_videoCv.notify_all();
				}
			}
		}
	}

	void Engine::updateAudioSettingsIfNeeded() {
		auto sampleRate = m_audioCapturer->getAudioSampleRate();
		auto numChannels = m_audioCapturer->getNumAudioChannels();

		if (m_audioEncoder && m_audioEncoder->getSampleRate() == sampleRate && m_audioEncoder->getNumChannels() == numChannels)
			return ;

		try {
			//make sure we processed all pending audio data
			std::lock_guard<std::mutex> lg(m_audioLock);

			try {
				m_audioRawPackets.clear();

				m_audioCv.notify_all();
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
		sendAudioInfo();
	}

	void Engine::captureAndSendAudio() {
		if (!m_running || !m_audioCapturer)
			return;

		updateAudioSettingsIfNeeded();

		if (!m_audioEncoder)
			return;

		auto pcmData = m_audioCapturer->beginCaptureAudio();
		if (pcmData == nullptr)
			return;

		std::lock_guard<std::mutex> lg(m_audioLock);

		try {
			if (m_audioRawPackets.size() >= MAX_PENDING_FRAMES)
				m_audioRawPackets.pop_front();

			m_audioRawPackets.push_back(pcmData);

			m_audioCv.notify_all();
		}
		catch (...) {
		}
	}

	ConstEventRef Engine::getEvent() {
		auto data = m_connHandler->receiveData();
		if (data == nullptr)
			return nullptr;

		auto event = deserializeEvent(std::move(data));
		if (event != nullptr) {
			return handleEventInternal(event);
		}

		return event;
	}

	void Engine::sendEvent(const ConstEventRef& event) {
		m_connHandler->sendData(*event);
	}

	void Engine::sendEventUnreliable(const ConstEventRef& event) {
		m_connHandler->sendDataUnreliable(*event);
	}

	void Engine::sendEvent(const PlainEvent& event) {
		m_connHandler->sendData(event);
	}

	void Engine::sendEventUnreliable(const PlainEvent& event)
	{
		m_connHandler->sendDataUnreliable(event);
	}

	EventRef Engine::handleEventInternal(const EventRef& event) {
		//process internal event
		switch (event->event.type) {
		case START_SEND_FRAME:
			m_sendFrame = true;
			break;
		case STOP_SEND_FRAME:
			m_sendFrame = false;
			break;
		case RECORD_START:
		{
			std::lock_guard<std::mutex> lg(m_videoLock);
			m_videoRecording = true;

			m_videoCv.notify_one();//notify video recording thread
		}
			break;
		case RECORD_END:
		{
			std::lock_guard<std::mutex> lg(m_videoLock);
			m_videoRecording = false;

			m_videoCv.notify_one();//notify video recording thread
		}
			break;
		case SCREENSHOT_CAPTURE:
			m_saveNextFrame = true;
			break;

		case HOST_INFO:
		{
			//return host's info to remote's side
			sendHostInfo();
		}
			break;
		case FRAME_INTERVAL:
			//change frame interval
			m_frameCaptureInterval = m_intendedFrameInterval = event->event.frameInterval;
			//restart frame capturing timer
			m_firstCapturedFrameTime64 = 0;
			m_numCapturedFrames = 0;
		default:
			//forward the event for users to process themselves
			return event;
		}
		return nullptr;
	}

	void Engine::sendHostInfo() {
		sendAudioInfo();

		auto event = std::make_shared<PlainEvent>(HOST_INFO);
		event->event.hostInfo.width = m_frameCapturer->getFrameWidth();
		event->event.hostInfo.height = m_frameCapturer->getFrameHeight();

		m_connHandler->sendData(*event);
	}

	void Engine::sendAudioInfo() {
		if (!m_audioCapturer)
			return;

		auto sampleRate = m_audioCapturer->getAudioSampleRate();
		auto numChannels = m_audioCapturer->getNumAudioChannels();

		//notify remote side about the audio settings
		PlainEvent event(AUDIO_STREAM_INFO);
		event.event.audioStreamInfo.sampleRate = sampleRate;
		event.event.audioStreamInfo.numChannels = numChannels;
		event.event.audioStreamInfo.framesBundleSize = DEFAULT_AUDIO_FRAME_BUNDLE;
		event.event.audioStreamInfo.frameSizeMs = DEFAULT_AUDIO_FRAME_SIZE_MS;

		sendEvent(event);
	}

	void Engine::frameCompressionProc() {
		SetCurrentThreadName("frameCompressionThread");
		
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_frameCompressLock);

			//wait until we have at least one captured frame
			m_frameCompressCv.wait(lk, [this] {return !(m_running && m_capturedFramesForCompress.size() == 0); });

			if (m_capturedFramesForCompress.size() > 0) {
				auto frame = m_capturedFramesForCompress.front();
				m_capturedFramesForCompress.pop_front();
				auto frameId = ++m_processedCapturedFrames;
				lk.unlock();

				auto compressedFrame = m_imgCompressor->compress(
													 frame,
													 frameId,
													 m_frameCapturer->getFrameWidth(),
													 m_frameCapturer->getFrameHeight(),
													 m_frameCapturer->getNumColorChannels());

				if (compressedFrame != nullptr) {
					//convert to frame event
					auto frameEvent = std::make_shared<FrameEvent>((ConstDataRef)compressedFrame, frameId);

					if (m_frameBundleSize <= 1)
					{
						//send to frame sending thread
						pushFrameDataForSending(frameId, *frameEvent);
					}
					else {
						//send to frame bundling thread
						pushCompressedFrameForBundling(frameEvent);
					}
				}//if (compressedFrame != nullptr)
			}//if (m_capturedFramesForCompress.size() > 0)
		}//while (m_running)
	}
	
	void Engine::frameBundleProc() {
		SetCurrentThreadName("frameBundleThread");
		
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_frameBundleLock);
			
			//wait until we have at least one compressed frame
			m_frameBundleCv.wait(lk, [this] {return !(m_running && m_frameBundles.size() == 0); });
			
			if (m_frameBundles.size() > 0) {
				auto bundleIte = m_frameBundles.begin();
				auto bundleId = bundleIte->first;
				auto bundle = bundleIte->second;
				m_frameBundles.erase(bundleIte);
				lk.unlock();
				
				if (m_sendFrame) {
					auto bundleEvent = std::make_shared<CompressedEvents>(0, *bundle);
					if (bundleEvent->event.type == COMPRESSED_EVENTS)
					{
						//send to frame sending thread
						pushFrameDataForSending(bundleId, *bundleEvent);
					}
				}//if (m_sendFrame)
				
#if DEBUG_CAPTURED_FRAMES > 0
				for (auto &frame: *bundle)
				{
					debugFrame(frame);
				}//if (frameId > m_lastSentFrameId)
#endif//#if DEBUG_CAPTURED_FRAMES > 0
			}//if (m_frameBundles.size() > 0)
		}//while (m_running)
	}

	void Engine::frameSendingProc() {
		SetCurrentThreadName("frameSendingThread");
		
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_frameSendingLock);

			//wait until we have at least one frame data available for sending
			m_frameSendingCv.wait(lk, [this] {return !(m_running && m_sendingFrames.size() == 0); });

			if (m_sendingFrames.size() > 0) {
				auto frameIte = m_sendingFrames.begin();
				auto frameId = frameIte->first;
				auto frame = frameIte->second;
				m_sendingFrames.erase(frameIte);
				lk.unlock();

				if (frameId > m_lastSentFrameId)//ignore lower id frame (it may be because the compression thead was too slow to produce the frame)
				{
					if (m_sendFrame)
					{
						if (m_frameBundleSize > 1)
						{
							//send frame interval
							PlainEvent frameIntervalEvent(FRAME_INTERVAL);
							frameIntervalEvent.event.frameInterval = m_frameCaptureInterval;
							m_connHandler->sendDataUnreliable(frameIntervalEvent);
						}
						
						m_connHandler->sendDataUnreliable(frame);

						m_lastSentFrameId = frameId;
					}//if (m_sendFrame)
					
#if DEBUG_CAPTURED_FRAMES > 0
					if (m_frameBundleSize <= 1)//no frame bundle, so we can debug individual frame here
						debugFrame(frameId, frame->data(), frame->size());
#endif//#if DEBUG_CAPTURED_FRAMES > 0
				}//if (frameId > m_lastSentFrameId)
			}//if (m_sendingFrames() > 0)
		}//while (m_running)
	}
	
	void Engine::pushCompressedFrameForBundling(const FrameEventRef& frame) {
		const auto bundleId = (frame->event.renderedFrameData.frameId - 1) / m_frameBundleSize + 1;
		const auto maxBundles = MAX_PENDING_FRAMES / m_frameBundleSize;
		
		m_frameBundleLock.lock();
		auto bundleIte = m_incompleteFrameBundles.find(bundleId);
		FrameBundleRef bundle;
		if (bundleIte == m_incompleteFrameBundles.end())
		{
			if (m_incompleteFrameBundles.size() >= maxBundles)
			{
				//too many pending bundles. remove the first one
				auto ite = m_incompleteFrameBundles.begin();
				m_incompleteFrameBundles.erase(ite);
			}
			
			//create new bundle
			bundle = std::make_shared<CompressedEvents::EventList>();
			auto re = m_incompleteFrameBundles.insert(std::pair<uint64_t, FrameBundleRef>(bundleId, bundle));
			if (re.second == false)
				return;
			bundleIte = re.first;
		}
		
		bundle = bundleIte->second;
		
		//insert frame to bundle
		bundle->push_back(frame);
		
		if (bundle->size() == m_frameBundleSize)//bundle is full
		{
			//transfer bundle from incomplete list to complete list
			if (m_frameBundles.size() >= maxBundles)
			{
				//too many pending bundles. remove the first one
				auto ite = m_frameBundles.begin();
				m_frameBundles.erase(ite);
			}
			
			m_frameBundles.insert(std::pair<uint64_t, FrameBundleRef>(bundleId, bundle));
			m_incompleteFrameBundles.erase(bundleIte);
			
			//notify frame bundling thread
			m_frameBundleCv.notify_one();
		}
		m_frameBundleLock.unlock();
	}
	
	void Engine::pushFrameDataForSending(uint64_t id, const DataRef& data) {
		m_frameSendingLock.lock();
		m_sendingFrames.insert(std::pair<uint64_t, DataRef>(id, data));
		if (m_sendingFrames.size() > MAX_PENDING_FRAMES)
		{
			//too many pending frames. remove the first one
			auto frameIte = m_sendingFrames.begin();
			m_sendingFrames.erase(frameIte);
		}
		m_frameSendingCv.notify_one();
		m_frameSendingLock.unlock();
	}
	
	void Engine::debugFrame(const FrameEventRef& frameEvent)
	{
		debugFrame(frameEvent->event.renderedFrameData.frameId,
				   frameEvent->event.renderedFrameData.frameData,
				   frameEvent->event.renderedFrameData.frameSize);
	}
	
	void Engine::debugFrame(uint64_t id, const void* data, size_t size)
	{
		//write to file
		static int fileIdx = 0;
		
		std::stringstream ss;
		fileIdx = (fileIdx + 1) % DEBUG_CAPTURED_FRAMES;
		
		ss << platformGetWritableFolder() << "___debug_captured_frame_" << fileIdx << ".jpg";
		std::ofstream os(ss.str(), std::ofstream::binary);
		if (os.good()) {
			os.write((char*)data, size);
			os.close();
		}
	}
	
	/*--------- video recording thread ----*/
	void Engine::videoRecordingProc() {
		SetCurrentThreadName("videoRecordingThread");
		bool videoRecording = false;
		bool firstFrameRecorded = false;
		time_checkpoint_t startFrameTime;
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_videoLock);
			
			if (m_videoRecording) {
				if (!videoRecording) {
					//start recording
					videoRecording = true;
					
					platformStartRecording();
				}
				
				//wait until we have at least one captured frame
				m_videoCv.wait(lk, [this] {return !m_running || m_capturedFramesForVideo.size() > 0 || !m_videoRecording; });
				
				if (m_capturedFramesForVideo.size()) {
					auto frameIte = m_capturedFramesForVideo.begin();
					auto frameTime = frameIte->first;
					auto frame = frameIte->second;
					m_capturedFramesForVideo.erase(frameIte);
					lk.unlock();
					
					if (!firstFrameRecorded)
					{
						startFrameTime = frameTime;
						firstFrameRecorded = true;
					}
					
					auto dt = getElapsedTime(startFrameTime, frameTime);
					
					platformRecordFrame(dt, frame);
				}//if (m_capturedFramesForVideo.size())
				
			}//if (m_videoRecording)
			else {
				if (videoRecording) {
					//process the remaining frames
					if (m_capturedFramesForVideo.size()) {
						auto remainFrames = m_capturedFramesForVideo;
						m_capturedFramesForVideo.clear();
						lk.unlock();
						
						for (auto& frameIte: remainFrames) {
							auto frameTime = frameIte.first;
							auto frame = frameIte.second;
							
							if (!firstFrameRecorded)
							{
								startFrameTime = frameTime;
								firstFrameRecorded = true;
							}
							
							auto dt = getElapsedTime(startFrameTime, frameTime);
							
							platformRecordFrame(dt, frame);
						}
					}//if (m_capturedFramesForVideo.size())
					
					videoRecording = false;
					firstFrameRecorded = false;
					
					platformEndRecording();
				}
				
				//wait until we start recording
				if (!lk)
					lk.lock();
				m_videoCv.wait(lk, [this] {return !m_running || m_videoRecording; });
			}//if (m_videoRecording)
		}// while (m_running)
	}
	
	void Engine::frameSavingProc() {
		SetCurrentThreadName("frameSavingThread");
		
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_screenshotLock);
			
			//wait until we have at least one captured frame
			m_screenshotCv.wait(lk, [this] {return !(m_running && m_capturedFramesForSave.size() == 0); });
			
			if (m_capturedFramesForSave.size() > 0) {
				auto frame = m_capturedFramesForSave.front();
				m_capturedFramesForSave.pop_front();
				lk.unlock();
				
				auto compressedFrame = convertToPng(frame,
													 m_frameCapturer->getFrameWidth(),
													 m_frameCapturer->getFrameHeight(),
													 m_frameCapturer->getNumColorChannels(),
													 false,
													 true);
				
				if (compressedFrame != nullptr) {
					//save to file
					std::stringstream ss;
					
					ss << platformGetWritableFolder() << platformGetAppName() << "-" << getCurrentTimeStr() << ".png";
					std::ofstream os(ss.str(), std::ofstream::binary);
					if (os.good()) {
						os.write((char*)compressedFrame->data(), compressedFrame->size());
						os.close();
					}
				}//if (compressedFrame != nullptr)
			}//if (m_capturedFramesForSave() > 0)
		}//while (m_running)
	}

	void Engine::audioSendingProc() {
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

#if DEFAULT_AUDIO_FRAME_BUNDLE > 1
		std::list<EventRef> packetsBundle;
#endif

		while (m_running) {
			std::unique_lock<std::mutex> lk(m_audioLock);

			//wait until we have at least one captured frame
			m_audioCv.wait(lk, [this] {return !(m_running && m_audioRawPackets.size() == 0); });

			if (m_audioRawPackets.size() > 0) {
				auto audioEncoder = m_audioEncoder;
				auto rawPacket = m_audioRawPackets.front();
				m_audioRawPackets.pop_front();
				lk.unlock();

				if (audioEncoder != nullptr && m_sendFrame) {
					auto &batchBuffer = batchBuffers[nextBatchBufferIdx];
					auto totalRawSamples = rawPacket->size() / sizeof(opus_int16);
					auto numRawSamplesPerChannel = totalRawSamples / audioEncoder->getNumChannels();
					size_t currentBatchSamplesPerChannel = batchBuffer.size() / audioEncoder->getNumChannels();
					auto raw_samples = (opus_int16*)rawPacket->data();

					//frame size ideally should be 20ms
					batchIdealSamplesPerChannel = audioEncoder->getSampleRate() * DEFAULT_AUDIO_FRAME_SIZE_MS / 1000;
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
											input, batchIdealSamplesPerChannel,
											output, outputMaxSize);

						if (packet_len > 0) {
							//send to client
							ConstDataRef packet = std::make_shared<DataSegment>(batchEncodeBuffer, 0, packet_len);
							auto packetId = m_sentAudioPackets++;
#if DEFAULT_AUDIO_FRAME_BUNDLE > 1
							auto audioPacketEvent = std::make_shared<FrameEvent>(packet, packetId, AUDIO_ENCODED_PACKET);
							packetsBundle.push_back(audioPacketEvent);
							if (packetsBundle.size() == DEFAULT_AUDIO_FRAME_BUNDLE)
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