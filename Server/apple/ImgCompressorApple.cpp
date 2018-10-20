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

#include <stdio.h>

#include <TargetConditionals.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#if TARGET_OS_IPHONE
#	include <MobileCoreServices/MobileCoreServices.h>
#else
#	include <CoreServices/CoreServices.h>
#endif

#include "ImgCompressor.h"

#ifndef MIN
#	define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

namespace HQRemote {
	struct CFDataWrapper : public IData {
		CFDataWrapper(size_t size) {
			cfData = CFDataCreateMutable(NULL, size);
		}
		
		~CFDataWrapper() {
			if (cfData)
			{
				auto retainCount = CFGetRetainCount(cfData);
				CFRelease(cfData);
			}
		}
		
		virtual unsigned char* data() override { return CFDataGetMutableBytePtr(cfData); }
		virtual const unsigned char* data() const override { return CFDataGetMutableBytePtr(cfData); }
		
		virtual size_t size() const override { return CFDataGetLength(cfData); }
		
		CFMutableDataRef getCFData() { return cfData; }
	private:
		CFMutableDataRef cfData;
	};
	
	DataRef convertToImgType(const CFStringRef outputType, ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip) {
		auto dstRef = std::make_shared<CFDataWrapper>(0);
		
		auto srcDataProvider = CGDataProviderCreateWithData(NULL,
															src->data(),
															src->size(),
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
		
		if (srcImage != NULL)
		{
			auto dstCreator = CGImageDestinationCreateWithData(dstRef->getCFData(),
															   outputType,
															   1,
															   NULL);
			
			if (dstCreator) {
				if (outputlowRes || flip)
				{
					//resize the image to lower resolution
					int outputWidth, outputHeight;
					
					if (outputlowRes) {
						float widthRatio = 200.f / width;
						float heightRatio = 400.f / height;
						
						float ratio = MIN(widthRatio, heightRatio);
						int lowres_width = (int)(width * ratio);
						int lowres_height = (int)(height * ratio);
						
						outputWidth = lowres_width;
						outputHeight = lowres_height;
					}
					else {
						outputWidth = width;
						outputHeight = height;
					}
					
					CGContextRef context = CGBitmapContextCreate(NULL, outputWidth, outputHeight,
																 bitsPerChannel,
																 4 * outputWidth,
																 colorSpace,
																 kCGImageAlphaNoneSkipFirst);
					if(context != NULL)
					{
						if (flip)
						{
							CGAffineTransform flipVertical = CGAffineTransformMake( 1, 0, 0, -1, 0, outputHeight );
							CGContextConcatCTM(context, flipVertical);
						}
							
						
						CGContextDrawImage(context, CGRectMake(0, 0, outputWidth, outputHeight), srcImage);
						// extract resulting image from context
						CGImageRef outputImgRef = CGBitmapContextCreateImage(context);
						CGImageDestinationAddImage(dstCreator, outputImgRef, NULL);
						CGImageDestinationFinalize(dstCreator);
						
						//TODO: error checking
						CGImageRelease(outputImgRef);
						CGContextRelease(context);
					}
					else
					{
						outputlowRes = flip = false;//fallback to high res
					}
				}// if (outputlowRes || flip)
				
				//original res
				if (!outputlowRes && !flip) {
					CGImageDestinationAddImage(dstCreator, srcImage, NULL);
					CGImageDestinationFinalize(dstCreator);
				}
				CFRelease(dstCreator);
			}
		}//if (srcImage != NULL)
		
		if (srcDataProvider)
			CGDataProviderRelease(srcDataProvider);
		if (colorSpace)
			CGColorSpaceRelease(colorSpace);
		if (srcImage)
			CGImageRelease(srcImage);
		
		if (dstRef->size() > 0)
			return dstRef;
		return nullptr;
	}
	
	DataRef convertToJpeg(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip)
	{
		return convertToImgType(kUTTypeJPEG, src, width, height, numChannels, outputlowRes, flip);
	}
	
	DataRef convertToPng(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip)
	{
		return convertToImgType(kUTTypePNG, src, width, height, numChannels, outputlowRes, flip);
	}
}