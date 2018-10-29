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

#ifndef REMOTE_ENGINE_H
#define REMOTE_ENGINE_H

#include "../BaseEngine.h"//winsock2.h should be included before windows.h
#include "../Common.h"
#include "../Event.h"
#include "../Timer.h"
#include "FrameCapturer.h"
#include "ImgCompressor.h"

#include <stdint.h>


#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API Engine: public BaseEngine {
	public:

		Engine(int port,
			   std::shared_ptr<IFrameCapturer> frameCapturer,
			   std::shared_ptr<IAudioCapturer> audioCapturer = nullptr,
			   std::shared_ptr<IImgCompressor> imgCompressor = nullptr,
			   size_t frameBundleSize = 1);
		Engine(std::shared_ptr<IConnectionHandler> connHandler,
			   std::shared_ptr<IFrameCapturer> frameCapturer,
			   std::shared_ptr<IAudioCapturer> audioCapturer = nullptr,
			   std::shared_ptr<IImgCompressor> imgCompressor = nullptr,
			   size_t frameBundleSize = 1);
		~Engine();

		//capture current frame and send to remote controller
		void captureAndSendFrame();

		virtual bool start(bool preprocessEventAsync = false) override;
		virtual void stop() override;

		void enableFrameIntervalAlternation(bool enable);

		double getFrameInterval() const { return m_intendedFrameInterval; }
	private:
		struct CapturedFrame {
			CapturedFrame(uint32_t width, uint32_t height, ConstDataRef rawFrameRef)
				: width(width), height(height), rawFrameDataRef(rawFrameRef)
			{}


			CapturedFrame(const CapturedFrame& src)
				: width(src.width), height(src.height), rawFrameDataRef(src.rawFrameDataRef)
			{}

			CapturedFrame(CapturedFrame&& src)
				: width (src.width), height(src.height), rawFrameDataRef(std::move(src.rawFrameDataRef))
			{}

			CapturedFrame& operator= (const CapturedFrame& src) {
				width = (src.width); height = (src.height); rawFrameDataRef = (src.rawFrameDataRef);
				return *this;
			}

			CapturedFrame& operator= (CapturedFrame&& src) {
				width = (src.width); height = (src.height); rawFrameDataRef = std::move(src.rawFrameDataRef);
				return *this;
			}

			uint32_t width, height;
			ConstDataRef rawFrameDataRef;
		};

		void platformConstruct();
		void platformDestruct();
		std::string platformGetWritableFolder();
		
		std::string platformGetAppName();
		
		void platformStartRecording();
		void platformRecordFrame(double t, const CapturedFrame& frame);
		void platformEndRecording();

		virtual void onDisconnected() override;
		virtual bool handleEventInternalImpl(const EventRef& event) override;
		void sendHostInfo();

		void frameCompressionProc();
		void frameBundleProc();
		void frameSendingProc();
		void videoRecordingProc();
		void frameSavingProc();
		
		void pushCompressedFrameForBundling(const FrameEventRef& frame);
		void pushFrameDataForSending(uint64_t id, const DataRef& data);
		void debugFrame(const FrameEventRef& frameEvent);
		void debugFrame(uint64_t id, const void* data, size_t size);

		std::shared_ptr<IFrameCapturer> m_frameCapturer;
		std::shared_ptr<IImgCompressor> m_imgCompressor;

		//frame compression & sending thread
		typedef std::shared_ptr<CompressedEvents::EventList> FrameBundleRef;
		std::list<CapturedFrame> m_capturedFramesForCompress;
		std::map<uint64_t, FrameBundleRef> m_incompleteFrameBundles;
		std::map<uint64_t, FrameBundleRef> m_frameBundles;
		std::map<uint64_t, DataRef> m_sendingFrames;
		std::mutex m_frameCompressLock;
		std::mutex m_frameBundleLock;
		std::mutex m_frameSendingLock;
		std::condition_variable m_frameCompressCv;
		std::condition_variable m_frameBundleCv;
		std::condition_variable m_frameSendingCv;
		std::unique_ptr<std::thread> m_frameSendingThread;
		std::vector<std::unique_ptr<std::thread> > m_frameCompressionThreads;
		std::vector<std::unique_ptr<std::thread> > m_frameBundleThreads;
		
		size_t m_frameBundleSize;
		uint64_t m_processedCapturedFrames;
		uint64_t m_lastSentFrameId;
		uint64_t m_numCapturedFrames;
		uint64_t m_firstCapturedFrameTime64;
		double m_frameCaptureInterval;
		double m_intendedFrameInterval;
		double m_nextFrameIntervalOffset;
		bool m_frameIntervalAlternation;
		std::atomic<bool> m_sendFrame;

		//video recording thread
		std::map<time_checkpoint_t, CapturedFrame, TimeCompare> m_capturedFramesForVideo;
		std::unique_ptr<std::thread> m_videoThread;
		std::mutex m_videoLock;
		std::condition_variable m_videoCv;
		bool m_videoRecording;
		
		//screenshot saving thread
		std::list<CapturedFrame> m_capturedFramesForSave;
		std::unique_ptr<std::thread> m_screenshotThread;
		std::mutex m_screenshotLock;
		std::condition_variable m_screenshotCv;
		
		//
		std::atomic<bool> m_saveNextFrame;

		//platform dependent
		struct Impl;
		Impl * m_impl;
	};
};



#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif


#endif // !REMOTE_ENGINE_H

