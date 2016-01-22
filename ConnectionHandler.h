#ifndef HQREMOTE_CONNECTION_HANDLER_H
#define HQREMOTE_CONNECTION_HANDLER_H

#include "Common.h"
#include "Data.h"
#include "Event.h"

#ifdef WIN32
#	include <Winsock2.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#endif

#include <map>
#include <list>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <condition_variable>

namespace HQRemote {
#ifdef WIN32
	typedef SSIZE_T _ssize_t;
	typedef SOCKET socket_t;
#else
	typedef ssize_t _ssize_t;
	typedef int socket_t;
#endif

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

	//interface
	class HQREMOTE_API IConnectionHandler {
	public:
		virtual ~IConnectionHandler() {}

		virtual void start() = 0;
		virtual void stop() = 0;

		virtual bool connected() const = 0;

		virtual DataRef receiveData() = 0;
		virtual void sendData(ConstDataRef data);
		virtual void sendDataUnreliable(ConstDataRef data);
		virtual void sendData(const void* data, size_t size) = 0;
		virtual void sendDataUnreliable(const void* data, size_t size) = 0;
	};

	//socket based handler
	class HQREMOTE_API SocketConnectionHandler : public IConnectionHandler {
	public:
		SocketConnectionHandler();
		~SocketConnectionHandler();

		virtual void start() override;
		virtual void stop() override;

		virtual bool connected() const override;

		virtual DataRef receiveData() override;
		virtual void sendData(const void* data, size_t size) override;
		virtual void sendDataUnreliable(const void* data, size_t size) override;
	private:
		struct MsgChunk;

		void platformConstruct();
		void platformDestruct();

		int platformGetLastSocketErr() const;

		void sendDataNoLock(const void* data, size_t size);
		void sendDataUnreliableNoLock(const void* data, size_t size);

		_ssize_t sendDataNoLock(socket_t socket, const sockaddr_in* pDstAddr, const void* data, size_t size);

		_ssize_t sendChunkUnreliableNoLock(socket_t socket, const sockaddr_in* pDstAddr, const MsgChunk& chunk);//connectionless only socket
		_ssize_t recvChunkUnreliableNoLock(socket_t socket, MsgChunk& chunk);//connectionless only socket
		_ssize_t recvDataUnreliableNoLock(socket_t socket);
		_ssize_t sendDataAtomicNoLock(socket_t socket, const void* data, size_t expectedSize);//connection oriented only socket
		_ssize_t recvDataAtomicNoLock(socket_t socket, void* data, size_t expectedSize);
		_ssize_t recvDataNoLock(socket_t socket);

		void recvProc();

	protected:
		struct MsgBuf {
			DataRef data;
			size_t filledSize;
		};

		virtual void initConnectionImpl() = 0;
		virtual void addtionalRcvThreadCleanupImpl() = 0;
		virtual void addtionalSocketCleanupImpl() = 0;

		void pushDataToQueue(DataRef data);

		std::mutex m_socketLock;
		//receiving thread
		std::map<uint64_t, MsgBuf> m_connLessBuffers;
		std::list<DataRef> m_dataQueue;
		std::mutex m_dataLock;
		std::unique_ptr<std::thread> m_recvThread;

		std::atomic<bool> m_running;

		std::atomic<socket_t> m_connSocket;
		std::atomic<socket_t> m_connLessSocket;//connection less socket
		std::unique_ptr<sockaddr_in> m_connLessSocketAddr;

		//platform dependent
		struct Impl;
		Impl * m_impl;
	};


	//connection-less socket based handler
	class HQREMOTE_API BaseUnreliableSocketHandler : public SocketConnectionHandler {
	public:
		BaseUnreliableSocketHandler(int connLessListeningPort);
		~BaseUnreliableSocketHandler();

	protected:
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;

		int m_connLessPort;
	};

	//socket based server handler
	class HQREMOTE_API SocketServerHandler : public BaseUnreliableSocketHandler {
	public:
		SocketServerHandler(int listeningPort, int connLessListeningPort);
		~SocketServerHandler();

	private:
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;

		int m_port;
		socket_t m_serverSocket;
	};

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif
}

#endif