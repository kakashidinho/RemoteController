#include "../ConnectionHandler.h"

#include <fcntl.h>

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
		//default address
		addresses.clear();

		struct in_addr default;
		default.s_addr = htonl(INADDR_ANY);
		addresses.push_back(default);
	}
}