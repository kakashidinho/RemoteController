APP_ABI := armeabi-v7a x86
APP_PLATFORM := android-9

APP_STL := gnustl_shared

ifeq ($(NDK_DEBUG),1)
  APP_DEBUG := true
  APP_CPPFLAGS += -DDEBUG=1 -g -ggdb
  APP_OPTIM := debug
else
  APP_DEBUG := false
  APP_CPPFLAGS += -DNDEBUG
  APP_OPTIM := release
endif

NDK_TOOLCHAIN_VERSION := clang3.6

