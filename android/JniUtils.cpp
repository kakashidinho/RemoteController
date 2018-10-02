//
//  JniUtils.cpp
//  nestopia
//
//  Created by Le Hoang Quyen on 26/2/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include "JniUtils.h"

#include <pthread.h>
#include <stdlib.h>

namespace HQRemote {
	static JavaVM *g_jvm = NULL;
	static void onJavaDetach(void* arg);

	extern "C"
	__attribute__((weak))
	jint JNIEXPORT JNI_OnLoad(JavaVM *vm, void *reserved)
	{
		setJVM(vm);

		return JNI_VERSION_1_4;
	}

	void setJVM(JavaVM *vm)
	{
		g_jvm = vm;
	}
		
	//this will be called when attached thread exit
	void onJavaDetach(void* arg)
	{
		JNIEnv *env;
		jint re = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_4);

		Log("Detaching current thread from java ...");
		if (re == JNI_OK) {
			g_jvm->DetachCurrentThread();
			Log("Detaching current thread from java DONE!");
		} else {
			Log("Current thread already detached, skipping ...");
		}
			
		//release thread key
		pthread_key_t *javaDetach = (pthread_key_t*)arg;
			
		pthread_setspecific(*javaDetach, NULL);
		pthread_key_delete(*javaDetach);
			
		free(javaDetach);
	}

	static JNIEnv * attachCurrenThreadJEnv()
	{
		JNIEnv *env;
		if (g_jvm->AttachCurrentThread(&env, NULL)!= JNI_OK)
			return NULL;
			
		//create thread destructor
		auto javaDetach = (pthread_key_t*)malloc( sizeof(pthread_key_t));
		pthread_key_create(javaDetach, onJavaDetach);
		//set thread key value so onJavaDetach() will be called when attached thread exit
		pthread_setspecific(*javaDetach, javaDetach);
			
		return env;
	}

	JNIEnv * getCurrenThreadJEnv()
	{
		JNIEnv *env;
		jint re = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_4);
			
		if(re == JNI_EDETACHED)
		{
			env = attachCurrenThreadJEnv();
		}
		else if (re != JNI_OK)
			env = NULL;
			
		return env;
	}

	void setCurrentThreadName(JNIEnv* jenv, const char* name) {
		// name this thread in C++
		pthread_setname_np(pthread_self(), name);

		// name this thread in java
		JClassRef ThreadClass = jenv->FindClass("java/lang/Thread");
		jmethodID currentThreadMethodID = jenv->GetStaticMethodID(ThreadClass, "currentThread", "()Ljava/lang/Thread;");

		jmethodID setThreadNameMethodID = jenv->GetMethodID(ThreadClass, "setName", "(Ljava/lang/String;)V");

		JObjectRef jcurrentThread = jenv->CallStaticObjectMethod(ThreadClass, currentThreadMethodID);
		JStringRef jname = jenv->NewStringUTF(name);
		jenv->CallVoidMethod(jcurrentThread, setThreadNameMethodID, jname.get());
	}
}
