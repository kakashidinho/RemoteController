#ifndef REMOTE_EVENT_H
#define REMOTE_EVENT_H

#include "Data.h"

#include <stdint.h>
#include <memory>
#include <list>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	typedef uint32_t EventType;

	enum PredefinedEventType: uint32_t {
		TOUCH_BEGAN,
		TOUCH_MOVED,
		TOUCH_ENDED,
		TOUCH_CANCELLED,
	
		START_SEND_FRAME,
		STOP_SEND_FRAME,
		
		RECORD_START,
		RECORD_END,
		
		SCREENSHOT_CAPTURE,

		HOST_INFO,
		RENDERED_FRAME,
		
		FRAME_INTERVAL,
		
		AUDIO_STREAM_INFO,
		AUDIO_ENCODED_PACKET,
		AUDIO_DECODED_PACKET,

		COMPRESSED_EVENTS,

		NO_EVENT,
	};

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
			} renderedFrameData;
			
			struct {
				uint32_t numEvents;
			} compressedEvents;

			struct {
				int32_t sampleRate;
				int32_t numChannels;
			} audioStreamInfo;
			
			double frameInterval;

			unsigned char customData[8 + 8 + 4];
		};

		EventType type;
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
		DataEvent(uint32_t storageSize, EventType type);
		
		virtual DataRef serialize() const override;
		virtual void deserialize(const DataRef& data) override;
		virtual void deserialize(DataRef&& data) override;

	protected:
		virtual void deserializeFromStorage() = 0;
		
		mutable DataRef storage;
	};
	
	struct HQREMOTE_API CompressedEvents : public DataEvent {
		typedef std::list<EventRef> EventList;
		typedef EventList::iterator iterator;
		typedef EventList::const_iterator const_iterator;
		
		CompressedEvents(): CompressedEvents(nullptr) {}
		CompressedEvents(const EventRef* event1, ...);//last argument should be nullptr
		CompressedEvents(const EventList& events);
		
		iterator begin() { return m_events.begin(); }
		const_iterator begin() const { return m_events.begin(); }
		const_iterator cbegin() const { return m_events.cbegin(); }
		
		iterator end() { return m_events.end(); }
		const_iterator end() const { return m_events.end(); }
		const_iterator cend() const { return m_events.cend(); }
		
	private:
		void init();
		virtual void deserializeFromStorage() override;
		
		EventList m_events;
	};

	struct HQREMOTE_API FrameEvent : public DataEvent {
		explicit FrameEvent(EventType type = RENDERED_FRAME);
		explicit FrameEvent(uint32_t frameSize, uint64_t frameId, EventType type = RENDERED_FRAME);
		explicit FrameEvent(ConstDataRef frameData, uint64_t frameId, EventType type = RENDERED_FRAME);

	private:
		virtual void deserializeFromStorage() override;
	};

	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<DataEvent> DataEventRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const DataEvent> ConstDataEventRef;
	
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<FrameEvent> FrameEventRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const FrameEvent> ConstFrameEventRef;

	HQREMOTE_API  EventRef HQ_FASTCALL deserializeEvent(DataRef&& data);
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif
