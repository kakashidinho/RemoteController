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
		CString() {
			init("", 0);
		}

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

		CString& operator=(const char* src) {
			delete[] m_str;
			m_str = NULL;

			if (src != NULL) {
				auto len = strlen(src);

				init(src, len);
			}
			else
				init("", 0);

			return *this;
		}

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
