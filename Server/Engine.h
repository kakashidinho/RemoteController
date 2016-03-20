#ifndef REMOTE_ENGINE_H
#define REMOTE_ENGINE_H

#include "../ConnectionHandler.h"//winsock2.h should be included before windows.h
#include "../Common.h"
#include "../Event.h"
#include "../Timer.h"
#include "FrameCapturer.h"
#include "AudioCapturer.h"
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

		bool connected() const {
			return m_connHandler->connected();
		}
		std::shared_ptr<const CString> getConnectionInternalError() const{
			return m_connHandler->getInternalErrorMsg();
		}

		//capture current frame and send to remote controller
		void captureAndSendFrame();
		ConstEventRef getEvent();

		void sendEvent(const PlainEvent& event);
		void sendEventUnreliable(const PlainEvent& event);
		void sendEvent(const ConstEventRef& event);
		void sendEventUnreliable(const ConstEventRef& event);

		bool start();
		void stop();

		//audio streaming
		//TODO: only support 16 bit PCM, and sample rate (8000, 12000, 16000, 24000, or 48000) for now
		void captureAndSendAudio();
	private:
		class AudioEncoder;

		void platformConstruct();
		void platformDestruct();
		std::string platformGetWritableFolder();
		
		std::string platformGetAppName();
		
		void platformStartRecording();
		void platformRecordFrame(double t, ConstDataRef frame);
		void platformEndRecording();

		EventRef handleEventInternal(const EventRef& event);
		void sendHostInfo();
		void sendAudioInfo();

		void frameCompressionProc();
		void frameBundleProc();
		void frameSendingProc();
		void videoRecordingProc();
		void frameSavingProc();
		void audioSendingProc();
		
		void updateAudioSettingsIfNeeded();
		
		void pushCompressedFrameForBundling(const FrameEventRef& frame);
		void pushFrameDataForSending(uint64_t id, const DataRef& data);
		void debugFrame(const FrameEventRef& frameEvent);
		void debugFrame(uint64_t id, const void* data, size_t size);

		std::shared_ptr<IFrameCapturer> m_frameCapturer;
		std::shared_ptr<IAudioCapturer> m_audioCapturer;
		std::shared_ptr<IImgCompressor> m_imgCompressor;
		std::shared_ptr<IConnectionHandler> m_connHandler;

		//frame compression & sending thread
		typedef std::shared_ptr<CompressedEvents::EventList> FrameBundleRef;
		std::list<ConstDataRef> m_capturedFramesForCompress;
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
		std::atomic<bool> m_sendFrame;
		
		//audio thread
		std::mutex m_audioLock;
		std::condition_variable m_audioCv;
		std::unique_ptr<std::thread> m_audioThread;
		std::shared_ptr<AudioEncoder> m_audioEncoder;
		std::list<ConstDataRef> m_audioRawPackets;

		uint64_t m_sentAudioPackets;

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

