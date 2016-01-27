//
//  JpegCompressor.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include <stdio.h>
#include <assert.h>

#include "../ZlibUtils.h"
#include "ImgCompressor.h"

namespace HQRemote {
	/*-------------- JpegImgCompressor ----------------*/
	JpegImgCompressor::JpegImgCompressor(bool outputLowRes, bool outputFlipped)
		:m_outputLowRes(outputLowRes), m_flip(outputFlipped)
	{
	}

	DataRef JpegImgCompressor::compress(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels) {
		return convertToJpeg(src, width, height, numChannels, m_outputLowRes, m_flip);
	}

	/*-------------- ZlibImgComressor ----------------*/
	ZlibImgComressor::ZlibImgComressor(int level) {
		m_level = level;
	}

	DataRef ZlibImgComressor::compress(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels) {
		auto compressedData = std::make_shared<GrowableData>();
		
		//init metadata
		compressedData->push_back(&width, sizeof(width));
		compressedData->push_back(&height, sizeof(height));
		compressedData->push_back(&numChannels, sizeof(numChannels));
		compressedData->expand(4);//padding

		try {
			zlibCompress(*src, m_level, *compressedData);
		}
		catch (...)
		{
			return nullptr;
		}

		return compressedData;
	}

	DataRef ZlibImgComressor::decompress(ConstDataRef src, uint32_t& width, uint32_t &height, unsigned int numChannels) {
		//read meta data
		memcpy(&width, src->data(), sizeof(width));
		memcpy(&height, src->data() + sizeof(width), sizeof(height));
		memcpy(&numChannels, src->data() + (sizeof(width) + sizeof(height)), sizeof(numChannels));

		ConstDataSegment compressedData(src, (sizeof(width) + sizeof(height) + sizeof(numChannels) + 4));

		try {
			return zlibDecompress(compressedData);
		}
		catch (...) {
			return nullptr;
		}
	}
}
