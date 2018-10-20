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

#include <stdio.h>
#include <assert.h>

#include "ImgCompressor.h"

#include "jpeglib.h"

#ifndef MIN
#	define MIN(a,b) (a) < (b)? (a) : (b)
#endif

namespace HQRemote {
	DataRef convertToJpeg(ConstDataRef src, uint32_t width, uint32_t height, unsigned int numChannels, bool outputlowRes, bool flip) {
		//TODO: deal with <outputlowRes>

		unsigned char* compressedFrameData = NULL;
		unsigned long compressedFrameSize = 0;
		//compress the frame using JPEG lib
		jpeg_compress_struct cinfo;
		jpeg_error_mgr jerr;

		JSAMPROW row_pointer[1];
		cinfo.err = jpeg_std_error(&jerr);
		jpeg_create_compress(&cinfo);
		jpeg_mem_dest(&cinfo, &compressedFrameData, &compressedFrameSize);

		/* Setting the parameters of the output file here */
		//TODO: only support 3 color channels for now
		assert(numChannels == 3);
		cinfo.image_width = width;
		cinfo.image_height = height;
		cinfo.input_components = numChannels;
		cinfo.in_color_space = JCS_RGB;
		/* default compression parameters, we shouldn't be worried about these */

		jpeg_set_defaults(&cinfo);
		cinfo.num_components = 3;
		//cinfo.data_precision = 4;
		cinfo.dct_method = JDCT_FLOAT;
		jpeg_set_quality(&cinfo, outputlowRes ? 40 : 80, TRUE);//TODO: a bit low quality

		/* Now do the compression .. */
		jpeg_start_compress(&cinfo, TRUE);
		/* like reading a file, this time write one row at a time */
		if (flip)
		{
			while (cinfo.next_scanline < cinfo.image_height)
			{
				row_pointer[0] = (unsigned char*)&src->data()[(cinfo.image_height - cinfo.next_scanline - 1) * cinfo.image_width * cinfo.input_components];
				jpeg_write_scanlines(&cinfo, row_pointer, 1);
			}
		}//flip
		else {
			while (cinfo.next_scanline < cinfo.image_height)
			{
				row_pointer[0] = (unsigned char*)&src->data()[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
				jpeg_write_scanlines(&cinfo, row_pointer, 1);
			}
		}//flip
		/* similar to read file, clean up after we're done compressing */
		jpeg_finish_compress(&cinfo);
		jpeg_destroy_compress(&cinfo);

		//TODO: error checking

		if (compressedFrameData != NULL) {
			return std::make_shared<CData>(
				compressedFrameData,
				compressedFrameSize,
				[](unsigned char*data) {
				free(data);
			});
		}

		return nullptr;
	}
}