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

#include "ZlibUtils.h"
#include "Common.h"

#include <zlib.h>

#include <assert.h>
#include <string>
#include <limits>

#ifdef max
#	undef max
#endif

#define COMPRESS_CHUNK_SIZE (256 * 1024)

namespace HQRemote {
	void HQ_FASTCALL zlibCompress(const IData& uncompressedData, int level, GrowableData& dst) {
		zlibCompress(uncompressedData.data(), uncompressedData.size(), level, dst);
	}

	void HQ_FASTCALL zlibCompress(const void* uncompressedData, size_t size, int level, GrowableData& dst) {
		if (level == 0)
			level = Z_DEFAULT_COMPRESSION;
		else if (level == -1)
			level = 0;

		z_stream sz;
		memset(&sz, 0, sizeof(sz));

		//placeholder for uncompressed size
		auto uncompressedSizeOff = dst.size();
		assert(uncompressedSizeOff % sizeof(uint64_t) == 0);

		dst.expand(sizeof(uint64_t));

		//compress the data
		if (deflateInit(&sz, level) != Z_OK)
		//if (deflateInit2(&sz, level, Z_DEFLATED, 15, 8, Z_DEFAULT_COMPRESSION) != Z_OK)
			throw std::runtime_error("deflateInit failed");
		if (size > std::numeric_limits<uint32_t>::max())
			throw std::runtime_error("uncompressed data too big");

		int re;
		unsigned char buffer[COMPRESS_CHUNK_SIZE];
		sz.next_in = (unsigned char*)uncompressedData;
		assert(size <= std::numeric_limits<uint32_t>::max());
		sz.avail_in = (uint32_t)size;
		sz.next_out = buffer;
		sz.avail_out = sizeof(buffer);

		while ((re = deflate(&sz, Z_FINISH)) != Z_STREAM_END && re == Z_OK) {
			dst.push_back(buffer, sizeof(buffer) - sz.avail_out);

			sz.next_out = buffer;
			sz.avail_out = sizeof(buffer);
		}

		dst.push_back(buffer, sizeof(buffer) - sz.avail_out);

		//finalize
		deflateEnd(&sz);
		uint64_t& uncompressedSize = *(uint64_t*)(dst.data() + uncompressedSizeOff);

		uncompressedSize = size;

		if (re != Z_STREAM_END)
		{
			throw std::runtime_error("compression failed");
		}
	}

	DataRef HQ_FASTCALL zlibDecompress(const IData& src) {
		return zlibDecompress(src.data(), src.size());
	}

	DataRef HQ_FASTCALL zlibDecompress(const void* src, size_t size) {
		const uint64_t& uncompressedSize = *(const uint64_t*)(src);
		const unsigned char* compressedData = (const unsigned char*)(&uncompressedSize + 1);
		_ssize_t compressedSize = (_ssize_t)size - sizeof(uncompressedSize);
		if (compressedSize < 0)
			throw  std::runtime_error("Size is too small for decompression");

		auto decompressedData = std::make_shared<CData>((size_t)uncompressedSize);
		uLongf decompressedSize = decompressedData->size();
		int re;
		if ((re = uncompress(decompressedData->data(), &decompressedSize, compressedData, compressedSize)) != Z_OK
			|| decompressedSize != uncompressedSize) {
			throw  std::runtime_error("Decompression failed");
		}

		return decompressedData;
	}
}
