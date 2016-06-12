#include "../ConnectionHandler.h"

#include <fcntl.h>
#include <ifaddrs.h>

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

	void SocketServerHandler::platformGetLocalAddressesForMulticast(std::vector<struct in_addr>& addresses) {
		addresses.clear();
#if 1
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