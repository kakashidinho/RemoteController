//
//  TimerLinux.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 26/2/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include <sys/time.h>
#include <mutex>
#include <sstream>

#include "../Timer.h"

namespace HQRemote {
	static std::mutex g_lock;
	
	struct Time : public time_checkpoint_t {
		Time()
		{
			clock_gettime(CLOCK_MONOTONIC , this);
		}
	};
	
	static Time& getStartTime() {
		static Time start;
		return start;
	}
	
	static inline uint64_t _convertToTimeCheckPoint64(const time_checkpoint_t& time){
		return (uint64_t)time.tv_sec * 1e9 + time.tv_nsec;
	}
	
	static inline void _convertToTimeCheckPoint(time_checkpoint_t& checkPoint, uint64_t time64) {
		checkPoint.tv_sec = time64 / 1e9;
		checkPoint.tv_nsec = time64 - checkPoint.tv_sec;
	}
	
	///get time check point
	void getTimeCheckPoint(time_checkpoint_t& checkPoint) {
		auto & startTime = getStartTime();
		
		clock_gettime(CLOCK_MONOTONIC , &checkPoint);
		
		//avoid time overflow
		checkPoint.tv_sec -= startTime.tv_sec;
		checkPoint.tv_nsec -= startTime.tv_nsec;
	}
	
	///get elapsed time in seconds between two check points
	double getElapsedTime(const time_checkpoint_t& point1 , const time_checkpoint_t& point2) {
		time_t sec = point2.tv_sec - point1.tv_sec;
		long nsec = point2.tv_nsec - point1.tv_nsec;
		
		return ((double)sec + (double)nsec / 1e9);
	}
	
	double getElapsedTime64(uint64_t point1, uint64_t point2) {
		time_checkpoint_t t1, t2;
		
		_convertToTimeCheckPoint(t1, point1);
		_convertToTimeCheckPoint(t2, point2);
		
		return getElapsedTime(t1, t2);
	}
	
	uint64_t getTimeCheckPoint64() {
		time_checkpoint_t time;
		getTimeCheckPoint(time);
		return _convertToTimeCheckPoint64(time);
	}
	
	uint64_t convertToTimeCheckPoint64(const time_checkpoint_t& time){
		return _convertToTimeCheckPoint64(time);
	}
	
	void convertToTimeCheckPoint(time_checkpoint_t& checkPoint, uint64_t time64) {
		_convertToTimeCheckPoint(checkPoint, time64);
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
		return _convertToTimeCheckPoint64(time);
	}
}
