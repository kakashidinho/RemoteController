#ifndef REMOTE_ENGINE_H
#define REMOTE_ENGINE_H

#include "../ConnectionHandler.h"//winsock2.h should be included before windows.h
#include "../Common.h"
#include "../Event.h"
#include "../Timer.h"
#include "FrameCapturer.h"
#include "ImgCompressor.h"

#include <stdint.h>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <condition_variable>


#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API Engine {
	public:

		Engine(int port, std::shared_ptr<IFrameCapturer> frameCapturer, std::shared_ptr<IImgCompressor> imgCompressor = nullptr);
		Engine(std::shared_ptr<IConnectionHandler> connHandler, std::shared_ptr<IFrameCapturer> frameCapturer, std::shared_ptr<IImgCompressor> imgCompressor = nullptr);
		~Engine();

		//capture current frame and send to remote controller
		void captureAndSendFrame();
		ConstEventRef getEvent();
	private:
		void platformConstruct();
		void platformDestruct();
		std::string platformGetWritableFolder();
		
		std::string platformGetAppName();
		
		void platformStartRecording();
		void platformRecordFrame(double t, ConstDataRef frame);
		void platformEndRecording();

		EventRef handleEventInternal(const EventRef& event);

		void frameCompressionProc();
		void frameSendingProc();
		void videoRecordingProc();
		void frameSavingProc();

		std::shared_ptr<IFrameCapturer> m_frameCapturer;
		std::shared_ptr<IImgCompressor> m_imgCompressor;
		std::shared_ptr<IConnectionHandler> m_connHandler;

		//frame compression & sending thread
		std::list<ConstDataRef> m_capturedFramesForCompress;
		std::map<uint64_t, DataRef> m_compressedFrames;
		std::mutex m_frameCompressLock;
		std::mutex m_frameSendingLock;
		std::condition_variable m_frameCompressCv;
		std::condition_variable m_frameSendingCv;
		std::unique_ptr<std::thread> m_frameSendingThread;
		std::vector<std::unique_ptr<std::thread> > m_frameCompressionThreads;
		
		uint64_t m_processedCapturedFrames;
		uint64_t m_lastSentFrameId;
		time_checkpoint_t m_lastSentFrameTime;
		double m_frameSendingInterval;
		std::atomic<bool> m_sendFrame;
		
		//video recording thread
		std::map<time_checkpoint_t, ConstDataRef, TimeCompare> m_capturedFramesForVideo;
		std::unique_ptr<std::thread> m_videoThread;
		std::mutex m_videoLock;
		std::condition_variable m_videoCv;
		bool m_videoRecording;
		
		//screenshot saving thread
		std::list<ConstDataRef> m_capturedFramesForSave;
		std::unique_ptr<std::thread> m_screenshotThread;
		std::mutex m_screenshotLock;
		std::condition_variable m_screenshotCv;
		
		//
		std::atomic<bool> m_saveNextFrame;
		
		std::atomic<bool> m_running;

		//platform dependent
		struct Impl;
		Impl * m_impl;
	};
};



#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif


#endif // !REMOTE_ENGINE_H

