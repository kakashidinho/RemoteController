//
//  ImgCompressor.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#ifndef Remote_ImgCompressor_h
#define Remote_ImgCompressor_h

#include "../Data.h"

namespace HQRemote {
	class IImgCompressor {
	public:
		virtual ~IImgCompressor() {}

		virtual DataRef compress(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels) = 0;
	};

	class JpegImgCompressor : public IImgCompressor {
	public:
		JpegImgCompressor(bool outputLowRes, bool outputFlipped);

		virtual DataRef compress(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels) override;

	private:
		bool m_outputLowRes;
		bool m_flip;
	};

	DataRef convertToJpeg(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
	DataRef convertToPng(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
}

#endif /* JpegCompressor_h */
