#include "Event.h"

#include <stdexcept>

namespace HQRemote {
	/*----------- PlainEvent -------------*/
	DataRef PlainEvent::serialize() {
		auto data = std::make_shared<CData>(sizeof(this->event));
		//TODO: assume all sides use the same byte order for now
		memcpy(data->data(), &this->event, sizeof(this->event));

		return data;
	}

	void PlainEvent::deserialize(const DataRef& data) {
		if (data->size() < sizeof(this->event))
			throw std::runtime_error("data is too small");

		//TODO: assume all sides use the same byte order for now
		memcpy(&this->event, data->data(), sizeof(this->event));
	}

	void PlainEvent::deserialize(DataRef&& data) {
		DataRef dataCopy = data;

		deserialize(dataCopy);

		data = nullptr;
	}

	/*------------- DataEvent --------------*/
	DataEvent::DataEvent(EventType type)
		: PlainEvent(type)
	{}

	DataEvent::DataEvent(EventType type, uint32_t storageSize) 
		: PlainEvent(type), storage(std::make_shared<CData>(storageSize))
	{}

	/*----------- FrameEvent -------------*/
	FrameEvent::FrameEvent()
		: DataEvent(RENDERED_FRAME)
	{
		event.renderedFrameData.frameId = 0;
		event.renderedFrameData.frameSize = 0;
		event.renderedFrameData.frameData = NULL;
	}

	FrameEvent::FrameEvent(uint32_t frameSize, uint64_t frameId)
		: DataEvent(RENDERED_FRAME, sizeof(event) + frameSize)
	{
		//frame data will use "this->storage" as its backing store
		event.renderedFrameData.frameId = frameId;
		event.renderedFrameData.frameSize = frameSize;
		event.renderedFrameData.frameData = storage->data() + sizeof(event);
	}

	FrameEvent::FrameEvent(ConstDataRef frameData, uint64_t frameId)
		:FrameEvent(frameData->size(), frameId)
	{
		//copy frame data
		memcpy(event.renderedFrameData.frameData, frameData->data(), frameData->size());
	}

	DataRef FrameEvent::serialize() {
		if (this->storage == nullptr)
		{
			return PlainEvent::serialize();
		}

		//TODO: assume all sides use the same byte order for now
		memcpy(this->storage->data(), &this->event, sizeof(this->event));

		return this->storage;
	}

	void FrameEvent::deserialize(const DataRef& data) {
		//copy data
		this->storage = std::make_shared<CData>(data->size());
		memcpy(this->storage->data(), data->data(), data->size());

		//deserialize
		deserializeFromStorage();
	}

	void FrameEvent::deserialize(DataRef&& data) {
		this->storage = data;
		data = nullptr;

		deserializeFromStorage();
	}

	void FrameEvent::deserializeFromStorage() {
		//TODO: assume all sides use the same byte order for now
		memcpy(&this->event, this->storage->data(), sizeof(this->event));

		//deserialize frame data
		event.renderedFrameData.frameData = this->storage->data() + sizeof(this->event);
	}

	//factory function
	EventRef deserializeEvent(DataRef&& data) {
		try {
			//inspect event type first
			auto& dataRefCopy = data;

			PlainEvent plainEvent;
			plainEvent.deserialize(dataRefCopy);

			switch (plainEvent.event.type) {
			case RENDERED_FRAME:
			{
				//this is non-plain event
				auto frameEvent = std::make_shared<FrameEvent>();
				frameEvent->deserialize(std::forward<DataRef>(data));

				return frameEvent;
			}
				break;
			default:
				return std::make_shared<PlainEvent>(plainEvent);
			}//switch (plain.event.type)
		} catch (...)
		{
			return nullptr;
		}
	}
}