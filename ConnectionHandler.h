#ifndef HQREMOTE_CONNECTION_HANDLER_H
#define HQREMOTE_CONNECTION_HANDLER_H

#ifdef WIN32
#	include <Winsock2.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#endif

#include "Common.h"
#include "CString.h"
#include "Data.h"
#include "Event.h"
#include "Timer.h"

#include <string>
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
	typedef SOCKET socket_t;
#else
	typedef int socket_t;
#endif

#if defined WIN32 || defined _MSC_VER
#	pragma warning(push)
#	pragma warning(disable:4251)
#endif

	struct HQREMOTE_API ConnectionEndpoint {
		ConnectionEndpoint(const char* addr, int _port)
		: address(addr), port(_port)
		{}
		
		CString address;
		int port;
	};
	
	//interface
	class HQREMOTE_API IConnectionHandler {
	public:
		virtual ~IConnectionHandler();

		bool start();
		void stop();
		bool running() const;

		double timeSinceStart() const;
		virtual bool connected() const = 0;

		virtual DataRef receiveData() = 0;
		virtual void sendData(ConstDataRef data);
		virtual void sendDataUnreliable(ConstDataRef data);
		virtual void sendData(const void* data, size_t size) = 0;
		virtual void sendDataUnreliable(const void* data, size_t size) = 0;
		virtual float getReceiveRate() const = 0;
	protected:
		IConnectionHandler();
		
		virtual bool startImpl() = 0;
		virtual void stopImpl() = 0;
		
		std::atomic<bool> m_running;
		time_checkpoint_t m_startTime;
	};

	//socket based handler
	class HQREMOTE_API SocketConnectionHandler : public IConnectionHandler {
	public:
		~SocketConnectionHandler();

		virtual bool connected() const override;

		virtual DataRef receiveData() override;
		virtual void sendData(const void* data, size_t size) override;
		virtual void sendDataUnreliable(const void* data, size_t size) override;

		virtual float getReceiveRate() const override {
			return m_recvRate;
		}
	private:
		struct MsgChunk;

		void platformConstruct();
		void platformDestruct();

		int platformGetLastSocketErr() const;
		
		virtual bool startImpl() override;
		virtual void stopImpl() override;

		void sendDataNoLock(const void* data, size_t size);
		void sendDataUnreliableNoLock(const void* data, size_t size);
		
		_ssize_t pingUnreliableNoLock(time_checkpoint_t sendTime);
		
		void recvProc();
		
	protected:
		SocketConnectionHandler();
		
		struct MsgBuf {
			DataRef data;
			size_t filledSize;
		};
		
		struct UnreliablePingInfo {
			uint64_t sendTime;
			double rtt;
		};

		virtual bool socketInitImpl() = 0;
		virtual void initConnectionImpl() = 0;
		virtual void addtionalRcvThreadCleanupImpl() = 0;
		virtual void addtionalSocketCleanupImpl() = 0;
		
		
		_ssize_t sendDataNoLock(socket_t socket, const sockaddr_in* pDstAddr, const void* data, size_t size);
		
		_ssize_t sendChunkUnreliableNoLock(socket_t socket, const sockaddr_in* pDstAddr, const MsgChunk& chunk);//connectionless only socket
		_ssize_t recvChunkUnreliableNoLock(socket_t socket, MsgChunk& chunk);//connectionless only socket
		_ssize_t recvDataUnreliableNoLock(socket_t socket);
		_ssize_t sendDataAtomicNoLock(socket_t socket, const void* data, size_t expectedSize);//connection oriented only socket
		_ssize_t recvDataAtomicNoLock(socket_t socket, void* data, size_t expectedSize);
		_ssize_t recvDataNoLock(socket_t socket);
		
		//check if we're able to connect to the remote endpoint on an unreliable channel
		bool testUnreliableRemoteEndpointNoLock();
		
		void pushDataToQueue(DataRef data, bool discardIfFull);

		std::mutex m_socketLock;
		//receiving thread
		std::map<uint64_t, MsgBuf> m_connLessBuffers;
		std::list<DataRef> m_dataQueue;
		std::mutex m_dataLock;
		std::unique_ptr<std::thread> m_recvThread;

		std::atomic<socket_t> m_connSocket;
		std::atomic<socket_t> m_connLessSocket;//connection less socket
		std::unique_ptr<sockaddr_in> m_connLessSocketDestAddr;//destination endpoint of connectionless socket
		
		UnreliablePingInfo m_lastConnLessPing;

		time_checkpoint_t m_lastRecvTime;
		size_t m_numLastestDataReceived;
		std::atomic<float> m_recvRate;

		bool m_enableReconnect;
		
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
		virtual bool socketInitImpl() override;
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;

		int m_connLessPort;
	};

	//socket based server handler
	class HQREMOTE_API SocketServerHandler : public BaseUnreliableSocketHandler {
	public:
		//pass <connLessListeningPort> = 0 if you don't want to use unreliable socket
		SocketServerHandler(int listeningPort, int connLessListeningPort);
		~SocketServerHandler();

		virtual bool connected() const override;
	private:
		virtual bool socketInitImpl() override;
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;

		int m_port;
		std::atomic<socket_t> m_serverSocket;
	};
	
	//socket based ureliable client handler
	class HQREMOTE_API UnreliableSocketClientHandler : public BaseUnreliableSocketHandler {
	public:
		UnreliableSocketClientHandler(int connLessListeningPort, const ConnectionEndpoint& connLessRemoteEndpoint);
		~UnreliableSocketClientHandler();
		
		virtual bool connected() const override;
	protected:
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;
		
		ConnectionEndpoint m_connLessRemoteEndpoint;
		
		std::atomic<bool> m_connLessRemoteReachable;
	};
	
	//socket based client handler
	class HQREMOTE_API SocketClientHandler : public UnreliableSocketClientHandler {
	public:
		//pass <remoteConnLessEndpoint.port> = 0 if you don't want to use unreliable socket
		SocketClientHandler(int listeningPort, int connLessListeningPort, const ConnectionEndpoint& remoteEndpoint, const ConnectionEndpoint& remoteConnLessEndpoint);
		~SocketClientHandler();
		
		virtual bool connected() const override;
	private:
		virtual void initConnectionImpl() override;
		virtual void addtionalRcvThreadCleanupImpl() override;
		virtual void addtionalSocketCleanupImpl() override;
		
		ConnectionEndpoint m_remoteEndpoint;//reliable endpoint
	};

#if defined WIN32 || defined _MSC_VER
#	pragma warning(pop)
#endif
}

#endif