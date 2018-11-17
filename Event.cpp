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

#include <assert.h>
#include <stdexcept>
#include <limits>
#include <stdarg.h>

#include "ZlibUtils.h"
#include "Event.h"
#include "Common.h"

#ifdef max
#	undef max
#endif


namespace HQRemote {
	/*----------- PlainEvent -------------*/
	DataRef PlainEvent::serialize() const {
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

	DataEvent::DataEvent(uint32_t addtionalStorageSize, EventType type)
		: PlainEvent(type), storage(std::make_shared<CData>(sizeof(event) + addtionalStorageSize))
	{}

	DataRef DataEvent::serialize() const {
		if (this->storage == nullptr)
		{
			return PlainEvent::serialize();
		}
		
		//serialize generic event data
		//TODO: assume all sides use the same byte order for now
		memcpy(this->storage->data(), &this->event, sizeof(this->event));
		
		return this->storage;
	}
	
	void DataEvent::deserialize(const DataRef& data) {
		//copy data
		this->storage = std::make_shared<CData>(data->size());
		memcpy(this->storage->data(), data->data(), data->size());
		
		//deserialize
		
		//TODO: assume all sides use the same byte order for now
		memcpy(&this->event, this->storage->data(), sizeof(this->event));
		
		deserializeFromStorage();
	}
	
	void DataEvent::deserialize(DataRef&& data) {
		this->storage = data;
		data = nullptr;
		
		//TODO: assume all sides use the same byte order for now
		memcpy(&this->event, this->storage->data(), sizeof(this->event));
		
		deserializeFromStorage();
	}
	
	/*---------- CompressedEvents --------*/
	CompressedEvents::CompressedEvents(int zlibCompressLevel, const EventRef* event1, ...)
	: DataEvent(COMPRESSED_EVENTS)
	{
		if (event1 == nullptr)
			return;
		
		m_events.push_back(*event1);
		
		const EventRef *pEvent;
		
		va_list args;
		va_start(args, event1);
		
		while ((pEvent = va_arg(args, const EventRef*)) != nullptr) {
			m_events.push_back(*pEvent);
		}
		
		va_end(args);
		
		init(zlibCompressLevel);
		
	}
	
	CompressedEvents::CompressedEvents(int zlibCompressLevel, const EventList& events)
	: DataEvent(COMPRESSED_EVENTS), m_events(events)
	{
		init(zlibCompressLevel);
	}

	CompressedEvents::CompressedEvents(int zlibCompressLevel, const_iterator eventListBegin, const_iterator eventListEnd)
	: DataEvent(COMPRESSED_EVENTS)
	{
		for (auto ite = eventListBegin; ite != eventListEnd; ++ite) {
			m_events.push_back(*ite);
		}

		init(zlibCompressLevel);
	}
	
	void CompressedEvents::init(int zlibCompressLevel) {
		try {
			//init generic info
			assert(m_events.size() <= std::numeric_limits<uint32_t>::max());
			this->event.compressedEvents.numEvents = (uint32_t)m_events.size();
			
			//init storage
			auto storage = std::make_shared<GrowableData>();
			storage->push_back(&this->event, sizeof(this->event));
			
			this->storage = storage;
			
			//serialize to storage
			//storage layout:
			//event's generic info | offset table | compressed data
			
			GrowableData uncompresedData;
			auto growableStorage = std::static_pointer_cast<GrowableData>(this->storage);
			
			//placeholder for event offset table
			auto offsetTableOff = growableStorage->size();
			assert(offsetTableOff == sizeof(this->event));
			assert(offsetTableOff % sizeof(uint64_t) == 0);
			
			growableStorage->expand(sizeof(uint64_t) * (m_events.size()));
			
			std::vector<uint64_t> offsetTable(m_events.size());
			size_t i = 0;
			
			//combine the event's serialized data into one single data
			for (auto &event: m_events) {
				auto eventData = event->serialize();
				
				offsetTable[i++] = uncompresedData.size();
				uncompresedData.push_back(eventData);
			}
			
			//compress the combined data
			zlibCompress(uncompresedData, zlibCompressLevel, *growableStorage);
			
			uint64_t *pOffsetTable = (uint64_t*)(growableStorage->data() + offsetTableOff);
			memcpy(pOffsetTable, offsetTable.data(), offsetTable.size() * sizeof(offsetTable[0]));

		} catch (...) {
			//failed
			this->storage = nullptr;
			m_events. clear();
			
			this->event.type = NO_EVENT;
		}
	}
	
	void CompressedEvents::deserializeFromStorage() {
		//storage layout:
		//event's generic info | offset table | compressed data
		auto offsetTableOff = sizeof(this->event);
		
		uint64_t *offsetTable = (uint64_t*)(this->storage->data() + offsetTableOff);
		DataSegment compressedData(this->storage, offsetTableOff + this->event.compressedEvents.numEvents * sizeof(offsetTable[0]));
		auto decompressedData = zlibDecompress(compressedData);
		auto uncompressedSize = decompressedData->size();
		
		//deserialize individual events
		for (uint32_t i = 0; i < this->event.compressedEvents.numEvents; ++i) {
			size_t offset = offsetTable[i];
			size_t size;
			if (i < this->event.compressedEvents.numEvents - 1)
				size = offsetTable[i + 1] - offset;
			else
				size = uncompressedSize - offset;
			
			auto dataSegement = std::make_shared<DataSegment>(decompressedData, offset, size);
			auto event = deserializeEvent(std::move(dataSegement));
			
			m_events.push_back(event);
		}
	}
	
	/*----------- FrameEvent -------------*/
	FrameEvent::FrameEvent(EventType type)
		: DataEvent(type)
	{
		event.renderedFrameData.frameId = 0;
		event.renderedFrameData.frameSize = 0;
		event.renderedFrameData.frameData = NULL;
	}

	FrameEvent::FrameEvent(uint32_t frameSize, uint64_t frameId, EventType type)
		: DataEvent(frameSize, type)
	{
		//frame data will use "this->storage" as its backing store
		event.renderedFrameData.frameId = frameId;
		event.renderedFrameData.frameSize = frameSize;
		event.renderedFrameData.frameData = storage->data() + sizeof(event);
	}

	FrameEvent::FrameEvent(const void* frameData, uint32_t frameSize, uint64_t frameId, EventType type)
		:FrameEvent(frameSize, frameId, type)
	{
		//copy frame data
		memcpy(event.renderedFrameData.frameData, frameData, frameSize);
	}

	FrameEvent::FrameEvent(ConstDataRef frameData, uint64_t frameId, EventType type)
		:FrameEvent((uint32_t)frameData->size(), frameId, type)
	{
		assert(frameData->size() <= std::numeric_limits<uint32_t>::max());
		
		//copy frame data
		memcpy(event.renderedFrameData.frameData, frameData->data(), frameData->size());
	}

	void FrameEvent::deserializeFromStorage() {
		//deserialize frame data
		event.renderedFrameData.frameData = this->storage->data() + sizeof(this->event);
	}

	EventType HQ_FASTCALL peekEventType(const DataRef& data) {
		try {
			PlainEvent plainEvent;
			plainEvent.deserialize(data);

			return plainEvent.event.type;
		}
		catch (...) {
			return NO_EVENT;
		}
	}

	//factory function
	EventRef HQ_FASTCALL deserializeEvent(DataRef&& data) {
		try {
			//inspect event type first
			auto& dataRefCopy = data;

			PlainEvent plainEvent;
			plainEvent.deserialize(dataRefCopy);

			switch (plainEvent.event.type) {
			case RENDERED_FRAME: case AUDIO_ENCODED_PACKET: case ENDPOINT_NAME: case MESSAGE:
			{
				//this is non-plain event
				auto frameEvent = std::make_shared<FrameEvent>(plainEvent.event.type);
				frameEvent->deserialize(std::forward<DataRef>(data));

				return frameEvent;
			}
				break;
			case COMPRESSED_EVENTS:
			{
				//this is non-plain event
				auto compressedEvents = std::make_shared<CompressedEvents>();
				compressedEvents->deserialize(std::forward<DataRef>(data));
				
				return compressedEvents;
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
