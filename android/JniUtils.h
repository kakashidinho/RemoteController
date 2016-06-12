//
//  JniUtils.h
//  nestopia
//
//  Created by Le Hoang Quyen on 26/2/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#ifndef HQRemoteJniUtils_hpp
#define HQRemoteJniUtils_hpp

#include "../Common.h"

#include <stdio.h>
#include <memory>

#include <jni.h>

namespace HQRemote {
	HQREMOTE_API void setJVM(JavaVM *vm);
	HQREMOTE_API JNIEnv * getCurrenThreadJEnv();
		
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
