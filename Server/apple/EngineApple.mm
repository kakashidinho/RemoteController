//
//  EngineApple.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

#include <mach-o/dyld.h>

#include "../Engine.h"



namespace HQRemote {
	//Helper functions
	static CGImageRef createCGImage(ConstDataRef frame, uint32_t width, uint32_t height, unsigned int numChannels)
	{
		auto srcDataProvider = CGDataProviderCreateWithData(NULL,
															frame->data(),
															frame->size(),
															NULL);
		if (srcDataProvider == NULL)
			return nullptr;
		
		size_t bitsPerChannel = 8;
		size_t bitsPerPixel = numChannels * bitsPerChannel;
		size_t bytesPerRow = numChannels * width;
		auto colorSpace = CGColorSpaceCreateDeviceRGB();
		if (colorSpace == NULL)
		{
			CGDataProviderRelease(srcDataProvider);
			return nullptr;
		}
		auto srcImage = CGImageCreate(width, height,
									  bitsPerChannel,
									  bitsPerPixel,
									  bytesPerRow,
									  colorSpace,
									  kCGImageAlphaNoneSkipFirst,//TODO: we don't support alpha channel for now
									  srcDataProvider,
									  NULL,
									  true,
									  kCGRenderingIntentDefault);
		
		return srcImage;
	}
	
	static CVPixelBufferRef pixelBufferFromFrameData(ConstDataRef frame,
													 uint32_t width, uint32_t height,
													 unsigned int numChannels,
													 uint32 destWidth, uint32_t destHeight,
													 bool flip)
	{
		CGImageRef image = createCGImage(frame, width, height, numChannels);
		
		NSDictionary *options = [[NSDictionary alloc] initWithObjectsAndKeys:
								 [NSNumber numberWithBool:YES], kCVPixelBufferCGImageCompatibilityKey,
								 [NSNumber numberWithBool:YES], kCVPixelBufferCGBitmapContextCompatibilityKey,
								 nil];
		
		CVPixelBufferRef pxbuffer = NULL;
		CVPixelBufferCreate(kCFAllocatorDefault,
							destWidth,
							destHeight, kCVPixelFormatType_32ARGB, (__bridge CFDictionaryRef) options,
							&pxbuffer);
		
		CVPixelBufferLockBaseAddress(pxbuffer, 0);
		void *pxdata = CVPixelBufferGetBaseAddress(pxbuffer);
		
		CGColorSpaceRef rgbColorSpace = CGColorSpaceCreateDeviceRGB();
		CGContextRef context = CGBitmapContextCreate(pxdata,
													 destWidth,
													 destHeight,
													 8, 4*destWidth,
													 rgbColorSpace,
													 (CGBitmapInfo)kCGImageAlphaNoneSkipFirst);
		
		CGContextConcatCTM(context, CGAffineTransformMakeRotation(0));
		
		if (flip) {
			CGAffineTransform flipVertical = CGAffineTransformMake( 1, 0, 0, -1, 0, destHeight );
			CGContextConcatCTM(context, flipVertical);
		}
		
		//    CGAffineTransform flipHorizontal = CGAffineTransformMake( -1.0, 0.0, 0.0, 1.0, destWidth, 0.0 );
		//    CGContextConcatCTM(context, flipHorizontal);
		
		
		CGContextDrawImage(context, CGRectMake(0, 0, destWidth, destHeight), image);
		CGColorSpaceRelease(rgbColorSpace);
		CGContextRelease(context);
		CGImageRelease(image);
		
		CVPixelBufferUnlockBaseAddress(pxbuffer, 0);
		
		
		return pxbuffer;
	}
	
	/*--------- Engine --------*/
	struct Engine::Impl {
		Impl() {
			cleanupVideoWriter();
		}
		~Impl() {
			cleanupVideoWriter();
		}
		
		uint32_t videoWidth;
		uint32_t videoHeight;
		AVAssetWriter *videoWriter;
		AVAssetWriterInput* videoWriterInput;
		AVAssetWriterInputPixelBufferAdaptor *videoAdaptor;
		CVPixelBufferRef videoPixelBuffer;
		double lastVideoFrameTime;
		
		void cleanupVideoWriter() {
			videoWriter = nil;
			videoWriterInput = nil;
			videoAdaptor = nil;
			videoPixelBuffer = NULL;
			lastVideoFrameTime = 0;
		}
	};
	

	void Engine::platformConstruct() {
		m_impl = new Impl();
		
	}
	void Engine::platformDestruct() {
		platformEndRecording();
		
		delete m_impl;
	}
	
	std::string Engine::platformGetWritableFolder() {
		std::string path = "../HQRemoteControllerData/";
		
		//create the directory
		mkdir(path.c_str(), 0775);
		
		return path;
	}
	
