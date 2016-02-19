#include "ConnectionHandler.h"
#include "Timer.h"

#include <assert.h>
#include <fstream>
#include <sstream>
#include <limits>

#define MAX_FRAGMEMT_SIZE (16 * 1024)
#define MAX_PENDING_UNRELIABLE_BUF 20
#define UNRELIABLE_PING_TIMEOUT 3
#define UNRELIABLE_PING_RETRIES 10
#define UNRELIABLE_PING_INTERVAL 10

#define NUM_PENDING_MSGS_TO_START_DISCARD 60

#define RCV_RATE_UPDATE_INTERVAL 1.0

#ifndef min
#	define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifdef WIN32
#	include <windows.h>

#	define _INADDR_ANY INADDR_ANY
#	define MSGSIZE_ERROR WSAEMSGSIZE 

typedef int socklen_t;

#else//#ifdef WIN32

#	define SOCKET_ERROR -1
#	define INVALID_SOCKET -1
#	define SD_BOTH SHUT_RDWR
#	define closesocket close
#	define _INADDR_ANY htonl(INADDR_ANY)
#	define MSGSIZE_ERROR EMSGSIZE 

#endif//#ifdef WIN32

namespace HQRemote {
	enum MsgChunkType :uint32_t {
		MSG_HEADER,
		FRAGMENT_HEADER,
		PING_MSG_CHUNK,
		PING_REPLY_MSG_CHUNK
	};

	union MsgChunkHeader 
	{
		struct {
			MsgChunkType type;
			uint32_t size;//chunk total size (including this header)
			uint64_t id;

			union {
				struct {
					uint32_t msg_size;
				} wholeMsgInfo;

				struct {
					uint32_t offset;
				} fragmentInfo;
				
				struct {
					uint64_t sendTime;
				} pingInfo;
			};
		};

		uint64_t unusedName[3];//this to make sure size of this header is multiple of 64 bit
	};

	struct SocketConnectionHandler::MsgChunk {
		MsgChunk() {
			assert(offsetHeaderToPayload() == 0);
		}

		MsgChunkHeader header;
		unsigned char payload[MAX_FRAGMEMT_SIZE];

	private:
		uint32_t offsetHeaderToPayload() const {
			return (unsigned char*)&payload - (unsigned char*)this - sizeof(header);
		}
	};

	/*--------------- IConnectionHandler -----------*/
	IConnectionHandler::IConnectionHandler()
	: m_running(false)
	{
	}
	
	IConnectionHandler::~IConnectionHandler() {
	}
	
	bool IConnectionHandler::start() {
		stop();
		
		if (!startImpl())
			return false;
		
		m_running = true;
		
		getTimeCheckPoint(m_startTime);

		return true;
	}
	
	void IConnectionHandler::stop() {
		stopImpl();
		m_running = false;
	}
	
	bool IConnectionHandler::running() const {
		return m_running;
	}
	
	double IConnectionHandler::timeSinceStart() const {
		if (!m_running)
			return 0;
		
		time_checkpoint_t curTime;
		getTimeCheckPoint(curTime);
		
		return getElapsedTime(m_startTime, curTime);
	}
	
	void IConnectionHandler::sendData(ConstDataRef data) {
		if (data == nullptr)
			return;
		sendData(data->data(), data->size());
	}

	void IConnectionHandler::sendDataUnreliable(ConstDataRef data) {
		if (data == nullptr)
			return;
		sendDataUnreliable(data->data(), data->size());
	}

	/*----------------SocketConnectionHandler ----------------*/
	SocketConnectionHandler::SocketConnectionHandler()
		:m_connLessSocket(INVALID_SOCKET), m_connSocket(INVALID_SOCKET), m_enableReconnect(true), m_recvRate(0)
	{
		platformConstruct();
	}

	SocketConnectionHandler::~SocketConnectionHandler()
	{
		platformDestruct();
	}


