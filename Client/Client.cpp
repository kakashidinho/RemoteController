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

#include "Client.h"
#include "../Timer.h"

#include <opus.h>
#include <opus_defines.h>

#include <future>

#define MAX_PENDING_FRAMES 4

#define FRAME_COUNTER_INTERVAL 2.0//s

namespace HQRemote {
	/*---------- Client ------------*/
	Client::Client(std::shared_ptr<IConnectionHandler> connHandler, float frameInterval, std::shared_ptr<IAudioCapturer> audioCapturer)
		: BaseEngine(connHandler, audioCapturer), m_frameInterval(frameInterval), m_lastRcvFrameTime64(0), m_lastRcvFrameId(0), m_numRcvFrames(0)
	{
	}

	Client::~Client() {
		stop();
	}

	void Client::setFrameInterval(float t) {
		m_frameInterval = t; 

		//restart frame receiving counter
		m_numRcvFrames = 0;
	}

	bool Client::start(bool preprocessEventAsync) {
		if (!BaseEngine::start(preprocessEventAsync))
			return false;

		m_lastRcvFrameTime64 = 0;
		m_lastRcvFrameId = 0;
		m_numRcvFrames = 0;

		return true;
	}

	void Client::stop() {
		BaseEngine::stop();
	}

	ConstFrameEventRef Client::getFrameEvent(uint32_t blockIfEmptyForMs) {
		if (getDataPollingThread() == nullptr)
			tryRecvEvent();

		ConstFrameEventRef event = nullptr;
		std::unique_lock<std::mutex> lk(m_frameQueueLock);

		//discard out of date frames
		FrameQueue::iterator frameIte;
		while ((frameIte = m_frameQueue.begin()) != m_frameQueue.end() && frameIte->first <= m_lastRcvFrameId)
		{
			m_frameQueue.erase(frameIte);
			
#if defined DEBUG || defined _DEBUG
			Log("---> discarded frame %lld\n", frameIte->first);
#endif
		}

		if (blockIfEmptyForMs > 0 && m_frameQueue.size() == 0) {
			m_frameQueueCv.wait_for(lk, std::chrono::milliseconds(blockIfEmptyForMs));
		}

		//check if any frame can be rendered immediately
		if (m_frameQueue.size() > 0) {
			frameIte = m_frameQueue.begin();
			auto frameId = frameIte->first;
			auto frame = frameIte->second;

			auto curTime64 = getTimeCheckPoint64();
			double elapsed = 0;
			double intentedElapsed = 0;
			if (m_numRcvFrames != 0)
			{
				elapsed = getElapsedTime64(m_lastRcvFrameTime64, curTime64);
				intentedElapsed = (m_numRcvFrames - 0.05) * m_frameInterval;
			}

			//found renderable frame
			if (elapsed >= intentedElapsed || m_numRcvFrames == 0)
			{
				if (elapsed >= FRAME_COUNTER_INTERVAL || elapsed - intentedElapsed > m_frameInterval + 0.00001)//frame arrived too late, reset frame counter
				{
					m_numRcvFrames = 0;
				}
				
				if (m_numRcvFrames == 0)
				{
					//cache first frame's time
					m_lastRcvFrameTime64 = curTime64;
				}

				m_frameQueue.erase(frameIte);

				m_lastRcvFrameId = frameId;
				m_numRcvFrames++;

				event = frame.frameRef;
				
#if defined DEBUG || defined _DEBUG
				Log("retrieved frame %lld\n", frameId);
#endif
			}
		}//if (m_frameQueue.size() > 0)

		return event;
	}

	bool Client::handleEventInternalImpl(const EventRef& eventRef) {
		auto& event = eventRef->event;
		switch (event.type) {
		case RENDERED_FRAME:
		{
			std::lock_guard<std::mutex> lg(m_frameQueueLock);

			auto trueFrameId = event.renderedFrameData.frameId & (~IMPORTANT_FRAME_ID_FLAG);

			if (m_frameQueue.size() >= MAX_PENDING_FRAMES) {
				if (m_frameQueue.begin()->first > trueFrameId) // this frame arrive too late
					break;
			}

			while (m_frameQueue.size() >= MAX_PENDING_FRAMES)
			{
				// discard old frames
				auto ite = m_frameQueue.begin();

				// try to keep important frames first
				while (ite != m_frameQueue.end() && ite->second.isImportant)
					++ite;

				if (ite == m_frameQueue.end()) // all of them are important? then drop the oldest one and subsequent unimportant ones
				{
					ite = m_frameQueue.begin();
					do {
						m_frameQueue.erase(ite++);
					} while (ite != m_frameQueue.end() && !ite->second.isImportant);
				}
				else {
					m_frameQueue.erase(ite);
				}
			}

			try {
				auto & newEntry = m_frameQueue[trueFrameId];
				newEntry.frameRef = std::static_pointer_cast<FrameEvent>(eventRef);
				newEntry.isImportant = event.renderedFrameData.frameId & IMPORTANT_FRAME_ID_FLAG;
				
				// strip the important frame id flag
				event.renderedFrameData.frameId = trueFrameId;
			}
			catch (...) {
				//TODO
			}

			m_frameQueueCv.notify_all();
		}
			break;
		case START_SEND_FRAME:
			//start sending captured audio
			m_sendAudio = true;

			break;
		case STOP_SEND_FRAME:
			//stop sending captured audio
			m_sendAudio = false;

			break;
		default:
			return false;
		}//switch (event.type)

		return true;
	}
}