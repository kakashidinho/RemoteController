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

#ifndef REMOTE_EVENT_H
#define REMOTE_EVENT_H

#include "Data.h"

#include <stdint.h>
#include <memory>
#include <list>
#include <vector>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	typedef uint32_t EventType;

	// callback to be called when determining a custom event containing frame data (i.e. it uses renderedFrameData field or not)
	typedef bool(*EventContainsFrameDataCallback)(EventType);

	enum PredefinedEventType : uint32_t {
		TOUCH_BEGAN,
		TOUCH_MOVED,
		TOUCH_ENDED,
		TOUCH_CANCELLED,

		START_SEND_FRAME,//Tell host to start sending its captured frame & audio. Or tell client to start sending its captured audio
		STOP_SEND_FRAME,//Tell host to stop sending its captured frame & audio. Or tell client to stop sending its captured audio

		RECORD_START,
		RECORD_END,

		SCREENSHOT_CAPTURE,

		HOST_INFO,
		RENDERED_FRAME,//this uses renderedFrameData field in Event struct
		ENDPOINT_NAME,//this uses renderedFrameData field in Event struct

		FRAME_INTERVAL,

		AUDIO_STREAM_INFO,
		AUDIO_ENCODED_PACKET,//this uses renderedFrameData field in Event struct
		AUDIO_DECODED_PACKET,

		COMPRESSED_EVENTS,

		MESSAGE,//send a message. This uses renderedFrameData field in Event struct
		MESSAGE_ACK,//sent message acknowledged

		NO_EVENT,

		CONNECTED_NOTIFIFACTION = 0xfffffffe,
		DISCONNECTED_NOTIFIFACTION,

		COMPATIBLE_MODE = CONNECTED_NOTIFIFACTION - 1, // this event is not meant for direct use outside  Engine modules
	};

	const uint64_t IMPORTANT_FRAME_ID_FLAG = 0x8000000000000000; // bitwise or the frame id with this flag to indicate the frame shouldn't be dropped
	const uint64_t UNUSED_FRAME_ID_BITS = (IMPORTANT_FRAME_ID_FLAG);

	struct HQREMOTE_API Event {
		Event(EventType _type) : type(_type)
		{}
		union {
			struct {
				int32_t id;
				float x;
				float y;
			} touchData;

			struct {
				uint32_t width;
				uint32_t height;
			} hostInfo;

			//this data can be used for both frame & audio packet event
			struct {
				uint64_t frameId;
				union {
					void* frameData;
					uint64_t frameDataAddr64;
				};
				uint32_t frameSize;
				float intervalAlternaionOffset;  // this is for client to know that the frame interval might not be constant
			} renderedFrameData;
			
			struct {
				uint32_t numEvents;
			} compressedEvents;

			struct {
				int32_t sampleRate;
				int32_t numChannels;
				int32_t framesBundleSize;
				int32_t frameSizeMs;
			} audioStreamInfo;

			struct {
				uint64_t messageId;
			} messageAck;

			struct {
				uint32_t mode;
			} compatibleMode;
			
			double frameInterval;

			// generic value
			float floatValue;
			double doubleValue;
			int16_t int16Value;
			uint16_t uint16Value;
			int32_t int32Value;
			uint32_t uint32Value;

			// custom data
			unsigned char customData[8 + 8 + 8];
		};

		EventType type;
		int32_t reserved;
	};

	struct HQREMOTE_API PlainEvent {
		Event event;

		PlainEvent() : event(NO_EVENT) {}
		PlainEvent(EventType type) : event(type) {}
		virtual ~PlainEvent() {}

		virtual DataRef serialize() const;
		virtual void deserialize(const DataRef& data);
		virtual void deserialize(DataRef&& data);

		//cast to data
		operator DataRef() const{
			return serialize();
		}
		operator ConstDataRef() const{
			return serialize();
		}
	protected:
	};

	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<PlainEvent> EventRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const PlainEvent> ConstEventRef;
	
	struct HQREMOTE_API DataEvent : public PlainEvent {
		DataEvent(EventType type);
		DataEvent(uint32_t addtionalStorageSize, EventType type);
		
		virtual DataRef serialize() const override;
		virtual void deserialize(const DataRef& data) override;
		virtual void deserialize(DataRef&& data) override;

	protected:
		virtual void deserializeFromStorage() = 0;
		
		mutable DataRef storage;//this storage will hold both generic event data and additional data
	};
	
	struct HQREMOTE_API CompressedEvents : public DataEvent {
		typedef std::vector<EventRef> EventList;
		typedef EventList::iterator iterator;
		typedef EventList::const_iterator const_iterator;
		
		// this constructor is used for deserialization
		CompressedEvents(EventContainsFrameDataCallback callback): CompressedEvents(-1, nullptr)
		{
			m_customTypeCallback = callback;
		}
		// these constructor for serialization
		CompressedEvents(int zlibCompressLevel, const EventRef* event1, ...);//last argument should be nullptr
		CompressedEvents(int zlibCompressLevel, const EventList& events);
		CompressedEvents(int zlibCompressLevel, const_iterator eventListBegin, const_iterator eventListEnd);
		
		iterator begin() { return m_events.begin(); }
		const_iterator begin() const { return m_events.begin(); }
		const_iterator cbegin() const { return m_events.cbegin(); }
		
		iterator end() { return m_events.end(); }
		const_iterator end() const { return m_events.end(); }
		const_iterator cend() const { return m_events.cend(); }
		
	private:
		void init(int zlibCompressLevel);
		virtual void deserializeFromStorage() override;
		
		EventList m_events;
		EventContainsFrameDataCallback m_customTypeCallback = nullptr;
	};

	struct HQREMOTE_API FrameEvent : public DataEvent {
		explicit FrameEvent(EventType type = RENDERED_FRAME);
		explicit FrameEvent(uint32_t frameSize, uint64_t frameId, EventType type = RENDERED_FRAME);
		explicit FrameEvent(const void* frameData, uint32_t frameSize, uint64_t frameId, EventType type = RENDERED_FRAME);
		explicit FrameEvent(ConstDataRef frameData, uint64_t frameId, EventType type = RENDERED_FRAME);

	private:
		virtual void deserializeFromStorage() override;
	};

	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<DataEvent> DataEventRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const DataEvent> ConstDataEventRef;
	
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<FrameEvent> FrameEventRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const FrameEvent> ConstFrameEventRef;

	// the callback only invoked for custom event type (value > NO_EVENT)
	HQREMOTE_API  EventRef HQ_FASTCALL deserializeEvent(DataRef&& data, EventContainsFrameDataCallback callback);
	HQREMOTE_API  EventType HQ_FASTCALL peekEventType(const DataRef& data);
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
