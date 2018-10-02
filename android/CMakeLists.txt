cmake_minimum_required(VERSION 3.4.1)

set(MY_SOURCE_DIR ..)

set(MY_C_FLAGS
    "-fvisibility=hidden"
    )

set(MY_CXX_FLAGS
    "-std=c++11"
    )

set(CMAKE_C_FLAGS
    "${CMAKE_C_FLAGS} ${MY_C_FLAGS}"
    )

set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} \
     ${MY_C_FLAGS} \
     ${MY_CXX_FLAGS}"
    )


set(MY_INCL_DIRS    ${MY_SOURCE_DIR}/third-party/opus/include
                    ${MY_SOURCE_DIR}/third-party/jpeg-9a)


set(MY_JPEG_SRC_FILES   ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcapimin.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcapistd.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcarith.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcinit.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcmarker.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcmaster.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcparam.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jctrans.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jaricom.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcomapi.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdapimin.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdapistd.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdarith.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdatadst.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdatasrc.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdcoefct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdcolor.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jddctmgr.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdhuff.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdinput.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdmainct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdmarker.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdmaster.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdmerge.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdpostct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdsample.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jdtrans.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jerror.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jidctflt.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jidctfst.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jidctint.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jmemansi.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jmemmgr.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jquant1.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jquant2.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jutils.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcmainct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcprepct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jccoefct.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jccolor.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcsample.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jcdctmgr.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jfdctint.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jfdctfst.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jfdctflt.c
                        ${MY_SOURCE_DIR}/third-party/jpeg-9a/jchuff.c
    )

set(MY_SRC_FILES    ${MY_SOURCE_DIR}/ZlibUtils.cpp
                    ${MY_SOURCE_DIR}/Common.cpp
                    ${MY_SOURCE_DIR}/AudioCapturer.cpp
                    ${MY_SOURCE_DIR}/BaseEngine.cpp
                    ${MY_SOURCE_DIR}/ConnectionHandler.cpp
                    ${MY_SOURCE_DIR}/Event.cpp
                    ${MY_SOURCE_DIR}/android/JniUtils.cpp
                    ${MY_SOURCE_DIR}/android/ConnectionHandlerAndroid.cpp
                    ${MY_SOURCE_DIR}/unix/ConnectionHandlerUnix.cpp
                    ${MY_SOURCE_DIR}/linux/ConnectionHandlerLinux.cpp
                    ${MY_SOURCE_DIR}/linux/TimerLinux.cpp
                    ${MY_SOURCE_DIR}/Client/Client.cpp
                    ${MY_SOURCE_DIR}/Server/Engine.cpp
                    ${MY_SOURCE_DIR}/Server/ImgCompressor.cpp
                    ${MY_SOURCE_DIR}/Server/JpegCompressor.cpp
                    ${MY_SOURCE_DIR}/Server/PngCompressor.cpp
                    ${MY_SOURCE_DIR}/Server/FrameCapturer.cpp
                    ${MY_SOURCE_DIR}/Server/android/EngineAndroid.cpp

    )


add_library(RemoteController

            SHARED

            ${MY_JPEG_SRC_FILES}
            ${MY_SRC_FILES}
            )


target_include_directories(
            RemoteController 
            
            PRIVATE
            
            ${MY_INCL_DIRS}
            )

# ---------- NDK libs --------------------
find_library( # Sets the name of the path variable.
              log-lib

              # Specifies the name of the NDK library that
              # you want CMake to locate.
              log )

find_library( # Sets the name of the path variable.
              z-lib

              # Specifies the name of the NDK library that
              # you want CMake to locate.
              z )


# ---------- opus ---------------------

set(MY_OPUS_SRC_DIR  ${MY_SOURCE_DIR}/third-party/opus-prebuilt/Android)
set(MY_OPUS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/opus )
file(MAKE_DIRECTORY ${MY_OPUS_BUILD_DIR})

add_subdirectory( # Specifies the directory of the CMakeLists.txt file.
                  ${MY_OPUS_SRC_DIR}

                  # Specifies the directory for the build outputs.
                  ${MY_OPUS_BUILD_DIR} )


# ---------- final linking -------------

target_link_libraries( RemoteController

                       ${log-lib} ${z-lib}

                       opus_static)