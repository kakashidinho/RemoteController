#include "Event.h"
#include "Common.h"

#include <assert.h>
#include <stdexcept>
#include <limits>
#include <stdarg.h>

#include <zlib.h>

#define COMPRESS_CHUNK_SIZE (256 * 1024)

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

	DataRef DataEvent::serialize() {
		if (this->storage == nullptr)
		{
			return PlainEvent::serialize();
		}
		
		//deserial generic event data
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
	CompressedEvents::CompressedEvents(const EventRef* event1, ...)
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
		
		init();
		
	}
	
	CompressedEvents::CompressedEvents(const EventList& events)
	: DataEvent(COMPRESSED_EVENTS), m_events(events)
	{
		init();
	}
	
	void CompressedEvents::init() {
		try {
			//init generic info
			assert(m_events.size() <= std::numeric_limits<uint32_t>::max());
			this->event.compressedEvents.numEvents = m_events.size();
			
			//init storage
			auto storage = std::make_shared<GrowableData>();
			storage->push_back(&this->event, sizeof(this->event));
			
			this->storage = storage;
			
			//serialize to storage
			//storage layout:
			//event's generic info | uncompressed size | offset table | compressed data
			
			GrowableData uncompresedData;
			auto growableStorage = std::static_pointer_cast<GrowableData>(this->storage);
			
			//placeholder for uncompressed size + event offset table
			auto uncompressedSizeOff = growableStorage->size();
			assert(uncompressedSizeOff == sizeof(this->event));
			assert(uncompressedSizeOff % sizeof(uint64_t) == 0);
			
			growableStorage->expand(sizeof(uint64_t) * (1 + m_events.size()));
			
			std::vector<uint64_t> offsetTable(m_events.size());
			size_t i = 0;
			z_stream sz;
			memset(&sz, 0, sizeof(sz));
			
			//combine the event's serialized data into one single data
			for (auto &event: m_events) {
				auto eventData = event->serialize();
				
				offsetTable[i++] = uncompresedData.size();
				uncompresedData.push_back(eventData);
			}
			
			//compress the combined data
			if (deflateInit(&sz, Z_DEFAULT_COMPRESSION) != Z_OK)
				throw std::runtime_error("deflateInit failed");
			if (uncompresedData.size() > std::numeric_limits<uint32_t>::max())
				throw std::runtime_error("uncompressed data too big");
			
			int re;
			unsigned char buffer[COMPRESS_CHUNK_SIZE];
			sz.next_in = uncompresedData.data();
			sz.avail_in = uncompresedData.size();
			sz.next_out = buffer;
			sz.avail_out = sizeof(buffer);
			
			while ((re = deflate(&sz, Z_FINISH)) != Z_STREAM_END && re == Z_OK) {
				growableStorage->push_back(buffer, sizeof(buffer) - sz.avail_out);
				
				sz.avail_out = sizeof(buffer);
			}
			
			growableStorage->push_back(buffer, sizeof(buffer) - sz.avail_out);
			
			//finalize
			deflateEnd(&sz);
			
			uint64_t& uncompressedSize = *(uint64_t*)(growableStorage->data() + uncompressedSizeOff);
			uint64_t *pOffsetTable = &uncompressedSize + 1;
			
			uncompressedSize = uncompresedData.size();
			memcpy(pOffsetTable, offsetTable.data(), offsetTable.size() * sizeof(offsetTable[0]));
			
			if (re != Z_STREAM_END)
			{
				throw std::runtime_error("compression failed");
			}
		} catch (...) {
			//failed
			this->storage = nullptr;
			m_events. clear();
			
			this->event.type = NO_EVENT;
		}
	}
	
	void CompressedEvents::deserializeFromStorage() {
		//storage layout:
		//event's generic info | uncompressed size | offset table | compressed data
		auto uncompressedSizeOff = sizeof(this->event);
		
		uint64_t& uncompressedSize = *(uint64_t*)(this->storage->data() + uncompressedSizeOff);
		uint64_t *offsetTable = &uncompressedSize + 1;
		unsigned char* compressedData = (unsigned char*)(offsetTable + this->event.compressedEvents.numEvents);
		_ssize_t compressedSize = (_ssize_t)this->storage->size() - (_ssize_t)(this->storage->data() - compressedData);
		if (compressedSize < 0)
			throw  std::runtime_error("Size is too small for decompression");
		
		auto decompressedData = std::make_shared<CData>(uncompressedSize);
		uLongf decompressedSize = decompressedData->size();
		if (uncompress(decompressedData->data(), &decompressedSize, compressedData, compressedSize) != Z_OK
			|| decompressedSize != uncompressedSize) {
			throw  std::runtime_error("Decompression failed");
		}
		
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
		assert(frameData->size() <= std::numeric_limits<uint32_t>::max());
		
		//copy frame data
		memcpy(event.renderedFrameData.frameData, frameData->data(), frameData->size());
	}

	void FrameEvent::deserializeFromStorage() {
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