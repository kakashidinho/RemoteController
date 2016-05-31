#include "../ConnectionHandler.h"

namespace HQRemote {
	struct SocketConnectionHandler::Impl {
		WSADATA wsaData;
	};

	void SocketConnectionHandler::platformConstruct() {
		m_impl = new Impl();

		auto sockVer = MAKEWORD(2, 2);

		WSAStartup(sockVer, &m_impl->wsaData);
		//TODO: error checking
	}
	void SocketConnectionHandler::platformDestruct() {

		WSACleanup();
		delete m_impl;
	}

	int SocketConnectionHandler::platformSetSocketBlockingMode(socket_t socket, bool blocking)
	{
		u_long noBlock = blocking ? 0 : 1;
		return ioctlsocket(socket, FIONBIO, &noBlock);
	}

	int SocketConnectionHandler::platformGetLastSocketErr() {
		return WSAGetLastError();
	}
}