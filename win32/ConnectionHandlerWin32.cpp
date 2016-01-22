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

	int SocketConnectionHandler::platformGetLastSocketErr() const {
		return WSAGetLastError();
	}
}