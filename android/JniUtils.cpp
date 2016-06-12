//
//  JniUtils.cpp
//  nestopia
//
//  Created by Le Hoang Quyen on 26/2/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include "JniUtils.h"

#include <pthread.h>

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
		g_jvm->DetachCurrentThread();
			
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
}
