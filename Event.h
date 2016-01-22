#ifndef REMOTE_EVENT_H
#define REMOTE_EVENT_H

#include "Data.h"

#include <stdint.h>
#include <memory>

namespace HQRemote {
	enum EventType: uint32_t {
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

		PING_MSG,
		
		CUSTOM_MSG,

		NO_EVENT,
	};

	struct Event {
		Event(EventType _type) : type(_type)
		{}

		EventType type;

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
				uint32_t frameSize;
				void* frameData;
			} renderedFrameData;
		};
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

	protected:
		DataRef storage;
	};

	struct FrameEvent : public DataEvent {
		FrameEvent();
		FrameEvent(uint32_t frameSize, uint64_t frameId);
		explicit FrameEvent(ConstDataRef frameData, uint64_t frameId);

		virtual DataRef serialize() override;
		virtual void deserialize(const DataRef& data) override;
		virtual void deserialize(DataRef&& data) override;

	private:
		void deserializeFromStorage();
	};

	typedef std::shared_ptr<DataEvent> DataEventRef;
	typedef std::shared_ptr<const DataEvent> ConstDataEventRef;

	EventRef deserializeEvent(DataRef&& data);
}

#endif
