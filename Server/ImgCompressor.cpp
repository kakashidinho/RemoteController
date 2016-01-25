//
//  JpegCompressor.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include <stdio.h>
#include <assert.h>

#include "ImgCompressor.h"

namespace HQRemote {

	JpegImgCompressor::JpegImgCompressor(bool outputLowRes, bool outputFlipped)
		:m_outputLowRes(outputLowRes), m_flip(outputFlipped)
	{
	}

	DataRef JpegImgCompressor::compress(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels) {
		return convertToJpeg(src, width, height, numChannels, m_outputLowRes, m_flip);
	}
}
