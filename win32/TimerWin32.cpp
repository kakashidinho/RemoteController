#include <stdio.h>
#include <time.h>
#include <mutex>
#include <sstream>

#include "../Timer.h"

namespace HQRemote {
	static std::mutex g_lock;

	///get time check point
	void getTimeCheckPoint(time_checkpoint_t& checkPoint) {
		QueryPerformanceCounter(&checkPoint);
	}

	///get elapsed time in seconds between two check points
	double getElapsedTime(const time_checkpoint_t& point1, const time_checkpoint_t& point2) {
		std::lock_guard<std::mutex> lg(g_lock);

		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);

		return (double)(point2.QuadPart - point1.QuadPart) / (double)frequency.QuadPart;
	}

	std::string getCurrentTimeStr() {
		std::lock_guard<std::mutex> lg(g_lock);

		SYSTEMTIME time;
		GetLocalTime(&time);

		std::stringstream ss;
		ss << time.wDay << "-" << time.wMonth << "-" << time.wYear << "-"
			<< time.wHour << "-" << time.wMinute << "-" << time.wSecond << "-" << time.wMilliseconds;

		return ss.str();
	}

	uint64_t generateIDFromTime(const time_checkpoint_t& time) {
		return time.QuadPart;
	}
}