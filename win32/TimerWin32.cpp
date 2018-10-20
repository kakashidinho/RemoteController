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
#include <time.h>
#include <mutex>
#include <sstream>

#include "../Timer.h"

namespace HQRemote {
	static std::mutex g_lock;

	///get time check point
	void HQ_FASTCALL getTimeCheckPoint(time_checkpoint_t& checkPoint) {
		QueryPerformanceCounter(&checkPoint);
	}

	///get elapsed time in seconds between two check points
	double HQ_FASTCALL getElapsedTime(const time_checkpoint_t& point1, const time_checkpoint_t& point2) {
		return getElapsedTime64(point1.QuadPart, point2.QuadPart);
	}

	double HQ_FASTCALL getElapsedTime64(uint64_t point1, uint64_t point2) {
		std::lock_guard<std::mutex> lg(g_lock);

		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);

		return (double)(point2 - point1) / (double)frequency.QuadPart;
	}
	
	uint64_t HQ_FASTCALL getTimeCheckPoint64() {
		time_checkpoint_t time;
		getTimeCheckPoint(time);
		return convertToTimeCheckPoint64(time);
	}
	
	uint64_t HQ_FASTCALL convertToTimeCheckPoint64(const time_checkpoint_t& checkPoint){
		return checkPoint.QuadPart;
	}
	
	void HQ_FASTCALL convertToTimeCheckPoint(time_checkpoint_t& checkPoint, uint64_t time64) {
		checkPoint.QuadPart = time64;
	}

	CString HQ_FASTCALL getCurrentTimeStr() {
		std::lock_guard<std::mutex> lg(g_lock);

		SYSTEMTIME time;
		GetLocalTime(&time);

		std::stringstream ss;
		ss << time.wDay << "-" << time.wMonth << "-" << time.wYear << "-"
			<< time.wHour << "-" << time.wMinute << "-" << time.wSecond << "-" << time.wMilliseconds;

		auto str = ss.str();
		return CString(str.c_str(), str.size());
	}

	uint64_t HQ_FASTCALL generateIDFromTime(const time_checkpoint_t& time) {
		return time.QuadPart;
	}
}