	std::string Engine::platformGetAppName() {
		char dummy[1];
		uint32_t bufSize = sizeof(dummy);
		
		if (_NSGetExecutablePath(dummy, &bufSize) != -1)
			return "noname";
		
		char* buf = (char*)malloc(bufSize);
		if (buf == NULL)
			return "noname";
		
		if (_NSGetExecutablePath(buf, &bufSize) != 0)
		{
			free(buf);
			return "noname";
		}
		
		std::string name = buf;
		free(buf);
		
		auto slash = name.find_last_of('/');
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		
		return name;
	}
	
	
	void Engine::platformStartRecording() {
		NSError *error = nil;
		NSString* path = [NSString stringWithFormat:@"%s%s-%s.mp4",
						  platformGetWritableFolder().c_str(),
						  platformGetAppName().c_str(),
						  getCurrentTimeStr().c_str()];
		//check if file exist
		NSFileManager *fileManager = [NSFileManager defaultManager];
		if ([fileManager fileExistsAtPath:path]){
			[fileManager removeItemAtPath:path error:nil];
		}
		
		m_impl->videoWriter = [[AVAssetWriter alloc] initWithURL:[NSURL fileURLWithPath:path]
												fileType:AVFileTypeMPEG4
												   error:&error];
		
		if (error != nil) {
			fprintf(stderr, "%s\n", [[error localizedDescription] UTF8String]);
			m_impl->videoWriter = nil;
			return;
		}
		
		auto videoWidth = m_frameCapturer->getFrameWidth();
		auto videoHeight = m_frameCapturer->getFrameHeight();
		//iPhone 6+ hack:
		if (videoWidth < videoHeight && (videoWidth > 1080 || videoHeight > 1920)) {
			float wRatio = 1080.f / videoWidth;
			float hRatio = 1920.f / videoHeight;
			
			if (fabs(wRatio - hRatio) < 0.0001f) {
				videoWidth = 1080;
				videoHeight = 1920;
			}
		}
		else if (videoWidth > videoHeight && (videoWidth > 1920 || videoHeight > 1080)) {
			float wRatio = 1920.f / videoWidth;
			float hRatio = 1080.f / videoHeight;
			
			if (fabs(wRatio - hRatio) < 0.0001f) {
				videoWidth = 1920;
				videoHeight = 1080;
			}
		}
		
		m_impl->videoWidth = videoWidth;
		m_impl->videoHeight = videoHeight;
		
		NSDictionary *videoSettings = [[NSDictionary alloc] initWithObjectsAndKeys:
									   AVVideoCodecH264, AVVideoCodecKey,
									   [NSNumber numberWithUnsignedInt:videoWidth], AVVideoWidthKey,
									   [NSNumber numberWithUnsignedInt:videoHeight], AVVideoHeightKey,
									   nil];
		
		m_impl->videoWriterInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:videoSettings];
		
		m_impl->videoAdaptor = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:m_impl->videoWriterInput
																						   sourcePixelBufferAttributes:nil];
		
		[m_impl->videoWriter addInput:m_impl->videoWriterInput];
		
		//Start a session:
		[m_impl->videoWriter startWriting];
		[m_impl->videoWriter startSessionAtSourceTime:kCMTimeZero];
	}
	
	void Engine::platformRecordFrame(double t, ConstDataRef frame) {
		if (m_impl->videoAdaptor == nil)
			return;
		
		//First time only
		if (m_impl->videoPixelBuffer == NULL)
			CVPixelBufferPoolCreatePixelBuffer (NULL, m_impl->videoAdaptor.pixelBufferPool, &m_impl->videoPixelBuffer);
		
		m_impl->videoPixelBuffer = pixelBufferFromFrameData(frame,
															m_frameCapturer->getFrameWidth(),
															m_frameCapturer->getFrameHeight(),
															m_frameCapturer->getNumColorChannels(),
															m_impl->videoWidth,
															m_impl->videoHeight,
															true);
		
		if (m_impl->videoPixelBuffer)
		{
			Float64 interval = t;
			int32_t timeScale = 1.0/(t - m_impl->lastVideoFrameTime);
			
			/**/
			CMTime presentTime=CMTimeMakeWithSeconds(interval, MAX(33, timeScale));
			
			// append buffer
			[m_impl->videoAdaptor appendPixelBuffer:m_impl->videoPixelBuffer withPresentationTime:presentTime];
			CVPixelBufferRelease(m_impl->videoPixelBuffer);
		}
		
		m_impl->lastVideoFrameTime = t;
	}
	
	void Engine::platformEndRecording() {
		if (m_impl->videoAdaptor == nil)
			return;
		
		//Finish the session:
		[m_impl->videoWriterInput markAsFinished];
		
		/**
		 *  fix bug on iOS7 is not work, finishWritingWithCompletionHandler method is not work
		 */
		// http://stackoverflow.com/questions/18885735/avassetwriter-fails-when-calling-finishwritingwithcompletionhandler
		Float64 interval = m_impl->lastVideoFrameTime;
		
		CMTime cmTime = CMTimeMake(interval, 1);
		[m_impl->videoWriter endSessionAtSourceTime:cmTime];
		
		if ([m_impl->videoWriter respondsToSelector:@selector(finishWritingWithCompletionHandler:)])
		{
			auto finished = std::make_shared<std::atomic<bool> > (false);
			[m_impl->videoWriter finishWritingWithCompletionHandler:^{
				*finished = true;
			}];
			
			//wait for the writing to finish
			while (!(*finished)){
				usleep(10000);
			}
		}
		else
		{
			[m_impl->videoWriter finishWriting];
		}
		
		CVPixelBufferPoolRelease(m_impl->videoAdaptor.pixelBufferPool);
		
		m_impl->cleanupVideoWriter();
	}
}