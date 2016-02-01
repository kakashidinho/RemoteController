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
		return compress(src->data(), src->size(), width, height, numChannels);
	}

	DataRef ZlibImgComressor::compress(const void* src, size_t size, uint32_t width, uint32_t height, unsigned int numChannels) {
		auto compressedData = std::make_shared<GrowableData>();
		
		//init metadata
		compressedData->push_back(&width, sizeof(width));
		compressedData->push_back(&height, sizeof(height));
		compressedData->push_back(&numChannels, sizeof(numChannels));
		compressedData->expand(4);//padding

		try {
			zlibCompress(src, size, m_level, *compressedData);
		}
		catch (...)
		{
			return nullptr;
		}

		return compressedData;
	}

	DataRef ZlibImgComressor::decompress(ConstDataRef src, uint32_t& width, uint32_t &height, unsigned int& numChannels) {
		return decompress(src->data(), src->size(), width, height, numChannels);
	}

	DataRef ZlibImgComressor::decompress(const void* src, size_t srcSize, uint32_t& width, uint32_t &height, unsigned int& numChannels) {
		//read meta data
		auto csrc = (const unsigned char*)src;

		memcpy(&width, csrc, sizeof(width));
		memcpy(&height, csrc + sizeof(width), sizeof(height));
		memcpy(&numChannels, csrc + (sizeof(width) + sizeof(height)), sizeof(numChannels));

		auto compressedDataOffset = (sizeof(width) + sizeof(height) + sizeof(numChannels) + 4);
		auto compressedData = (csrc + compressedDataOffset);
		auto compressedSize = srcSize - compressedDataOffset;

		try {
			return zlibDecompress(compressedData, compressedSize);
		}
		catch (...) {
			return nullptr;
		}
	}
}
