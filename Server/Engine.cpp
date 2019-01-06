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

#include "Engine.h"
#include "ImgCompressor.h"
#include "../Event.h"

#include <opus.h>
#include <opus_defines.h>

#include <assert.h>
#include <fstream>
#include <sstream>

#define DEBUG_CAPTURED_FRAMES 0
#define MAX_PENDING_FRAMES 4
#define FRAME_COUNTER_INTERVAL 2.0//s

#define MAX_NUM_COMPRESS_THREADS 4
#define DEFAULT_NUM_COMPRESS_THREADS 2

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
				   std::shared_ptr<IAudioCapturer> audioCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize,
				   bool supportScreenshot, bool supportVideoRecord)
	: Engine(std::make_shared<BaseUnreliableSocketHandler>(port), frameCapturer, audioCapturer, imgCompressor, frameBundleSize,
			 supportScreenshot, supportVideoRecord)
	{
		
	} 
	Engine::Engine(std::shared_ptr<IConnectionHandler> connHandler,
				   std::shared_ptr<IFrameCapturer> frameCapturer,
				   std::shared_ptr<IAudioCapturer> audioCapturer,
				   std::shared_ptr<IImgCompressor> imgCompressor,
				   size_t frameBundleSize,
				   bool supportScreenshot, bool supportVideoRecord)
		: BaseEngine(connHandler, audioCapturer), m_frameCapturer(frameCapturer), m_imgCompressor(imgCompressor),
			m_processedCapturedFrames(0), m_lastSentFrameId(0), m_sendFrame(false),
			m_frameBundleSize(frameBundleSize), m_firstCapturedFrameTime64(0), m_numCapturedFrames(0),
			m_frameCaptureInterval(0), m_intendedFrameInterval(DEFAULT_FRAME_SEND_INTERVAL),
			m_videoRecording(false), m_saveNextFrame(false),
		    m_frameIntervalAlternation(false),
			m_supportScreenshot(supportScreenshot), m_supportVideoRecord(supportVideoRecord)
	{
		if (m_frameCapturer == nullptr) {
			throw std::runtime_error("Null frame capturer is not allowed");
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

	bool Engine::start(bool preprocessEventAsync)
	{
		if (!BaseEngine::start(preprocessEventAsync))
			return false;

		m_capturedFramesForCompress.clear();
		m_capturedFramesForSave.clear();
		m_capturedFramesForVideo.clear();
		m_incompleteFrameBundles.clear();
		m_frameBundles.clear();
		m_sendingFrames.clear();

		m_frameCaptureInterval = m_intendedFrameInterval;
		m_firstCapturedFrameTime64 = 0;
		m_numCapturedFrames = 0;

		m_sendFrame = false;

		// start background threads to compress captured frames
		startFrameCompressionThreads();

		//start background thread to send compressed frame to remote side
		startFrameSendingThreadIfNeeded();

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
		if (m_supportVideoRecord) {
			m_videoThread = std::unique_ptr<std::thread>(new std::thread([this] {
				videoRecordingProc();
			}));
		}


		//start backgroun thread to save screenshot
		if (m_supportScreenshot) {
			m_screenshotThread = std::unique_ptr<std::thread>(new std::thread([this] {
				frameSavingProc();
			}));
		}

		return true;
	}

	void Engine::stop()
	{
		BaseEngine::stop();

		//wake up all threads
		{
			std::lock_guard<std::mutex> lg(m_frameBundleLock);
			m_frameBundleCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_videoLock);
			m_videoCv.notify_all();
		}
		{
			std::lock_guard<std::mutex> lg(m_screenshotLock);
			m_screenshotCv.notify_all();
		}

		stopFrameCompressionThreads();
		stopFrameSendingThread();

		//join with all threads
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
		
#ifdef DEBUG
		Log("Engine::stop() finished\n");
#endif
	}

	void Engine::setImageCompressor(std::shared_ptr<IImgCompressor> imgCompressor) {
#ifdef DEBUG
		Log("Engine::setImageCompressor()\n");
#endif
		stopFrameCompressionThreads();
		stopFrameSendingThread();

		m_imgCompressor = imgCompressor;

		// restart frame compression threads
		if (m_running) {
			startFrameCompressionThreads();
			startFrameSendingThreadIfNeeded();
		}
	}

	unsigned int Engine::startFrameCompressionThreads() {
#ifdef DEBUG
		Log("Engine::startFrameCompressionThreads()\n");
#endif
		stopFrameCompressionThreads();

		m_forceStopFrameCompression = false;

		//start background threads compress captured frames
		unsigned int numCompressThreads = 1;

		if (m_imgCompressor->canSupportMultiThreads()) {
			numCompressThreads = max(std::thread::hardware_concurrency(), DEFAULT_NUM_COMPRESS_THREADS);
			numCompressThreads = min(MAX_NUM_COMPRESS_THREADS, numCompressThreads);
		}

		m_frameCompressionThreads.reserve(numCompressThreads);
		for (unsigned int i = 0; i < numCompressThreads; ++i) {
			auto thread = std::unique_ptr<std::thread>(new std::thread([this] {
				frameCompressionProc();
			}));

			m_frameCompressionThreads.push_back(std::move(thread));
		}

		return numCompressThreads;
	}

	void Engine::stopFrameCompressionThreads() {
#ifdef DEBUG
		Log("Engine::stopFrameCompressionThreads()\n");
#endif
		m_forceStopFrameCompression = true;

		{
			std::lock_guard<std::mutex> lg(m_frameCompressLock);
			m_frameCompressCv.notify_all();
		}
		
		for (auto& compressThread : m_frameCompressionThreads) {
			if (compressThread->joinable())
				compressThread->join();
		}
		m_frameCompressionThreads.clear();
	}

	void Engine::startFrameSendingThreadIfNeeded() {
#ifdef DEBUG
		Log("Engine::startFrameSendingThreadIfNeeded()\n");
#endif

		stopFrameSendingThread();

		auto numCompressThreads = m_frameCompressionThreads.size();

		if (m_frameBundleSize > 1 || numCompressThreads > 1) {
			m_forceStopFrameSending = false;

			m_frameSendingThread = std::unique_ptr<std::thread>(new std::thread([this] {
				frameSendingProc();
			}));
#ifdef DEBUG
			Log("Engine::startFrameSendingThreadIfNeeded() --> started\n");
#endif
		}
		else {

#ifdef DEBUG
			Log("Engine::startFrameSendingThreadIfNeeded() --> no thread needed\n");
#endif
		}
	}

	void Engine::stopFrameSendingThread() {
#ifdef DEBUG
		Log("Engine::stopFrameSendingThread()\n");
#endif

		m_forceStopFrameSending = true;

		{
			std::lock_guard<std::mutex> lg(m_frameSendingLock);
			m_frameSendingCv.notify_all();
		}

		if (m_frameSendingThread && m_frameSendingThread->joinable())
			m_frameSendingThread->join();
		m_frameSendingThread = nullptr;
	}

	void Engine::lockFrameCaptureRateToFrameInterval(bool lock) {
		m_lockFrameCaptureRateToFrameInterval = lock;
	}

	void Engine::enableFrameIntervalAlternation(bool enable) {
		m_frameIntervalAlternation = enable;
	}

	//capture current frame and send to remote controller
	void Engine::captureAndSendFrame() {
		auto frameRef = m_frameCapturer->beginCaptureFrame();
		if (frameRef != nullptr) {
			uint64_t time64 = getTimeCheckPoint64();

			float intervalOffset = 0; // this is for client to know that the frame interval might not be constant

			if (m_firstCapturedFrameTime64 != 0)
			{
				if (m_frameIntervalAlternation) {
					if (m_numCapturedFrames % 2) {
						intervalOffset = 0.5f * m_intendedFrameInterval;
					}
					else {
						intervalOffset = -0.5f * m_intendedFrameInterval;
					}
				}

				auto intendedElapsedTime = (m_numCapturedFrames - 0.05) * m_intendedFrameInterval + intervalOffset;
				auto elapsed = getElapsedTime64(m_firstCapturedFrameTime64, time64);
				bool skip = m_lockFrameCaptureRateToFrameInterval && (elapsed < intendedElapsedTime); //skip
				if (skip)
					return;
			
				m_frameCaptureInterval = 0.8 * m_frameCaptureInterval + 0.2 * elapsed / (m_numCapturedFrames + 1);
				
				if (elapsed >= FRAME_COUNTER_INTERVAL
					|| (m_lockFrameCaptureRateToFrameInterval && elapsed - intendedElapsedTime > m_intendedFrameInterval + 0.00001))//frame arrived too late, reset frame counter
				{
					m_firstCapturedFrameTime64 = time64;
					m_numCapturedFrames = 0;
				}
			}
			else
				m_firstCapturedFrameTime64 = time64;
			
			m_numCapturedFrames++;

#if defined DEBUG || defined _DEBUG
			//HQRemote::Log("captured fps: %.2f\n", 1.f / m_frameCaptureInterval);
#endif

			//send to frame compression threads
			uint32_t width, height;
			m_frameCapturer->getFrameDimens(width, height);
			CapturedFrame frameInfo(width, height, intervalOffset, frameRef);

			{
				std::lock_guard<std::mutex> lg(m_frameCompressLock);
				m_capturedFramesForCompress.push_back(frameInfo);
				
				if (m_capturedFramesForCompress.size() > MAX_PENDING_FRAMES)
					m_capturedFramesForCompress.pop_front();

				m_frameCompressCv.notify_all();
			}
			
			if (m_saveNextFrame.load(std::memory_order_relaxed))
			{
				std::lock_guard<std::mutex> lg(m_screenshotLock);
				m_capturedFramesForSave.push_back(frameInfo);
				
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

					m_capturedFramesForVideo.insert(std::pair<time_checkpoint_t, CapturedFrame> (time, frameInfo));
					
					m_videoCv.notify_all();
				}
			}
		}
	}

	void Engine::onDisconnected() {
		m_sendFrame = false;

		BaseEngine::onDisconnected();
	}

	bool Engine::handleEventInternalImpl(const EventRef& event) {
		//process internal event
		switch (event->event.type) {
		case START_SEND_FRAME:
			m_sendFrame = m_sendAudio = true;
			// forward the event to user
			pushEvent(event);
			break;
		case STOP_SEND_FRAME:
			m_sendFrame = m_sendAudio = false;
			// forward the event to user
			pushEvent(event);
			break;
		case RECORD_START:
		{
			if (!m_supportVideoRecord)
				break;
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
			if (m_supportScreenshot)
				m_saveNextFrame = true;
			break;

		case HOST_INFO:
		{
			//return host's info to remote's side
			sendHostInfo();

			// forward the event to user
			pushEvent(event);
		}
			break;
		case FRAME_INTERVAL:
			//change frame interval
			m_frameCaptureInterval = m_intendedFrameInterval = event->event.frameInterval;
			//restart frame capturing timer
			m_firstCapturedFrameTime64 = 0;
			m_numCapturedFrames = 0;

			HQRemote::Log("Engine: frame interval changed to %.3f\n", m_frameCaptureInterval);

			// forward the event to user
			pushEvent(event);

			break;
		default:
			//forward the event to base class
			return false;
		}
		return true;
	}

	void Engine::sendHostInfo() {
		sendCapturedAudioInfo();

		PlainEvent eventWrapper(HOST_INFO);
		m_frameCapturer->getFrameDimens(eventWrapper.event.hostInfo.width, eventWrapper.event.hostInfo.height);

		sendEvent(eventWrapper);
	}

	void Engine::frameCompressionProc() {
		SetCurrentThreadName("frameCompressionThread");
		
		uint64_t l_compressedFrames = 0;
		uint64_t l_receivedFrames = 0;
		uint64_t frameIdForCompress;
		uint64_t frameIdForSending;
		const bool isMultiThreads = m_imgCompressor->canSupportMultiThreads();

		while (!m_forceStopFrameCompression) {
			std::unique_lock<std::mutex> lk(m_frameCompressLock);

			//wait until we have at least one captured frame
			m_frameCompressCv.wait(lk, [this] {return !(!m_forceStopFrameCompression && m_capturedFramesForCompress.size() == 0); });

			if (m_capturedFramesForCompress.size() > 0) {
				auto frame = m_capturedFramesForCompress.front();
				m_capturedFramesForCompress.pop_front();
				auto multithreadId = ++m_processedCapturedFrames; // this is synchronized between multiple compression threads

				l_receivedFrames++; // this is only local counter for this thread

				lk.unlock();

				IImgCompressor::CompressArgs info;
				info.width = frame.width;
				info.height = frame.height;
				info.numChannels = m_frameCapturer->getNumColorChannels();
				info.timeStamp = 0;
				info.outImportantFrame = false;

				if (isMultiThreads)
					frameIdForCompress = multithreadId;
				else
					frameIdForCompress = l_receivedFrames;

				auto compressedFrame = m_imgCompressor->compress2(
													 frame.rawFrameDataRef,
													 frameIdForCompress,
													 info);

				while (compressedFrame != nullptr) {
					try {
						l_compressedFrames++;

						if (isMultiThreads)
							frameIdForSending = multithreadId;
						else
							frameIdForSending = l_compressedFrames;

						if (info.outImportantFrame)
							frameIdForSending |= IMPORTANT_FRAME_ID_FLAG;

						//convert to frame event
						auto frameEvent = std::make_shared<FrameEvent>((ConstDataRef)compressedFrame, frameIdForSending);
						frameEvent->event.renderedFrameData.intervalAlternaionOffset = frame.intervalAlternaionOffset;

						compressedFrame = nullptr; // to break loop in multithreads case

						if (m_frameBundleSize <= 1)
						{
							if (isMultiThreads) {
								//send to frame sending thread
								pushFrameDataForSending(frameIdForSending, *frameEvent);
							}
							else {
								// single thread compression
								// send to network directly
								if (m_sendFrame.load(std::memory_order_relaxed))
									getConnHandler()->sendDataUnreliable(*frameEvent);
							}
						}
						else {
							//send to frame bundling thread
							pushCompressedFrameForBundling(frameEvent);
						}
					}
					catch (...) {
						// ignore
					}

					// check if compressor has anymore compressed frame output, probably from unfinished compression request
					if (!isMultiThreads)
						compressedFrame = m_imgCompressor->anyMoreCompressedOutput(info.outImportantFrame);

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
				
				if (m_sendFrame.load(std::memory_order_relaxed)) {
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
		
		while (!m_forceStopFrameSending) {
			std::unique_lock<std::mutex> lk(m_frameSendingLock);

			//wait until we have at least one frame data available for sending
			m_frameSendingCv.wait(lk, [this] {return !(!m_forceStopFrameSending && m_sendingFrames.size() == 0); });

			if (m_sendingFrames.size() > 0) {
				auto frameIte = m_sendingFrames.begin();
				auto frameId = frameIte->first;
				auto frame = frameIte->second;
				m_sendingFrames.erase(frameIte);
				lk.unlock();

				if (frameId > m_lastSentFrameId)//ignore lower id frame (it may be because the compression thead was too slow to produce the frame)
				{
					if (m_sendFrame.load(std::memory_order_relaxed))
					{
						if (m_frameBundleSize > 1)
						{
							//send frame interval
							PlainEvent frameIntervalEvent(FRAME_INTERVAL);
							frameIntervalEvent.event.frameInterval = m_frameCaptureInterval;
							sendEventUnreliable(frameIntervalEvent);
						}
						
						getConnHandler()->sendDataUnreliable(frame);

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
		const auto maxBundles = max(MAX_PENDING_FRAMES / m_frameBundleSize, 1);
		
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
#if DEBUG_CAPTURED_FRAMES
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
#endif
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
				
#ifndef HQREMOTE_NO_PNG
				auto compressedFrame = convertToPng(frame.rawFrameDataRef,
													 frame.width,
													 frame.height,
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
#endif // HQREMOTE_NO_PNG
			}//if (m_capturedFramesForSave() > 0)
		}//while (m_running)
	}
}