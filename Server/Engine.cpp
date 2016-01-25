#include "Engine.h"
#include "ImgCompressor.h"

#include <assert.h>
#include <fstream>
#include <sstream>

#define DEBUG_CAPTURED_FRAMES 0
#define MAX_PENDING_FRAMES 60

#define DEFAULT_FRAME_SEND_INTERVAL (1 / 30.0)

namespace HQRemote {

	/*------------Engine -----------*/
	Engine::Engine(int port, std::shared_ptr<IFrameCapturer> frameCapturer, std::shared_ptr<IImgCompressor> imgCompressor)
	: Engine(std::make_shared<BaseUnreliableSocketHandler>(port), frameCapturer, imgCompressor)
	{
		
	} 
	Engine::Engine(std::shared_ptr<IConnectionHandler> connHandler, std::shared_ptr<IFrameCapturer> frameCapturer, std::shared_ptr<IImgCompressor> imgCompressor)
		: m_connHandler(connHandler), m_frameCapturer(frameCapturer), m_imgCompressor(imgCompressor),
			m_processedCapturedFrames(0), m_lastSentFrameId(0), m_sendFrame(false), m_frameSendingInterval(DEFAULT_FRAME_SEND_INTERVAL),
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

		m_running = true;
		
		m_connHandler->start();

		getTimeCheckPoint(m_lastSentFrameTime);

		//start background thread to send compressed frame to remote side
		m_frameSendingThread = std::unique_ptr<std::thread>(new std::thread([this] {
			frameSendingProc();
		}));

		//start background threads compress captured frames
		auto numThreads = std::thread::hardware_concurrency();
		m_frameCompressionThreads.reserve(numThreads);
		for (unsigned int i = 0; i < numThreads; ++i) {
			auto thread = std::unique_ptr<std::thread>(new std::thread([this] {
				frameCompressionProc();
			}));

			m_frameCompressionThreads.push_back(std::move(thread));
		}
		
		//start background thread to record video
		m_videoThread = std::unique_ptr<std::thread>(new std::thread([this] {
			videoRecordingProc();
		}));
		
		
		//start backgroun thread to save screenshot
		m_screenshotThread = std::unique_ptr<std::thread>(new std::thread([this] {
			frameSavingProc();
		}));
	}
	Engine::~Engine() {
		m_running = false;

		m_connHandler->stop();

		//wake up all threads
		{
			std::lock_guard<std::mutex> lg(m_frameSendingLock);
			m_frameCompressCv.notify_all();
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
		if (m_frameSendingThread->joinable())
			m_frameSendingThread->join();
		
		for (auto& compressThread : m_frameCompressionThreads) {
			if (compressThread->joinable())
				compressThread->join();
		}
		
		if (m_videoThread->joinable())
			m_videoThread->join();

		if (m_screenshotThread->joinable())
			m_screenshotThread->join();
		
		platformDestruct();
	}

	//capture current frame and send to remote controller
	void Engine::captureAndSendFrame() {
		auto frameRef = m_frameCapturer->beginCaptureFrame();
		if (frameRef != nullptr) {
			time_checkpoint_t time;
			getTimeCheckPoint(time);
			
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
			auto event = std::make_shared<PlainEvent>(HOST_INFO);
			event->event.hostInfo.width = m_frameCapturer->getFrameWidth();
			event->event.hostInfo.height = m_frameCapturer->getFrameHeight();

			m_connHandler->sendData(*event);
		}
			break;
		default:
			//forward the event for users to process themselves
			return event;
		}
		return nullptr;
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

					//send to frame sending thread
					m_frameSendingLock.lock();
					m_compressedFrames.insert(std::pair<uint64_t, DataRef>(frameId, *frameEvent));
					if (m_compressedFrames.size() > MAX_PENDING_FRAMES)
					{
						//too many pending frames. remove the first one
						auto frameIte = m_compressedFrames.begin();
						m_compressedFrames.erase(frameIte);
					}
					m_frameSendingCv.notify_one();
					m_frameSendingLock.unlock();
				}//if (compressedFrame != nullptr)
			}//if (m_capturedFramesForCompress.size() > 0)
		}//while (m_running)
	}

	void Engine::frameSendingProc() {
		SetCurrentThreadName("frameSendingThread");
		
		while (m_running) {
			std::unique_lock<std::mutex> lk(m_frameSendingLock);

			//wait until we have at least one compressed frame
			m_frameSendingCv.wait(lk, [this] {return !(m_running && m_compressedFrames.size() == 0); });

			if (m_compressedFrames.size() > 0) {
				auto frameIte = m_compressedFrames.begin();
				auto frameId = frameIte->first;
				auto frame = frameIte->second;
				m_compressedFrames.erase(frameIte);
				lk.unlock();

				if (frameId > m_lastSentFrameId)//ignore lower id frame (it may be because the compression thead was too slow to produce the frame)
				{
					time_checkpoint_t curTime;
					getTimeCheckPoint(curTime);
					if (m_sendFrame)
					{
						auto frameElapsedTime = getElapsedTime(m_lastSentFrameTime, curTime);

						if (frameElapsedTime >= m_frameSendingInterval)
						{
							m_connHandler->sendDataUnreliable(frame);

							m_lastSentFrameId = frameId;
							m_lastSentFrameTime = curTime;
						}//if (frameElapsedTime <= m_frameSendingInterval)
					}//if (m_sendFrame)

#if DEBUG_CAPTURED_FRAMES > 0
					//write to file
					static int fileIdx = 0;

					std::stringstream ss;
					fileIdx = (fileIdx + 1) % DEBUG_CAPTURED_FRAMES;

					ss << platformGetWritableFolder() << "___debug_captured_frame_" << fileIdx << ".jpg";
					std::ofstream os(ss.str(), std::ofstream::binary);
					if (os.good()) {
						os.write((char*)frame->data(), frame->size());
						os.close();
					}
#endif //if DEBUG_CAPTURED_FRAMES > 0
				}//if (frameId > m_lastSentFrameId)
			}//if (m_compressedFrames.size() > 0)
		}//while (m_running)
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