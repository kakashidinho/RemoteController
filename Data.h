#ifndef HQREMOTE_DATA_H
#define HQREMOTE_DATA_H

#include "Common.h"

#include <memory>
#include <functional>
#include <vector>
#include <stdexcept>

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

namespace HQRemote {
	//data wrapper
	class HQREMOTE_API IData {
	public:
		virtual ~IData() {}

		virtual unsigned char* data() = 0;
		virtual const unsigned char* data() const = 0;

		virtual size_t size() const = 0;
	};
	
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<IData> DataRef;
	typedef HQREMOTE_API_TYPEDEF std::shared_ptr<const IData> ConstDataRef;

	class HQREMOTE_API CData : public IData {
	public:
		typedef std::function<void(unsigned char*)> DestructFunc;

		CData()
			:m_size(0), m_data(NULL), m_destructFunc([](unsigned char* data) { delete[] data; })
		{}

		CData(size_t size)
			:m_size(size), m_data(new unsigned char[size]), m_destructFunc([](unsigned char* data) { delete[] data; })
		{}

		CData(const unsigned char* data, size_t size)
			:CData(size)
		{
			memcpy(m_data, data, size);
		}

		//this object will take ownership of the passed pointer if <destructFunc> is not null
		CData(unsigned char* data, size_t size, DestructFunc destructFunc) {
			transferFrom(data, size, destructFunc);
		}

		~CData() {
			if (m_destructFunc)
				m_destructFunc(m_data);
		}

		virtual unsigned char* data() override { return m_data; }
		virtual const unsigned char* data() const override { return m_data; }

		virtual size_t size() const override { return m_size; }

		//this object will take ownership of the passed pointer
		void transferFrom(unsigned char* data, size_t size, DestructFunc destructFunc) {
			m_data = data;
			m_size = size;
			m_destructFunc = destructFunc;
		}
	private:
		unsigned char* m_data;
		size_t m_size;
		DestructFunc m_destructFunc;
	};
	
	class HQREMOTE_API GrowableData: public IData {
	public:
		GrowableData()
		{
		}
		
		GrowableData(size_t initialCapacity)
		{
			m_data.reserve(initialCapacity);
		}
		
		virtual unsigned char* data() override { return m_data.data(); }
		virtual const unsigned char* data() const override { return m_data.data(); }
		
		virtual size_t size() const override { return m_data.size(); }
		
		void push_back(const void* _data, size_t _size)
		{
			m_data.reserve(m_data.size() + _size);
			for (size_t i = 0; i < _size; ++i)
				m_data.push_back(((unsigned char*)_data)[i]);
		}
		
		void push_back(ConstDataRef _data){
			if (_data)
				push_back(_data->data(), _data->size());
		}
		
		void expand(size_t size) {
			m_data.insert(m_data.end(), size, 0);
		}

		void resize(size_t size) {
			m_data.resize(size);
		}

		void reserve(size_t cap) {
			m_data.reserve(cap);
		}
	private:
		std::vector<unsigned char> m_data;
	};
	
	template <class T>
	class HQREMOTE_API TDataSegment: public T {
	public:
		typedef std::shared_ptr<T> SrcDataRef;

		TDataSegment(SrcDataRef parent, size_t offset)
			: m_parent(parent), m_offset(offset), m_size(parent->size() - offset)
		{}

		TDataSegment(SrcDataRef parent, size_t offset, size_t size)
		: m_parent(parent), m_offset(offset), m_size(size)
		{}
		
		virtual const unsigned char* data() const override { return m_parent->data() + m_offset; }
		
		virtual size_t size() const override { return m_size; }
	protected:
		SrcDataRef m_parent;
		size_t m_offset;
		size_t m_size;
	};

	class HQREMOTE_API DataSegment : public TDataSegment < IData > {
	public:
		typedef TDataSegment < IData > Parent;

		DataSegment(SrcDataRef parent, size_t offset)
			: Parent(parent, offset)
		{}

		DataSegment(SrcDataRef parent, size_t offset, size_t size)
			: Parent(parent, offset, size)
		{}

		virtual unsigned char* data() override { return m_parent->data() + m_offset; }
	};

	class HQREMOTE_API ConstDataSegment : public TDataSegment < const IData > {
	public:
		typedef TDataSegment <const IData > Parent;

		ConstDataSegment(SrcDataRef parent, size_t offset)
			: Parent(parent, offset)
		{}

		ConstDataSegment(SrcDataRef parent, size_t offset, size_t size)
			: Parent(parent, offset, size)
		{}

		virtual unsigned char* data() override { throw std::runtime_error("Const Data is not allowed to be modified"); }
	};
}

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif

#endif