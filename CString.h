#ifndef HQREMOTE_CSTRING_H
#define HQREMOTE_CSTRING_H

#include "Common.h"

#include <stdlib.h>
#include <string.h>
#include <ostream>
#include <new>

namespace HQRemote {
	class HQREMOTE_API CString {
	public:
		explicit CString(const char* str)
		{
			if (str == NULL)
				throw std::bad_alloc();

			auto len = strlen(str);

			init(str, len);
		}

		CString(const char* str, size_t len)
		{
			if (str == NULL)
				throw std::bad_alloc();

			init(str, len);
		}

		CString(const CString& src)
			:CString(src.m_str, src.m_len)
		{}

		CString(CString&& src)
		{
			m_str = src.m_str;
			m_len = src.m_len;
			src.m_str = NULL;
			src.m_len = 0;
		}

		~CString() {
			delete[] m_str;
		}

		const char* c_str() const { return m_str; }
		size_t size() const { return m_len; }

	private:
		void init(const char* str, size_t len) {
			m_len = len;
			m_str = new char[m_len + 1];
			memcpy(m_str, str, m_len + 1);
		}

		char *m_str;
		size_t m_len;
	};

	static inline std::ostream& operator << (std::ostream& os, const CString& str) {
		return os << str.c_str();
	}
}

#endif
