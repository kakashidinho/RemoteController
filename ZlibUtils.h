#ifndef HQREMOTE_ZLIB_H
#define HQREMOTE_ZLIB_H

#include "Data.h"

namespace HQRemote {
	//pass <level>=0 to use default compression level.
	//<dst>'s current size must be multiple of 64 bits
	void zlibCompress(const IData& src, int level, GrowableData& dst);
	DataRef zlibDecompress(const IData& src);
}

#endif