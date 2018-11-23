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

#ifndef Remote_ImgCompressor_h
#define Remote_ImgCompressor_h

#include "../Common.h"
#include "../Data.h"

namespace HQRemote {
	class HQREMOTE_API IImgCompressor {
	public:
		struct CompressArgs {
			uint32_t width, height;
			unsigned int numChannels;
			uint64_t timeStamp; // millisecond
			bool outImportantFrame; // write true to this upon return to indicate the frame is important
		};

		virtual ~IImgCompressor() {}

		virtual DataRef compress(ConstDataRef src, uint64_t id, uint32_t width, uint32_t height, unsigned int numChannels) {
			return nullptr;
		}

		// this version allows the callee to modify the id of the compressed frame
		virtual DataRef compress2(ConstDataRef src, const uint64_t id, CompressArgs& info) {
			return compress(src, id, info.width, info.height, info.numChannels);
		}

		// this is only for object that returns false from canSupportMultiThreads(). Which means it operates in pipeline mode
		virtual DataRef anyMoreCompressedOutput(bool& important) { return nullptr; }

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
		static DataRef decompress(ConstDataRef src, uint32_t& width, uint32_t &height, unsigned int& numChannels);
		static DataRef decompress(const void* src, size_t srcSize, uint32_t& width, uint32_t &height, unsigned int& numChannels);
	private:
		int m_level;
	};

	DataRef convertToJpeg(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
	DataRef convertToPng(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip);
}

#endif /* JpegCompressor_h */
