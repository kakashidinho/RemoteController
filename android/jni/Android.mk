ROOT_PATH := $(call my-dir)

#-------------------#

LOCAL_PATH := $(ROOT_PATH)/../..

include $(CLEAR_VARS)


LOCAL_MODULE := RemoteController

LOCAL_CPPFLAGS := -std=c++11
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third-party/opus/include $(LOCAL_PATH)/third-party/jpeg-9a

JPEG_SRC_FILES := 	third-party/jpeg-9a/jcapimin.c \
    				third-party/jpeg-9a/jcapistd.c \
				    third-party/jpeg-9a/jcarith.c \
				    third-party/jpeg-9a/jcinit.c \
				    third-party/jpeg-9a/jcmarker.c \
				    third-party/jpeg-9a/jcmaster.c \
				    third-party/jpeg-9a/jcparam.c \
				    third-party/jpeg-9a/jctrans.c \
					third-party/jpeg-9a/jaricom.c \
				    third-party/jpeg-9a/jcomapi.c \
				    third-party/jpeg-9a/jdapimin.c \
				    third-party/jpeg-9a/jdapistd.c \
				    third-party/jpeg-9a/jdarith.c \
				    third-party/jpeg-9a/jdatadst.c \
				    third-party/jpeg-9a/jdatasrc.c \
				    third-party/jpeg-9a/jdcoefct.c \
				    third-party/jpeg-9a/jdcolor.c \
				    third-party/jpeg-9a/jddctmgr.c \
				    third-party/jpeg-9a/jdhuff.c \
				    third-party/jpeg-9a/jdinput.c \
				    third-party/jpeg-9a/jdmainct.c \
				    third-party/jpeg-9a/jdmarker.c \
				    third-party/jpeg-9a/jdmaster.c \
				    third-party/jpeg-9a/jdmerge.c \
				    third-party/jpeg-9a/jdpostct.c \
				    third-party/jpeg-9a/jdsample.c \
				    third-party/jpeg-9a/jdtrans.c \
				    third-party/jpeg-9a/jerror.c \
				    third-party/jpeg-9a/jidctflt.c \
				    third-party/jpeg-9a/jidctfst.c \
				    third-party/jpeg-9a/jidctint.c \
				    third-party/jpeg-9a/jmemansi.c \
				    third-party/jpeg-9a/jmemmgr.c \
				    third-party/jpeg-9a/jquant1.c \
				    third-party/jpeg-9a/jquant2.c \
				    third-party/jpeg-9a/jutils.c \
				    third-party/jpeg-9a/jcmainct.c \
				    third-party/jpeg-9a/jcprepct.c \
				    third-party/jpeg-9a/jccoefct.c \
				    third-party/jpeg-9a/jccolor.c \
				    third-party/jpeg-9a/jcsample.c \
				    third-party/jpeg-9a/jcdctmgr.c \
				    third-party/jpeg-9a/jfdctint.c \
				    third-party/jpeg-9a/jfdctfst.c \
				    third-party/jpeg-9a/jfdctflt.c \
				    third-party/jpeg-9a/jchuff.c \

LOCAL_SRC_FILES := 	ZlibUtils.cpp \
					Common.cpp \
					ConnectionHandler.cpp \
					Event.cpp \
					linux/ConnectionHandlerLinux.cpp \
					linux/TimerLinux.cpp \
					Client/Client.cpp \
					Server/AudioCapturer.cpp \
					Server/Engine.cpp \
					Server/ImgCompressor.cpp \
					Server/JpegCompressor.cpp \
					Server/PngCompressor.cpp \
					Server/FrameCapturer.cpp \
					Server/android/EngineAndroid.cpp \

LOCAL_SRC_FILES += $(JPEG_SRC_FILES)


LOCAL_ARM_MODE := arm

LOCAL_CPP_FEATURES := exceptions

LOCAL_LDLIBS := -lz

LOCAL_STATIC_LIBRARIES := opus_static

include $(BUILD_SHARED_LIBRARY)

$(call import-add-path, $(LOCAL_PATH)/third-party)
$(call import-module,opus-prebuilt/Android/jni)
