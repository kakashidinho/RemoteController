#include "Engine.h"
#include "ImgCompressor.h"

#include <assert.h>
#include <fstream>
#include <sstream>

#define DEBUG_CAPTURED_FRAMES 0
#define MAX_PENDING_FRAMES 60

#define DEFAULT_NUM_COMPRESS_THREADS 4

#define DEFAULT_FRAME_SEND_INTERVAL (1 / 30.0)

#ifndef max
#	define max(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#	define min(a,b) ((a) < (b) ? (a) : (b))
#endif

namespace HQRemote {

	/*------------Engine -----------*/
	Engine::Engine(int port,
				   std::shared_ptr<IFrameCapturer> frameCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize)
	: Engine(std::make_shared<BaseUnreliableSocketHandler>(port), frameCapturer, imgCompressor, frameBundleSize)
	{
		
	} 
	Engine::Engine(std::shared_ptr<IConnectionHandler> connHandler,
				   std::shared_ptr<IFrameCapturer> frameCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize)
		: m_connHandler(connHandler), m_frameCapturer(frameCapturer), m_imgCompressor(imgCompressor),
			m_processedCapturedFrames(0), m_lastSentFrameId(0), m_sendFrame(false),
			m_frameBundleSize(frameBundleSize), m_lastCapturedFrameTime64(0),
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

		m_running = true;

		//start background thread to send compressed frame to remote side
		m_frameSendingThread = std::unique_ptr<std::thread>(new std::thread([this] {
			frameSendingProc();
		}));

		//start background threads compress captured frames
		auto numCompressThreads = max(std::thread::hardware_concurrency(), DEFAULT_NUM_COMPRESS_THREADS);
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
	}

	//capture current frame and send to remote controller
	void Engine::captureAndSendFrame() {
		auto frameRef = m_frameCapturer->beginCaptureFrame();
		if (frameRef != nullptr) {
			uint64_t time64 = getTimeCheckPoint64();
			
			if (m_lastCapturedFrameTime64 != 0)
			{
				auto curFrameInterval = getElapsedTime64(m_lastCapturedFrameTime64, time64);
				if (curFrameInterval < m_intendedFrameInterval)//skip
					return;
			
				m_frameCaptureInterval = 0.8 * m_frameCaptureInterval + 0.2 * curFrameInterval;
			}
			m_lastCapturedFrameTime64 = time64;
			
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
			m_intendedFrameInterval = event->event.frameInterval;
		default:
			//forward the event for users to process themselves
			return event;
		}
		return nullptr;
	}

	void Engine::sendHostInfo() {
		auto event = std::make_shared<PlainEvent>(HOST_INFO);
		event->event.hostInfo.width = m_frameCapturer->getFrameWidth();
		event->event.hostInfo.height = m_frameCapturer->getFrameHeight();

		m_connHandler->sendData(*event);
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
					auto bundleEvent = std::make_shared<CompressedEvents>(*bundle);
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
}