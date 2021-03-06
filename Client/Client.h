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

#ifndef HQ_REMOTE_CLIENT_H
#define HQ_REMOTE_CLIENT_H

#include "../BaseEngine.h"

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	class HQREMOTE_API Client: public BaseEngine {
	public:
		Client(std::shared_ptr<IConnectionHandler> connHandler, 
			float frameInterval, 
			std::shared_ptr<IAudioCapturer> audioCapturer = nullptr,
			size_t maxPendingFrames = 4);
		~Client();

		void setFrameInterval(float t);
		float getFrameInterval() const { return m_frameInterval; }

		void setMaxPendingFrames(size_t maxPendingFrames);

		//query rendered frame event
		ConstFrameEventRef getFrameEvent(uint32_t blockIfEmptyForMs = 0);
		size_t getFrameEvents(ConstFrameEventRef* frameEvents, size_t maxFrames, uint32_t blockIfEmptyForMs = 0);

		virtual bool start(bool preprocessEventAsync = true) override;
		virtual void stop() override;

		void enableFrameIntervalAlternation(bool enable);
	private:
		virtual bool handleEventInternalImpl(const EventRef& event) override;

		struct FrameInfo {
			ConstFrameEventRef frameRef;
			bool isImportant;
		};

		typedef std::map<uint64_t, FrameInfo> FrameQueue;

		std::mutex m_frameQueueLock;
		std::condition_variable m_frameQueueCv;
		FrameQueue m_frameQueue;

		float m_frameInterval;
		uint64_t m_lastRcvFrameTime64;
		uint64_t m_lastRcvFrameId;
		uint64_t m_numRcvFrames;
		size_t m_maxPendingFrames;

		bool m_frameIntervalAlternation;
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
