#include "ConnectionHandler.h"
#include "Timer.h"

#include <assert.h>
#include <fstream>
#include <sstream>
#include <limits>

#define MAX_FRAGMEMT_SIZE (16 * 1024)
#define MAX_PENDING_UNRELIABLE_BUF 20

#ifdef WIN32
#	include <windows.h>

#	define _INADDR_ANY INADDR_ANY
#	define MSGSIZE_ERROR WSAEMSGSIZE 

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
	void IConnectionHandler::sendData(ConstDataRef data) {
		sendData(data->data(), data->size());
	}

	void IConnectionHandler::sendDataUnreliable(ConstDataRef data) {
		sendDataUnreliable(data->data(), data->size());
	}

	/*----------------SocketConnectionHandler ----------------*/
	SocketConnectionHandler::SocketConnectionHandler()
		:m_running(false), m_connLessSocket(INVALID_SOCKET), m_connSocket(INVALID_SOCKET)
	{
		platformConstruct();
	}

	SocketConnectionHandler::~SocketConnectionHandler()
	{
		platformDestruct();

		stop();
	}


	void SocketConnectionHandler::start()
	{
		stop();

		m_running = true;

		//start background thread to receive remote event
		m_recvThread = std::unique_ptr<std::thread>(new std::thread([this] {
			recvProc();
		}));
	}

	void SocketConnectionHandler::stop()
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

		//join with all threads
		if (m_recvThread != nullptr && m_recvThread->joinable())
		{
			m_recvThread->join();

			m_recvThread = nullptr;
		}
	}

	bool SocketConnectionHandler::connected() const {
		return m_connSocket != INVALID_SOCKET || m_connLessSocket != INVALID_SOCKET;
	}

	//obtain data received through background thread
	DataRef SocketConnectionHandler::receiveData()
	{
		DataRef data = nullptr;

		if (m_dataLock.try_lock()){
			data = m_dataQueue.front();
			m_dataQueue.pop_front();

			m_dataLock.unlock();
		}

		return data;
	}

	void SocketConnectionHandler::pushDataToQueue(DataRef data) {
		std::lock_guard<std::mutex> lg(m_dataLock);

		m_dataQueue.push_back(data);
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

		if (m_connLessSocket == INVALID_SOCKET)//fallback to reliable socket
			sendDataNoLock(data, size);
		else
			sendDataUnreliableNoLock(data, size);
	}

	void SocketConnectionHandler::sendDataUnreliableNoLock(const void* data, size_t size)
	{
		sendDataNoLock(m_connLessSocket, m_connLessSocketAddr.get(), data, size);
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

		if (socket == m_connSocket)//TCP
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
					chunk.header.fragmentInfo.offset += re;//next fragment
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
		if (m_connLessSocketAddr != nullptr)
		{
			return recvfrom(socket, (char*)&chunk, sizeof(chunk), 0, NULL, NULL);
		}
		else {
			//obtain remote size's address
			int len = sizeof(sockaddr_in);

			m_connLessSocketAddr = std::make_unique<sockaddr_in>();

			auto re = recvfrom(socket, (char*)&chunk, sizeof(chunk), 0, (sockaddr*)m_connLessSocketAddr.get(), &len);

			return re;
		}
	}

	_ssize_t SocketConnectionHandler::sendDataAtomicNoLock(socket_t socket, const void* data, size_t size) {
		_ssize_t re;

		size_t offset = 0;
		do {
			re = send(socket, (const char*)data + offset, size - offset, 0);
			offset += re;
		} while (re != SOCKET_ERROR && offset < size);

		return re == SOCKET_ERROR ? re : size;
	}

	_ssize_t SocketConnectionHandler::recvDataAtomicNoLock(socket_t socket, void* data, size_t expectedSize) {
		_ssize_t re;
		size_t offset = 0;
		char* ptr = (char*)data;
		do {
			re = recv(socket, ptr + offset, expectedSize - offset, 0);
			offset += re;
		} while (re != SOCKET_ERROR && offset < expectedSize);

		if (re == SOCKET_ERROR)
			return re;

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
				pushDataToQueue(data);
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
					pushDataToQueue(buffer.data);
					
					//remove from pending list
					m_connLessBuffers.erase(pendingBufIte);
				}
			}
		}
			break;
		}//switch (chunk.header.type)

		return re;
	}

	void SocketConnectionHandler::recvProc()
	{
		//TODO: separate thread for tcp and udp
		SetCurrentThreadName("remoteDataReceiverThread");

		_ssize_t re;
		while (m_running) {
			m_socketLock.lock();
			socket_t l_connSocket = m_connSocket;
			socket_t l_connLessSocket = m_connLessSocket;
			m_socketLock.unlock();
			//we have an existing connection
			if (l_connSocket != INVALID_SOCKET || l_connLessSocket != INVALID_SOCKET) {
				//wat for at most 1ms
				timeval timeout;
				timeout.tv_sec = 0;
				timeout.tv_usec = 1000;

				fd_set sset;

				//read data sent via unreliable socket
				if (l_connLessSocket != INVALID_SOCKET) {
					FD_ZERO(&sset);
					FD_SET(l_connLessSocket, &sset);

					if (select(1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connLessSocket, &sset))
					{
						re = recvDataUnreliableNoLock(l_connLessSocket);

						if (re == SOCKET_ERROR) {
							std::lock_guard<std::mutex> lg(m_socketLock);
							//close socket
							closesocket(m_connLessSocket);
							m_connLessSocket = INVALID_SOCKET;
						}
					}//if (select(1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
				}//if (l_connLessSocket != INVALID_SOCKET)

				//read data sent via reliable socket
				if (l_connSocket != INVALID_SOCKET) {
					FD_ZERO(&sset);
					FD_SET(l_connSocket, &sset);

					if (select(1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
					{
						re = recvDataNoLock(l_connSocket);

						if (re == SOCKET_ERROR) {
							std::lock_guard<std::mutex> lg(m_socketLock);
							//close socket
							closesocket(m_connSocket);
							m_connSocket = INVALID_SOCKET;
						}
					}//if (select(1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
				} //if (l_connSocket != INVALID_SOCKET)

			}//if (l_connSocket != INVALID_SOCKET || l_connLessSocket != INVALID_SOCKET)

			else {
				//initialize connection
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

	void BaseUnreliableSocketHandler::initConnectionImpl() {
		_ssize_t re;

		sockaddr_in sa;
		memset(&sa, 0, sizeof sa);

		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = _INADDR_ANY;

		 //create connection less socket
		if (m_connLessSocket == INVALID_SOCKET && m_connLessPort != 0) {
			m_connLessSocketAddr = nullptr;

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
	}

	void BaseUnreliableSocketHandler::addtionalRcvThreadCleanupImpl() {
	}

	void BaseUnreliableSocketHandler::addtionalSocketCleanupImpl() {
	}

	/*-------------  SocketServerHandler  ---------------------------*/
	SocketServerHandler::SocketServerHandler(int listeningPort, int connLessListeningPort)
	: BaseUnreliableSocketHandler(connLessListeningPort),  m_port(listeningPort)
	{
	}

	SocketServerHandler::~SocketServerHandler() {

	}

	void SocketServerHandler::initConnectionImpl() {
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

		if (m_serverSocket != INVALID_SOCKET) {
			//accepting incoming remote connection
			socket_t connSocket = accept(m_serverSocket, NULL, NULL);

			if (connSocket != INVALID_SOCKET)
			{
				//successully connected to remote side
				std::lock_guard<std::mutex> lg(m_socketLock);
				m_connSocket = connSocket;
			}//if (connSocket != INVALID_SOCKET)
		}//if (m_serverSocket != INVALID_SOCKET && m_port != 0)

		//create connection less socket
		BaseUnreliableSocketHandler::initConnectionImpl();
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
}