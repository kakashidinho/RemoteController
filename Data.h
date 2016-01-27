#ifndef HQREMOTE_DATA_H
#define HQREMOTE_DATA_H

#include <memory>
#include <functional>
#include <vector>
#include <stdexcept>

namespace HQRemote {
	//data wrapper
	struct IData {
		virtual ~IData() {}

		virtual unsigned char* data() = 0;
		virtual const unsigned char* data() const = 0;

		virtual size_t size() const = 0;
	};
	
	typedef std::shared_ptr<IData> DataRef;
	typedef std::shared_ptr<const IData> ConstDataRef;

	struct CData : public IData {
		typedef std::function<void(unsigned char*)> DestructFunc;

		CData()
			:m_size(0), m_data(NULL), m_destructFunc([](unsigned char* data) { delete[] data; })
		{}

		CData(size_t size)
			:m_size(size), m_data(new unsigned char[size]), m_destructFunc([](unsigned char* data) { delete[] data; })
		{}

		//this object will take ownership of the passed pointer
		CData(unsigned char* data, size_t size, DestructFunc destructFunc) {
			transferFrom(data, size, destructFunc);
		}

		~CData() {
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
	
	struct GrowableData: public IData {
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
	private:
		std::vector<unsigned char> m_data;
	};
	
	template <class T>
	struct TDataSegment: public T {
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

	class DataSegment : public TDataSegment < IData > {
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

	class ConstDataSegment : public TDataSegment < const IData > {
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

#endif