//
//  Timer.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 12/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#ifndef Remote_Timer_h
#define Remote_Timer_h

#include <string>

#if defined WIN32/*---windows---*/

#	include <windows.h>

#elif defined __APPLE__/*----apple------*/

#	include <mach/mach_time.h>
#	include <time.h>
#else

#	error need implementation

#endif//#if defined WIN32

namespace HQRemote {
#if defined WIN32/*---windows---*/
	
	typedef LARGE_INTEGER time_checkpoint_t;

	class TimeCompare {
	public:
		bool operator() (const time_checkpoint_t& t1, const time_checkpoint_t& t2) const {
			return t1.QuadPart < t2.QuadPart;
		}
	};
	
#elif defined __APPLE__/*----apple------*/
	
	typedef uint64_t time_checkpoint_t;

	class TimeCompare {
	public:
		bool operator() (const time_checkpoint_t& t1, const time_checkpoint_t& t2) const {
			return t1 < t2;
}
	};
#else
	
#	error need implementation
	
#endif//#if defined WIN32
	
	///get time check point
	void getTimeCheckPoint(time_checkpoint_t& checkPoint);
	///get elapsed time in seconds between two check points
	double getElapsedTime(const time_checkpoint_t& point1 , const time_checkpoint_t& point2);
	
	std::string getCurrentTimeStr();

	//generate increasing id based on time
	uint64_t generateIDFromTime(const time_checkpoint_t& time);
	inline uint64_t generateIDFromTime() {
		time_checkpoint_t time;
		getTimeCheckPoint(time);
		return generateIDFromTime(time);
	}
}

#endif /* Timer_h */
