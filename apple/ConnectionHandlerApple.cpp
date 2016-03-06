//
//  ConnectionHandlerApple.cpp
//  RemoteController
//
//  Created by Le Hoang Quyen on 22/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#include "../ConnectionHandler.h"

#include <fcntl.h>

namespace HQRemote {
	struct SocketConnectionHandler::Impl {
	};
	
	void SocketConnectionHandler::platformConstruct() {
		m_impl = new Impl();
	}
	void SocketConnectionHandler::platformDestruct() {
		delete m_impl;
	}

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

	int SocketConnectionHandler::platformGetLastSocketErr() const {
		return errno;
	}
}