	bool SocketConnectionHandler::startImpl()
	{
		if (!socketInitImpl())
			return false;

		m_running = true;
		
		//invalidate last connectionless ping info
		m_lastConnLessPing.sendTime = 0;
		m_lastConnLessPing.rtt = -1;

		//start background thread to receive remote event
		m_recvThread = std::unique_ptr<std::thread>(new std::thread([this] {
			recvProc();
		}));

		return true;
	}

	void SocketConnectionHandler::stopImpl()
	{
		m_running = false;

		m_socketLock.lock();

		if (m_connSocket != INVALID_SOCKET)
		{
			shutdown(m_connSocket, SD_BOTH);
		}

		if (m_connLessSocket != INVALID_SOCKET)
		{
			shutdown(m_connLessSocket, SD_BOTH);
		}

		addtionalSocketCleanupImpl();

		m_socketLock.unlock();

		//wake any thread blocked when trying to retrieve data
		{
			m_dataLock.lock();
			m_dataCv.notify_all();
			m_dataLock.unlock();
		}

		//join with all threads
		if (m_recvThread != nullptr && m_recvThread->joinable())
		{
			m_recvThread->join();

			m_recvThread = nullptr;
		}
	}

	bool SocketConnectionHandler::connected() const {
		return m_connSocket != INVALID_SOCKET || (m_connLessSocket != INVALID_SOCKET && m_connLessSocketDestAddr != nullptr);
	}

	//obtain data received through background thread
	DataRef SocketConnectionHandler::receiveData()
	{
		DataRef data = nullptr;

		if (m_dataLock.try_lock()){
			if (m_dataQueue.size() > 0)
			{
				data = m_dataQueue.front();
				m_dataQueue.pop_front();
			}
			
			m_dataLock.unlock();
		}

		return data;
	}

	DataRef SocketConnectionHandler::receiveDataBlock() {
		std::unique_lock<std::mutex> lk(m_dataLock);

		m_dataCv.wait(lk, [this] { return !m_running || m_dataQueue.size() > 0; });

		if (m_dataQueue.size())
		{
			auto re = m_dataQueue.front();
			m_dataQueue.pop_front();
			return re;
		}
		return nullptr;
	}

	void SocketConnectionHandler::pushDataToQueue(DataRef data, bool discardIfFull) {
		std::lock_guard<std::mutex> lg(m_dataLock);

		//calculate data rate
		time_checkpoint_t curTime;
		getTimeCheckPoint(curTime);

		m_numLastestDataReceived += data->size();
		
		auto elapsedTime = getElapsedTime(m_lastRecvTime, curTime);
		if (elapsedTime >= RCV_RATE_UPDATE_INTERVAL)
		{
			m_recvRate = 0.8f * m_recvRate + 0.2f * m_numLastestDataReceived / (float)elapsedTime;
			
			m_lastRecvTime = curTime;
			m_numLastestDataReceived = 0;
		}

		//discard data if no more room
		if (discardIfFull && m_dataQueue.size() > NUM_PENDING_MSGS_TO_START_DISCARD)
		{
#if defined DEBUG || defined _DEBUG
			fprintf(stderr, "discarded a message due to too many in queue\n");
#endif
			return;//ignore
		}
		
		m_dataQueue.push_back(data);

		m_dataCv.notify_all();
	}

	void SocketConnectionHandler::sendData(const void* data, size_t size)
	{
		std::lock_guard<std::mutex> lg(m_socketLock);

		if (m_connSocket == INVALID_SOCKET)//fallback to unreliable socket
			sendDataUnreliableNoLock(data, size);
		else
			sendDataNoLock(data, size);
	}

	void SocketConnectionHandler::sendDataNoLock(const void* data, size_t size)
	{
		sendDataNoLock(m_connSocket, NULL, data, size);
	}

