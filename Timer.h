//
//  Timer.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 12/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#ifndef Remote_Timer_h
#define Remote_Timer_h

#include "Common.h"
#include "CString.h"

#include <string>
#include <mutex>

#if defined WIN32/*---windows---*/

#	include <windows.h>

#elif defined __APPLE__/*----apple------*/

#	include <mach/mach_time.h>
#	include <time.h>

#elif defined __ANDROID__ /*----- android -----*/
#	include <time.h>

#else

#	error need implementation

#endif//#if defined WIN32

namespace HQRemote {
#if defined WIN32/*---windows---*/
	
	typedef LARGE_INTEGER time_checkpoint_t;

	class HQREMOTE_API TimeCompare {
	public:
		bool operator() (const time_checkpoint_t& t1, const time_checkpoint_t& t2) const {
			return t1.QuadPart < t2.QuadPart;
		}
	};
	
#elif defined __APPLE__/*----apple------*/
	
	typedef uint64_t time_checkpoint_t;

	class HQREMOTE_API TimeCompare {
	public:
		bool operator() (const time_checkpoint_t& t1, const time_checkpoint_t& t2) const {
			return t1 < t2;
}
	};
	
#elif defined __ANDROID__ /*----- android -----*/
	typedef struct timespec time_checkpoint_t;
	
	class HQREMOTE_API TimeCompare {
	public:
		bool operator() (const time_checkpoint_t& t1, const time_checkpoint_t& t2) const {
			return t1.tv_sec < t2.tv_sec || (t1.tv_sec == t2.tv_sec && t1.tv_nsec < t2. tv_nsec);
		}
	};
#else
	
#	error need implementation
	
#endif//#if defined WIN32
	
	///get time check point
	HQREMOTE_API void HQ_FASTCALL getTimeCheckPoint(time_checkpoint_t& checkPoint);
	///get elapsed time in seconds between two check points
	HQREMOTE_API double HQ_FASTCALL getElapsedTime(const time_checkpoint_t& point1 , const time_checkpoint_t& point2);
	
	HQREMOTE_API CString HQ_FASTCALL getCurrentTimeStr();
	
	HQREMOTE_API uint64_t HQ_FASTCALL getTimeCheckPoint64();
	HQREMOTE_API uint64_t HQ_FASTCALL convertToTimeCheckPoint64(const time_checkpoint_t& checkPoint);
	HQREMOTE_API void HQ_FASTCALL convertToTimeCheckPoint(time_checkpoint_t& checkPoint, uint64_t time64);

	HQREMOTE_API double HQ_FASTCALL getElapsedTime64(uint64_t point1, uint64_t point2);

	//generate increasing id based on time
	HQREMOTE_API uint64_t HQ_FASTCALL generateIDFromTime(const time_checkpoint_t& time);
	static inline uint64_t HQ_FASTCALL generateIDFromTime() {
		time_checkpoint_t time;
		getTimeCheckPoint(time);
		return generateIDFromTime(time);
	}

	/*------------- ScopedTimeProfiler ----------------*/
	class ScopedTimeProfiler {
	public:
		ScopedTimeProfiler(const std::string& logPrefix, std::mutex& avgTimeLock, float& avgTime, float& totalWindowTime)
			: m_logPrefix(logPrefix), m_avgTimeLock(&avgTimeLock), m_avgTime(avgTime), m_totalWindowTime(totalWindowTime)
		{
			m_startTime = HQRemote::getTimeCheckPoint64();
		}

		ScopedTimeProfiler(const std::string& logPrefix, float& avgTime, float& totalWindowTime)
			: m_logPrefix(logPrefix), m_avgTimeLock(nullptr), m_avgTime(avgTime), m_totalWindowTime(totalWindowTime)
		{
			m_startTime = HQRemote::getTimeCheckPoint64();
		}


		~ScopedTimeProfiler()
		{
			auto endTime = HQRemote::getTimeCheckPoint64();
			auto elapsed = (float)HQRemote::getElapsedTime64(m_startTime, endTime);

			if (m_avgTimeLock)
				m_avgTimeLock->lock();
			m_avgTime = 0.8f * m_avgTime + elapsed;

			m_totalWindowTime = m_totalWindowTime + elapsed;

			if (m_totalWindowTime >= 0.5)
			{
				m_totalWindowTime = 0;
				HQRemote::Log("%s: avg time %.2f (ms)\n", m_logPrefix.c_str(), m_avgTime * 1000.f);
			}

			if (m_avgTimeLock)
				m_avgTimeLock->unlock();
		}

	private:
		std::string m_logPrefix;

		std::mutex* m_avgTimeLock;
		float& m_avgTime;
		float& m_totalWindowTime;

		uint64_t m_startTime;
	};
}

#endif /* Timer_h */
