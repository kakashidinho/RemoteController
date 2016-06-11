#include "../ConnectionHandler.h"

#include <Iphlpapi.h>

namespace HQRemote {
	/*------------ SocketConnectionHandler ---------*/
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

	/*--------- SocketServerHandler -------------*/
	void SocketServerHandler::platformGetLocalAddressesForMulticast(std::vector<struct in_addr>& addresses) {
		addresses.clear();

		IP_ADAPTER_ADDRESSES* addressesBuf = NULL;

		try {
			ULONG addressesBufSize = 0;
			if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, NULL, NULL, &addressesBufSize) != ERROR_BUFFER_OVERFLOW)
				throw std::runtime_error("");//go to catch block

			addressesBuf = (IP_ADAPTER_ADDRESSES*)malloc(addressesBufSize);
			if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, NULL, addressesBuf, &addressesBufSize) != ERROR_SUCCESS)
				throw std::runtime_error("");//go to catch block

			auto pAddressInfo = addressesBuf;
			while (pAddressInfo) {
				if ((pAddressInfo->Flags & IP_ADAPTER_NO_MULTICAST) == 0)
				{
					auto pAddress = pAddressInfo->FirstUnicastAddress;
					while (pAddress) {
						if (pAddress->Address.iSockaddrLength == sizeof(struct sockaddr_in))
						{
							auto addr = (struct sockaddr_in*)pAddress->Address.lpSockaddr;
							addresses.push_back(addr->sin_addr);
						}

						pAddress = pAddress->Next;//next address
					}//while (pAddress)
				}

				pAddressInfo = pAddressInfo->Next;//next adapter's address info
			}//while (pAddressInfo)
		}
		catch (...) {
			addresses.clear();

			//use default address
			struct in_addr default;
			default.s_addr = INADDR_ANY;
			addresses.push_back(default);
		}

		//cleanup
		free(addressesBuf);
	}
}