	void SocketConnectionHandler::sendDataUnreliable(const void* data, size_t size)
	{
		std::lock_guard<std::mutex> lg(m_socketLock);

		if (m_connLessSocket == INVALID_SOCKET || m_connLessSocketDestAddr == nullptr)//fallback to reliable socket
		{
			if (m_connSocket != INVALID_SOCKET)
				sendDataNoLock(data, size);
		}
		else
			sendDataUnreliableNoLock(data, size);
	}

	void SocketConnectionHandler::sendDataUnreliableNoLock(const void* data, size_t size)
	{
		sendDataNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), data, size);
	}

	_ssize_t SocketConnectionHandler::sendDataNoLock(socket_t socket, const sockaddr_in* pDstAddr, const void* data, size_t size) {
		_ssize_t re = 0;
		assert(size <= 0xffffffff);
			
		//TODO: assume all sides use the same byte order for now
		MsgChunkHeader header;
		header.type = MSG_HEADER;
		header.id = generateIDFromTime();
		header.size = sizeof(header);//chunk size
		header.wholeMsgInfo.msg_size = (uint32_t)size;//whole message size

		if (pDstAddr == NULL)//TCP
		{
			//send header
			re = sendDataAtomicNoLock(socket, &header, sizeof(header));
			if (re == SOCKET_ERROR)
				return re;

			//send whole data
			re = sendDataAtomicNoLock(socket, data, size);

		}//if (socket == m_connSocket)
		else if (socket == m_connLessSocket) {
			MsgChunk chunk;
			chunk.header = header;

			//send header
			re = sendChunkUnreliableNoLock(socket, pDstAddr, chunk);

			//send data's fragments
			_ssize_t maxFragmentSize = sizeof(chunk.payload);
			//TODO: assume all sides use the same byte order for now
			chunk.header.type = FRAGMENT_HEADER;
			chunk.header.fragmentInfo.offset = 0;

			uint32_t chunkPayloadSize;

			do {
				_ssize_t remainSize = (_ssize_t)size - (_ssize_t)chunk.header.fragmentInfo.offset;
				chunkPayloadSize = min(maxFragmentSize, remainSize);
				chunk.header.size = sizeof(chunk.header) + chunkPayloadSize;

				//fill chunk's data
				memcpy(chunk.payload, (const char*)data + chunk.header.fragmentInfo.offset, chunkPayloadSize);

				//send chunk
				re = sendChunkUnreliableNoLock(socket, pDstAddr, chunk);
						
				if (re == SOCKET_ERROR)
				{
					//exceed max message size, try to reduce it
					if (platformGetLastSocketErr() == MSGSIZE_ERROR && maxFragmentSize > 1) {
						maxFragmentSize >>= 1;

						re = 0;//restart in next iteration
					}
				}
				else {
					chunk.header.fragmentInfo.offset += chunkPayloadSize;//next fragment
				}
			} while (re != SOCKET_ERROR && chunk.header.fragmentInfo.offset < size);
		}//else if (socket == m_connLessSocket)
		return re;
	}

	_ssize_t SocketConnectionHandler::sendChunkUnreliableNoLock(socket_t socket, const sockaddr_in* pDstAddr, const MsgChunk& chunk) {
		if (pDstAddr == NULL)//we must have info of remote size's address
			return 0;
		return sendto(socket, (char*)&chunk, chunk.header.size, 0, (const sockaddr*)pDstAddr, sizeof(sockaddr_in));
	}

	_ssize_t SocketConnectionHandler::recvChunkUnreliableNoLock(socket_t socket, MsgChunk& chunk) {
		if (m_connLessSocketDestAddr != nullptr)
		{
			return recvfrom(socket, (char*)&chunk, sizeof(chunk), 0, NULL, NULL);
		}
		else {
			//obtain remote size's address
			socklen_t len = sizeof(sockaddr_in);

			m_connLessSocketDestAddr = std::unique_ptr<sockaddr_in>(new sockaddr_in());

			auto re = recvfrom(socket, (char*)&chunk, sizeof(chunk), 0, (sockaddr*)m_connLessSocketDestAddr.get(), &len);

			return re;
		}
	}

	_ssize_t SocketConnectionHandler::sendDataAtomicNoLock(socket_t socket, const void* data, size_t size) {
		_ssize_t re;

		size_t offset = 0;
		do {
			re = send(socket, (const char*)data + offset, size - offset, 0);
			offset += re;
		} while (re != SOCKET_ERROR && re != 0 && offset < size);

		return (re == SOCKET_ERROR || re == 0 ) ? SOCKET_ERROR : size;
	}

	_ssize_t SocketConnectionHandler::recvDataAtomicNoLock(socket_t socket, void* data, size_t expectedSize) {
		_ssize_t re;
		size_t offset = 0;
		char* ptr = (char*)data;
		do {
			re = recv(socket, ptr + offset, expectedSize - offset, 0);
			offset += re;
		} while (re != SOCKET_ERROR && re != 0 && offset < expectedSize);

		if (re == SOCKET_ERROR || re == 0)
			return SOCKET_ERROR;

		return expectedSize;
	}

	_ssize_t SocketConnectionHandler::recvDataNoLock(socket_t socket) {
		MsgChunkHeader header;
		_ssize_t re;

		re = recvDataAtomicNoLock(socket, &header, sizeof(header));
		if (re == SOCKET_ERROR)
			return re;

		switch (header.type) {
		case MSG_HEADER:
		{
			auto data = std::make_shared<CData>(header.wholeMsgInfo.msg_size);
			//read data immediately
			re = recvDataAtomicNoLock(socket, data->data(), data->size());
			if (re != SOCKET_ERROR)
				pushDataToQueue(data, false);
		}
			break;
		}//switch (header.type)

		return re;
	}

	_ssize_t SocketConnectionHandler::recvDataUnreliableNoLock(socket_t socket) {
		MsgChunk chunk;
		_ssize_t re;

		//read chunk data
		re = recvChunkUnreliableNoLock(socket, chunk);
		if (re == SOCKET_ERROR)
			return re;

		switch (chunk.header.type) {
		case MSG_HEADER:
		{
			auto data = std::make_shared<CData>(chunk.header.wholeMsgInfo.msg_size);
			//initialize a placeholder for upcoming message
			if (m_connLessBuffers.size() == MAX_PENDING_UNRELIABLE_BUF)//discard oldest pending message
			{
				auto first = m_connLessBuffers.begin();
				m_connLessBuffers.erase(first);
				
#if defined DEBUG || defined _DEBUG
				fprintf(stderr, "discarded a message\n");
#endif
			}

			MsgBuf newBuf;
			newBuf.data = data;
			newBuf.filledSize = 0;

			m_connLessBuffers.insert(std::pair<uint64_t, MsgBuf>(chunk.header.id, newBuf));
		}
			break;
		case FRAGMENT_HEADER:
		{
			//fill the pending message's buffer
			auto pendingBufIte = m_connLessBuffers.find(chunk.header.id);
			if (pendingBufIte != m_connLessBuffers.end()) {
				auto& buffer = pendingBufIte->second;

				auto payloadSize = chunk.header.size - sizeof(chunk.header);

				memcpy(buffer.data->data() + chunk.header.fragmentInfo.offset, chunk.payload, payloadSize);
				buffer.filledSize += payloadSize;

				//message is complete, push to data queue for comsuming
				if (buffer.filledSize >= buffer.data->size()) {
					pushDataToQueue(buffer.data, true);
					
					//remove from pending list
					m_connLessBuffers.erase(pendingBufIte);
				}
			}
#if defined DEBUG || defined _DEBUG
			else {
				fprintf(stderr, "discarded a fragment\n");
			}
#endif
		}
			break;
		case PING_MSG_CHUNK:
		{
			//reply
			chunk.header.type = PING_REPLY_MSG_CHUNK;
			sendChunkUnreliableNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), chunk);
		}
			break;
		case PING_REPLY_MSG_CHUNK:
		{
			//the time that ping message sent
			auto pingSendTime64 = chunk.header.pingInfo.sendTime;
			if (m_lastConnLessPing.sendTime <= pingSendTime64)
			{
				time_checkpoint_t curTime;
				time_checkpoint_t pingSendTime;
				
				convertToTimeCheckPoint(pingSendTime, pingSendTime64);
				getTimeCheckPoint(curTime);
				
				//update latest ping info
				m_lastConnLessPing.sendTime = pingSendTime64;
				m_lastConnLessPing.rtt = getElapsedTime(pingSendTime, curTime);
			}
		}
			break;
		}//switch (chunk.header.type)

		return re;
	}
	
	_ssize_t SocketConnectionHandler::pingUnreliableNoLock(time_checkpoint_t sendTime) {
		//ping remote host
		MsgChunk pingChunk;
		pingChunk.header.type = PING_MSG_CHUNK;
		pingChunk.header.size = sizeof(pingChunk.header);
		pingChunk.header.id = generateIDFromTime(sendTime);
		
		pingChunk.header.pingInfo.sendTime = convertToTimeCheckPoint64(sendTime);
		
		return sendChunkUnreliableNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), pingChunk);
	}
	
	bool SocketConnectionHandler::testUnreliableRemoteEndpointNoLock() {
		bool re = false;
		if (m_connLessSocket != INVALID_SOCKET && m_connLessSocketDestAddr != nullptr)
		{
			const double ping_timeout = UNRELIABLE_PING_TIMEOUT;
			const size_t retries = UNRELIABLE_PING_RETRIES;
			//ping remote host
			time_checkpoint_t time1, time2;
			
			for (size_t i = 0; m_running && !re && i < retries; ++i)
			{
				//invalidate last ping info
				m_lastConnLessPing.sendTime = 0;
				
				getTimeCheckPoint(time1);
				if (pingUnreliableNoLock(time1) == SOCKET_ERROR) {
					continue;
				}
			
				do {
					//check if the reply has arrived
					timeval poll_timeout;
					poll_timeout.tv_sec = 0;
					poll_timeout.tv_usec = 1000;
					
					fd_set sset;
					
					//read data sent via unreliable socket
					FD_ZERO(&sset);
					FD_SET(m_connLessSocket, &sset);
						
					if (select(m_connLessSocket + 1, &sset, NULL, NULL, &poll_timeout) == 1
						&& FD_ISSET(m_connLessSocket, &sset))
						recvDataUnreliableNoLock(m_connLessSocket);
					
					if (m_lastConnLessPing.sendTime == convertToTimeCheckPoint64(time1))
					{
						re = true;
					}
					
					getTimeCheckPoint(time2);
				} while (m_running && !re && getElapsedTime(time1, time2) < ping_timeout);
			}//for (size_t i = 0; i < retries; ++i)
			
		}//if (m_connLessSocketDestAddr != nullptr)
		
		return re;
	}

	void SocketConnectionHandler::recvProc()
	{
		//TODO: separate thread for tcp and udp
		SetCurrentThreadName("remoteDataReceiverThread");

		_ssize_t re;
		bool connectedBefore = false;
		while (m_running) {
			m_socketLock.lock();
			auto l_connected = connected();
			socket_t l_connSocket = m_connSocket;
			socket_t l_connLessSocket = m_connLessSocket;
			m_socketLock.unlock();
			//we have an existing connection
			if (l_connected) {
				if (!connectedBefore)
				{
					getTimeCheckPoint(m_lastRecvTime);
					m_numLastestDataReceived = 0;
					m_recvRate = 0;

					connectedBefore = true;
				}

				//wat for at most 1ms
				timeval timeout;
				timeout.tv_sec = 0;
				timeout.tv_usec = 1000;

				fd_set sset;

				//read data sent via unreliable socket
				if (l_connLessSocket != INVALID_SOCKET) {
					FD_ZERO(&sset);
					FD_SET(l_connLessSocket, &sset);

					if (select(l_connLessSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connLessSocket, &sset))
					{
						re = recvDataUnreliableNoLock(l_connLessSocket);

						if (re == SOCKET_ERROR) {
							std::lock_guard<std::mutex> lg(m_socketLock);
							//invalidate remote end point
							m_connLessSocketDestAddr = nullptr;
						}
					}//if (select(l_connLessSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
				}//if (l_connLessSocket != INVALID_SOCKET)

				//read data sent via reliable socket
				if (l_connSocket != INVALID_SOCKET) {
					FD_ZERO(&sset);
					FD_SET(l_connSocket, &sset);

					if (select(l_connSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
					{
						re = recvDataNoLock(l_connSocket);

						if (re == SOCKET_ERROR) {
							std::lock_guard<std::mutex> lg(m_socketLock);
							//close socket
							closesocket(m_connSocket);
							m_connSocket = INVALID_SOCKET;
						}
					}//if (select(l_connSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
				} //if (l_connSocket != INVALID_SOCKET)

			}//if (l_connected)
			else if (connectedBefore && !m_enableReconnect) {
				//stop
				m_running = false;
			}
			else {
				//initialize connection
				m_connLessSocketDestAddr = nullptr;
				
				initConnectionImpl();
			}//else of if (l_connSocket != INVALID_SOCKET)
		}//while (m_running)

		m_socketLock.lock();
		if (m_connSocket != INVALID_SOCKET)
		{
			closesocket(m_connSocket);
			m_connSocket = INVALID_SOCKET;
		}

		if (m_connLessSocket != INVALID_SOCKET)
		{
			closesocket(m_connLessSocket);
			m_connLessSocket = INVALID_SOCKET;
		}

		m_socketLock.unlock();

		addtionalRcvThreadCleanupImpl();
	}


	/*---------------- BaseUnreliableSocketHandler -------------------*/
	BaseUnreliableSocketHandler::BaseUnreliableSocketHandler(int connLessListeningPort)
		: SocketConnectionHandler(), m_connLessPort(connLessListeningPort)
	{
	}

	BaseUnreliableSocketHandler::~BaseUnreliableSocketHandler() {

	}

	bool BaseUnreliableSocketHandler::socketInitImpl() {
		_ssize_t re;

		sockaddr_in sa;
		memset(&sa, 0, sizeof sa);

		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = _INADDR_ANY;

		std::lock_guard<std::mutex> lg(m_socketLock);
		//create connection less socket
		if (m_connLessSocket == INVALID_SOCKET && m_connLessPort != 0) {
			m_connLessSocketDestAddr = nullptr;

			m_connLessSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (m_connLessSocket != INVALID_SOCKET) {
				sa.sin_port = htons(m_connLessPort);

				re = ::bind(m_connLessSocket, (sockaddr*)&sa, sizeof(sa));
				if (re == SOCKET_ERROR) {
					//failed
					closesocket(m_connLessSocket);
					m_connLessSocket = INVALID_SOCKET;
				}
			}//if (m_connLessSocket != INVALID_SOCKET)
		}//if (m_connLessSocket == INVALID_SOCKET && m_connLessPort != 0)

		return m_connLessSocket != INVALID_SOCKET;
	}

	void BaseUnreliableSocketHandler::initConnectionImpl() {
		
	}

	void BaseUnreliableSocketHandler::addtionalRcvThreadCleanupImpl() {
	}

	void BaseUnreliableSocketHandler::addtionalSocketCleanupImpl() {
	}

	/*-------------  SocketServerHandler  ---------------------------*/
	SocketServerHandler::SocketServerHandler(int listeningPort, int connLessListeningPort)
	: BaseUnreliableSocketHandler(connLessListeningPort),  m_serverSocket(INVALID_SOCKET), m_port(listeningPort)
	{
	}

	SocketServerHandler::~SocketServerHandler() {

	}

	bool SocketServerHandler::connected() const {
		return (m_connLessPort == 0 || m_connLessSocket != INVALID_SOCKET)
				&& m_connSocket != INVALID_SOCKET;
	}
	
	bool SocketServerHandler::socketInitImpl() {
		//create connection less socket
		if (!BaseUnreliableSocketHandler::socketInitImpl())
			return false;

		_ssize_t re;

		sockaddr_in sa;
		memset(&sa, 0, sizeof sa);

		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = _INADDR_ANY;

		//create server socket
		if (m_serverSocket == INVALID_SOCKET && m_port != 0) {
			std::lock_guard<std::mutex> lg(m_socketLock);

			m_serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (m_serverSocket != INVALID_SOCKET) {
				sa.sin_port = htons(m_port);

				re = ::bind(m_serverSocket, (sockaddr*)&sa, sizeof(sa));
				if (re == SOCKET_ERROR) {
					//failed
					closesocket(m_serverSocket);
					m_serverSocket = INVALID_SOCKET;
				}
				else {
					re = listen(m_serverSocket, 1);

					if (re == SOCKET_ERROR) {
						//failed
						closesocket(m_serverSocket);
						m_serverSocket = INVALID_SOCKET;
					}
				}
			}//if (m_serverSocket != INVALID_SOCKET)

		}//if (m_serverSocket == INVALID_SOCKET)

		return (m_serverSocket != INVALID_SOCKET);
	}

	void SocketServerHandler::initConnectionImpl() {
		if (m_serverSocket != INVALID_SOCKET) {
			//accepting incoming remote connection
			socket_t connSocket = accept(m_serverSocket, NULL, NULL);

			if (connSocket != INVALID_SOCKET)
			{
				{
					//successully connected to remote side
					std::lock_guard<std::mutex> lg(m_socketLock);
					m_connSocket = connSocket;
				}
				
				//establish connectionless connection
				BaseUnreliableSocketHandler::initConnectionImpl();
			}//if (connSocket != INVALID_SOCKET)
		}//if (m_serverSocket != INVALID_SOCKET && m_port != 0)
	}

	void SocketServerHandler::addtionalRcvThreadCleanupImpl() {
		m_socketLock.lock();
		if (m_serverSocket != INVALID_SOCKET)
		{
			closesocket(m_serverSocket);
			m_serverSocket = INVALID_SOCKET;
		}
		m_socketLock.unlock();

		//super
		BaseUnreliableSocketHandler::addtionalRcvThreadCleanupImpl();
	}

	void SocketServerHandler::addtionalSocketCleanupImpl() {
		if (m_serverSocket != INVALID_SOCKET)
		{
			shutdown(m_serverSocket, SD_BOTH);
			closesocket(m_serverSocket);
			m_serverSocket = INVALID_SOCKET;
		}

		//super
		BaseUnreliableSocketHandler::addtionalSocketCleanupImpl();
	}
	
	/*---------------- UnreliableSocketClientHandler -------------------*/
	UnreliableSocketClientHandler::UnreliableSocketClientHandler(int connLessListeningPort, const ConnectionEndpoint& connLessRemoteEndpoint)
	: BaseUnreliableSocketHandler(connLessListeningPort), m_connLessRemoteEndpoint(connLessRemoteEndpoint), m_connLessRemoteReachable(false)
	{
		m_enableReconnect = false;
	}
	
	UnreliableSocketClientHandler::~UnreliableSocketClientHandler() {
		
	}
	
	bool UnreliableSocketClientHandler::connected() const {
		return m_connLessRemoteReachable;
	}
	
	void UnreliableSocketClientHandler::initConnectionImpl() {
		if (m_connLessRemoteEndpoint.port != 0)
		{
			BaseUnreliableSocketHandler::initConnectionImpl();//super
			
			std::lock_guard<std::mutex> lg(m_socketLock);
			
			//create destination address
			m_connLessSocketDestAddr = std::unique_ptr<sockaddr_in>(new sockaddr_in());
			
			sockaddr_in &sa = *m_connLessSocketDestAddr;
			memset(&sa, 0, sizeof sa);
			
			sa.sin_family = AF_INET;
			sa.sin_addr.s_addr = inet_addr(m_connLessRemoteEndpoint.address.c_str());
			sa.sin_port = htons(m_connLessRemoteEndpoint.port);
			
			//try to ping the destination
			m_connLessRemoteReachable = testUnreliableRemoteEndpointNoLock();
			
			//close socket if destination unreachable
			if (!m_connLessRemoteReachable) {
				if (m_connLessSocket != INVALID_SOCKET)
				{
					closesocket(m_connLessSocket);
					m_connLessSocket = INVALID_SOCKET;
				}
			}
		}//if (m_connLessRemoteEndpoint.port != 0)
	}
	
	void UnreliableSocketClientHandler::addtionalRcvThreadCleanupImpl() {
		m_connLessRemoteReachable = false;
		
		//super
		BaseUnreliableSocketHandler::addtionalRcvThreadCleanupImpl();
	}
	
	void UnreliableSocketClientHandler::addtionalSocketCleanupImpl() {
		m_connLessRemoteReachable = false;
		
		//super
		BaseUnreliableSocketHandler::addtionalSocketCleanupImpl();
	}
	
	/*---------------- SocketClientHandler -------------------*/
	SocketClientHandler::SocketClientHandler(int listeningPort, int connLessListeningPort, const ConnectionEndpoint& remoteEndpoint, const ConnectionEndpoint& connLessRemoteEndpoint)
	: UnreliableSocketClientHandler(connLessListeningPort, connLessRemoteEndpoint),
		m_remoteEndpoint(remoteEndpoint)
	{
	}
	
	SocketClientHandler::~SocketClientHandler() {
		
	}
	
	bool SocketClientHandler::connected() const {
		return (m_connLessRemoteEndpoint.port == 0 || UnreliableSocketClientHandler::connected())
				&& m_connSocket != INVALID_SOCKET;
	}
	
	void SocketClientHandler::initConnectionImpl() {
		_ssize_t re;
		
		sockaddr_in sa;
		memset(&sa, 0, sizeof sa);
		
		sa.sin_family = AF_INET;
		
		//connect to server
		if (m_connSocket == INVALID_SOCKET && m_remoteEndpoint.port != 0) {
			
			std::unique_lock<std::mutex> lk(m_socketLock);
			
			m_connSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (m_connSocket != INVALID_SOCKET) {
				sa.sin_addr.s_addr = inet_addr(m_remoteEndpoint.address.c_str());
				sa.sin_port = htons(m_remoteEndpoint.port);
				
				socket_t l_connSocket = m_connSocket;
				lk.unlock();
				
				re = ::connect(l_connSocket, (sockaddr*)&sa, sizeof(sa));
				if (re == SOCKET_ERROR) {
					//failed
					
					lk.lock();
					if (m_connSocket != INVALID_SOCKET)
					{
						closesocket(m_connSocket);
						m_connSocket = INVALID_SOCKET;
					}
					lk.unlock();
				}
				
			}//if (m_connSocket != INVALID_SOCKET && m_remoteEndpoint.port != 0)
			
		}//if (m_connSocket == INVALID_SOCKET)
		
		//create connectionless socket
		UnreliableSocketClientHandler::initConnectionImpl();
	}
	
	void SocketClientHandler::addtionalRcvThreadCleanupImpl() {
		//super
		UnreliableSocketClientHandler::addtionalRcvThreadCleanupImpl();
	}
	
	void SocketClientHandler::addtionalSocketCleanupImpl() {
		
		//super
		UnreliableSocketClientHandler::addtionalSocketCleanupImpl();
	}
}