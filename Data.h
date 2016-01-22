#ifndef HQREMOTE_DATA_H
#define HQREMOTE_DATA_H

#include <memory>
#include <functional>

namespace HQRemote {
	//data wrapper
	struct IData {
		virtual ~IData() {}

		virtual unsigned char* data() = 0;
		virtual const unsigned char* data() const = 0;

		virtual size_t size() const = 0;
	};

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

	typedef std::shared_ptr<IData> DataRef;
	typedef std::shared_ptr<const IData> ConstDataRef;
}

#endif