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
#include <sys/time.h>
#include <mutex>
#include <sstream>

#include "../Timer.h"

namespace HQRemote {
	static std::mutex g_lock;
	
	///get time check point
	void getTimeCheckPoint(time_checkpoint_t& checkPoint) {
		checkPoint = mach_absolute_time();
	}
	
	///get elapsed time in seconds between two check points
	double getElapsedTime(const time_checkpoint_t& point1 , const time_checkpoint_t& point2) {
		std::lock_guard<std::mutex> lg(g_lock);
		
		static bool s_init = false;
		static mach_timebase_info_data_t s_timebase;
		static double s_factor = 0.0;
		if (!s_init)
		{
			mach_timebase_info(&s_timebase);
			s_factor = s_timebase.numer / (1e9 * s_timebase.denom);
			s_init = true;
		}
		
		time_checkpoint_t elapsed = (point2 - point1);
		double time = (double) (elapsed * s_factor);
		
		return time;
	}
	
	double getElapsedTime64(uint64_t point1, uint64_t point2) {
		return getElapsedTime(point1, point2);
	}

	uint64_t getTimeCheckPoint64() {
		time_checkpoint_t time;
		getTimeCheckPoint(time);
		return time;
	}
	
	uint64_t convertToTimeCheckPoint64(const time_checkpoint_t& checkPoint){
		return checkPoint;
	}
	
	void convertToTimeCheckPoint(time_checkpoint_t& checkPoint, uint64_t time64) {
		checkPoint = time64;
	}
	
	CString getCurrentTimeStr() {
		std::lock_guard<std::mutex> lg(g_lock);

		timeval tv;
		tm* ptm;
		gettimeofday(&tv, NULL);
		ptm = localtime(&tv.tv_sec);
		
		std::stringstream ss;
		ss << ptm->tm_mday << "-" << ptm->tm_mon + 1 << "-" << ptm->tm_year + 1900 << "-"
		<< ptm->tm_hour << "-" << ptm->tm_min << "-" << ptm->tm_sec << "-" << tv.tv_usec;
		
		auto str = ss.str();
		return CString(str.c_str(), str.size());
	}

	uint64_t generateIDFromTime(const time_checkpoint_t& time) {
		return time;
	}
}
