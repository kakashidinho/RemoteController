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

#include "../ConnectionHandler.h"

#include <fcntl.h>
#if !defined __ANDROID__
#	include <ifaddrs.h>
#endif

namespace HQRemote {
	int SocketConnectionHandler::platformSetSocketBlockingMode(socket_t socket, bool blocking)
	{
		auto arg = fcntl(socket, F_GETFL, NULL);
		if (arg < 0)
			return arg;

		if (blocking)
			arg &= ~O_NONBLOCK;
		else
			arg |= O_NONBLOCK;
		return fcntl(socket, F_SETFL, arg);
	}

	int SocketConnectionHandler::platformGetLastSocketErr() {
		return errno;
	}

	in_addr SocketConnectionHandler::platformIpv4StringToAddr(const char* addr_str) {
		in_addr re;
		re.s_addr = inet_addr(addr_str);
		return re;
	}

	const char* SocketConnectionHandler::platformIpv4AddrToString(const in_addr* addr, char* addr_buf, size_t addr_buf_max_len) {
		return inet_ntop(AF_INET, (const void*)addr, addr_buf, addr_buf_max_len);
	}

	__attribute__((weak))
	void SocketServerHandler::platformGetLocalAddressesForMulticast(std::vector<struct in_addr>& addresses) {
		addresses.clear();
#if !defined __ANDROID__
		struct ifaddrs *ifap_buf;
		if (getifaddrs(&ifap_buf) == 0)
		{
			auto ifap = ifap_buf;
			while (ifap)
			{
				if (ifap->ifa_addr->sa_family == AF_INET)
				{
					auto& addr = *(struct sockaddr_in*)ifap->ifa_addr;
					addresses.push_back(addr.sin_addr);
				}
				ifap = ifap->ifa_next;//next interface
			}
			
			freeifaddrs(ifap_buf);
		}
		else
#endif
		{
			//default address

			struct in_addr _default;
			_default.s_addr = htonl(INADDR_ANY);
			addresses.push_back(_default);
		}
	}
}