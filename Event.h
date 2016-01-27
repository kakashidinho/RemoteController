#ifndef REMOTE_EVENT_H
#define REMOTE_EVENT_H

#include "Data.h"

#include <stdint.h>
#include <memory>
#include <list>

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
		
		COMPRESSED_EVENTS,

		NO_EVENT,
	};

	struct Event {
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
			
			double frameInterval;

			unsigned char customData[8 + 8 + 4];
		};

		EventType type;
	};

	struct PlainEvent {
		Event event;

		PlainEvent() : event(NO_EVENT) {}
		PlainEvent(EventType type) : event(type) {}
		virtual ~PlainEvent() {}

		virtual DataRef serialize();
		virtual void deserialize(const DataRef& data);
		virtual void deserialize(DataRef&& data);

		//cast to data
		operator DataRef(){
			return serialize();
		}
		operator ConstDataRef(){
			return serialize();
		}
	protected:
	};

	typedef std::shared_ptr<PlainEvent> EventRef;
	typedef std::shared_ptr<const PlainEvent> ConstEventRef;
	
	struct DataEvent : public PlainEvent {
		DataEvent(EventType type);
		DataEvent(EventType type, uint32_t storageSize);
		
		virtual DataRef serialize() override;
		virtual void deserialize(const DataRef& data) override;
		virtual void deserialize(DataRef&& data) override;

	protected:
		virtual void deserializeFromStorage() = 0;
		
		DataRef storage;
	};
	
	struct CompressedEvents : public DataEvent {
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

	struct FrameEvent : public DataEvent {
		FrameEvent();
		FrameEvent(uint32_t frameSize, uint64_t frameId);
		explicit FrameEvent(ConstDataRef frameData, uint64_t frameId);

	private:
		virtual void deserializeFromStorage() override;
	};

	typedef std::shared_ptr<DataEvent> DataEventRef;
	typedef std::shared_ptr<const DataEvent> ConstDataEventRef;
	
	typedef std::shared_ptr<FrameEvent> FrameEventRef;
	typedef std::shared_ptr<const FrameEvent> ConstFrameEventRef;

	EventRef deserializeEvent(DataRef&& data);
}

#endif
