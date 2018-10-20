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
#ifndef HQRemoteJniUtils_hpp
#define HQRemoteJniUtils_hpp

#include "../Common.h"

#include <stdio.h>
#include <memory>

#include <jni.h>

namespace HQRemote {
	HQREMOTE_API void setJVM(JavaVM *vm);
	HQREMOTE_API JNIEnv * getCurrenThreadJEnv();
	HQREMOTE_API void setCurrentThreadName(JNIEnv* env, const char* name);
		
	template <class T>
	class JWrapper {
	public:
		JWrapper()
		:m_raw(NULL)
		{}
			
		JWrapper(const JWrapper& src)
		:m_raw(src.m_raw)
		{}
			
		explicit JWrapper(T raw)
		:m_raw(raw)
		{}
			
		T get() const { return m_raw; }
		operator T () const { return m_raw; }
		operator bool() const { return m_raw != NULL; }
			
		JWrapper& operator = (T raw) {
			m_raw = raw;
			return *this;
		}
	protected:
		T m_raw;
	};
	
	template <class T>
	class JObjectWrapper : public JWrapper<T> {
	public:
		typedef JWrapper<T> parent_t;

		JObjectWrapper() : parent_t() {}
		JObjectWrapper(T obj) : parent_t(obj) {}

		~JObjectWrapper() {
			release();
		}

		JObjectWrapper& operator = (T raw) {
			release();

			this->m_raw = raw;
			return *this;
		}

	private:
		void release();
	};

	template <class T>
	inline void JObjectWrapper<T>::release() {
		if (this->m_raw)
		{
			auto jenv = getCurrenThreadJEnv();
			if (jenv)
			{
				auto refType = jenv->GetObjectRefType(this->m_raw);
				switch (refType)
				{
				case JNILocalRefType:
					jenv->DeleteLocalRef(this->m_raw);
					break;
				case JNIGlobalRefType:
					jenv->DeleteGlobalRef(this->m_raw);
					break;
				case JNIWeakGlobalRefType:
					jenv->DeleteWeakGlobalRef(this->m_raw);
					break;
				}
			}//if (jenv)

			this->m_raw = NULL;
		}//if (m_raw)
	}

	typedef JWrapper<jmethodID> JMethodID;
	typedef JWrapper<jfieldID> JFieldID;

	typedef JObjectWrapper<jobject> JObjectRef;
	typedef JObjectWrapper<jclass> JClassRef;
	typedef JObjectWrapper<jstring> JStringRef;
	typedef JObjectWrapper<jbyteArray> JByteArrayRef;
}


#endif /* HQRemoteJniUtils_hpp */
