
#include "JniUtils.h"
#include "../ConnectionHandler.h"

#include <fcntl.h>
#include <mutex>

#define SAFE_ASSIGN_JNI(re, call) {\
	re = call;\
	if (jenv->ExceptionOccurred())\
	{\
		re = 0;\
		jenv->ExceptionDescribe();\
		jenv->ExceptionClear();\
	}\
}

#define SAFE_ASSIGN_JNI_IF_NULL(re, call) if (!re) SAFE_ASSIGN_JNI(re, call)

namespace HQRemote {
	void SocketServerHandler::platformGetLocalAddressesForMulticast(std::vector<struct in_addr>& addresses_list_out) {
		addresses_list_out.clear();

		try
		{
			auto jenv = getCurrenThreadJEnv();

			if (jenv)
			{
				JClassRef NetworkInterface, Enumeration, InetAddress;

				SAFE_ASSIGN_JNI(NetworkInterface, jenv->FindClass("java/net/NetworkInterface"));
				SAFE_ASSIGN_JNI(Enumeration, jenv->FindClass("java/util/Enumeration"));
				SAFE_ASSIGN_JNI(InetAddress, jenv->FindClass("java/net/InetAddress"));

				if (!NetworkInterface || !Enumeration || !InetAddress)
					return;

				static std::mutex g_globalLock;

				//NetworkInterface's
				static JMethodID getNetworkInterfaces, getInetAddresses; 
				//InetAddress's
				static JMethodID isLoopbackAddress, isAnyLocalAddress, isMulticastAddress, getHostAddress, getAddress;
				//Enumeration's
				static JMethodID hasMoreElements, nextElement;

				{
					std::lock_guard<std::mutex> lg(g_globalLock);

					SAFE_ASSIGN_JNI_IF_NULL(getNetworkInterfaces, jenv->GetStaticMethodID(NetworkInterface, "getNetworkInterfaces", "()Ljava/util/Enumeration;"));
					SAFE_ASSIGN_JNI_IF_NULL(getInetAddresses, jenv->GetMethodID(NetworkInterface, "getInetAddresses", "()Ljava/util/Enumeration;"));

					SAFE_ASSIGN_JNI_IF_NULL(isLoopbackAddress, jenv->GetMethodID(InetAddress, "isLoopbackAddress", "()Z"));
					SAFE_ASSIGN_JNI_IF_NULL(isAnyLocalAddress, jenv->GetMethodID(InetAddress, "isAnyLocalAddress", "()Z"));
					SAFE_ASSIGN_JNI_IF_NULL(isMulticastAddress, jenv->GetMethodID(InetAddress, "isMulticastAddress", "()Z"));
					SAFE_ASSIGN_JNI_IF_NULL(getHostAddress, jenv->GetMethodID(InetAddress, "getHostAddress", "()Ljava/lang/String;"));
					SAFE_ASSIGN_JNI_IF_NULL(getAddress, jenv->GetMethodID(InetAddress, "getAddress", "()[B"));

					SAFE_ASSIGN_JNI_IF_NULL(hasMoreElements, jenv->GetMethodID(Enumeration, "hasMoreElements", "()Z"));
					SAFE_ASSIGN_JNI_IF_NULL(nextElement, jenv->GetMethodID(Enumeration, "nextElement", "()Ljava/lang/Object;"));
				}

				JObjectRef interfaces;
				SAFE_ASSIGN_JNI(interfaces, jenv->CallStaticObjectMethod(NetworkInterface, getNetworkInterfaces));
				if (!interfaces)
					return;

				while (jenv->CallBooleanMethod(interfaces, hasMoreElements))
				{
					JObjectRef _interface = jenv->CallObjectMethod(interfaces, nextElement);

					JObjectRef addresses;
					SAFE_ASSIGN_JNI(addresses, jenv->CallObjectMethod(_interface, getInetAddresses));

					if (addresses)
					{
						while (jenv->CallBooleanMethod(addresses, hasMoreElements))
						{
							JObjectRef address = jenv->CallObjectMethod(addresses, nextElement);

							if (jenv->CallBooleanMethod(address, isAnyLocalAddress) == JNI_FALSE &&
								jenv->CallBooleanMethod(address, isLoopbackAddress) == JNI_FALSE &&
								jenv->CallBooleanMethod(address, isMulticastAddress) == JNI_FALSE)
							{
								JByteArrayRef jaddressBytes;
								SAFE_ASSIGN_JNI(jaddressBytes, (jbyteArray)jenv->CallObjectMethod(address, getAddress));
								if (jaddressBytes)
								{
									auto length = jenv->GetArrayLength(jaddressBytes.get());
									if (length == 4)
									{
										//this is ipv4. Get its address in string form
										JStringRef jaddr_str;
										SAFE_ASSIGN_JNI(jaddr_str, (jstring)jenv->CallObjectMethod(address, getHostAddress));

										const char* addr_str;
										if (jaddr_str && (addr_str = jenv->GetStringUTFChars(jaddr_str, NULL)) != NULL)
										{
											struct in_addr addrToInsert;
											//convert to in_addr
											addrToInsert.s_addr = inet_addr(addr_str);

											//release jni resource
											jenv->ReleaseStringUTFChars(jaddr_str, addr_str);

											//insert to output list
											addresses_list_out.push_back(addrToInsert);
										}
									}//if (length == 4)
								}//if (addressBytes)
							}
						}//while (has more addresses)
					}//if (addresses)
				}//while (jenv->CallBooleanMethod(interfaces, hasMoreElements))
			}//if (jenv)
		}
		catch (...) {
			addresses_list_out.clear();

			//default address
			struct in_addr _default;
			_default.s_addr = htonl(INADDR_ANY);
			addresses_list_out.push_back(_default);
		}
	}
}