//
//  ImgCompressor.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#ifndef Remote_ImgCompressor_h
#define Remote_ImgCompressor_h

#include "../Common.h"
#include "../Data.h"

namespace HQRemote {
	class HQREMOTE_API IImgCompressor {
	public:
		virtual ~IImgCompressor() {}

		virtual DataRef compress(ConstDataRef src, uint64_t id, uint32_t width, uint32_t height, unsigned int numChannels) = 0;
		virtual bool canSupportMultiThreads() const { return true; }
	};

	class HQREMOTE_API JpegImgCompressor : public IImgCompressor {
	public:
		JpegImgCompressor(bool outputLowRes, bool outputFlipped);

		virtual DataRef compress(ConstDataRef src, uint64_t id, uint32_t width, uint32_t height, unsigned int numChannels) override;

	private:
		bool m_outputLowRes;
		bool m_flip;
	};

	class HQREMOTE_API ZlibImgComressor : public IImgCompressor{
	public:
		ZlibImgComressor(int level = 0);//pass 0 to use default compression level, -1 to disable compression

		virtual DataRef compress(ConstDataRef src, uint64_t id, uint32_t width, uint32_t height, unsigned int numChannels) override;
		DataRef compress(const void* src, size_t size, uint32_t width, uint32_t height, unsigned int numChannels);
		DataRef decompress(ConstDataRef src, uint32_t& width, uint32_t &height, unsigned int& numChannels);
		DataRef decompress(const void* src, size_t srcSize, uint32_t& width, uint32_t &height, unsigned int& numChannels);
	private:
		int m_level;
	};

	DataRef convertToJpeg(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
	DataRef convertToPng(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
}

#endif /* JpegCompressor_h */
