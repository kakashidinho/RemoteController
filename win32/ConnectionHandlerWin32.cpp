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
#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP
#	include <Ws2tcpip.h>
#	if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
#		include <Iphlpapi.h>
#	endif
#endif

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

	in_addr SocketConnectionHandler::platformIpv4StringToAddr(const char* addr_str) {
		in_addr re;
#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP
		re.s_addr = INADDR_ANY;

		InetPtonA(AF_INET, addr_str, &re);
#else//#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP
		re.s_addr = inet_addr(addr_str);
#endif//#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP
		return re;
	}

	const char* SocketConnectionHandler::platformIpv4AddrToString(const in_addr* addr, char* addr_buf, size_t addr_buf_max_len) {
#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP || WINAPI_FAMILY == WINAPI_FAMILY_APP
		return inet_ntop(AF_INET, const_cast<in_addr*>(addr), addr_buf, addr_buf_max_len);
#else//#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP

		//WinRT doesn't have inet_ntop
		auto addr_str = inet_ntoa(*addr);
		auto addr_str_len = strlen(addr_str);
		if (addr_str_len > addr_buf_max_len - 1)
			return NULL;

		memcpy(addr_buf, addr_str, addr_str_len + 1);

		return addr_buf;
#endif//#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
	}

	/*--------- SocketServerHandler -------------*/
	void SocketServerHandler::platformGetLocalAddressesForMulticast(std::vector<struct in_addr>& addresses) {
		addresses.clear();
#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
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
		catch (...) 
#endif// #if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
		{
			addresses.clear();

			//use default address
			struct in_addr default;
			default.s_addr = INADDR_ANY;
			addresses.push_back(default);
		}
#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
		//cleanup
		free(addressesBuf);
#endif//#if WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
	}
}