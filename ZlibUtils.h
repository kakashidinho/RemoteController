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

#ifndef HQREMOTE_ZLIB_H
#define HQREMOTE_ZLIB_H

#include "Data.h"

namespace HQRemote {
	//pass <level>=0 to use default compression level. <level>=-1 to use no compression at all
	//<dst>'s current size must be multiple of 64 bits
	HQREMOTE_API void HQ_FASTCALL zlibCompress(const IData& src, int level, GrowableData& dst);
	HQREMOTE_API void HQ_FASTCALL zlibCompress(const void* src, size_t size, int level, GrowableData& dst);
	HQREMOTE_API DataRef HQ_FASTCALL zlibDecompress(const IData& src);
	HQREMOTE_API DataRef HQ_FASTCALL zlibDecompress(const void* src, size_t size);
}

#endif