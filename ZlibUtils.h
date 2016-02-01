#ifndef HQREMOTE_ZLIB_H
#define HQREMOTE_ZLIB_H

#include "Data.h"

namespace HQRemote {
	//pass <level>=0 to use default compression level.
	//<dst>'s current size must be multiple of 64 bits
	HQREMOTE_API void HQ_FASTCALL zlibCompress(const IData& src, int level, GrowableData& dst);
	HQREMOTE_API void HQ_FASTCALL zlibCompress(const void* src, size_t size, int level, GrowableData& dst);
	HQREMOTE_API DataRef HQ_FASTCALL zlibDecompress(const IData& src);
	HQREMOTE_API DataRef HQ_FASTCALL zlibDecompress(const void* src, size_t size);
}

#endif