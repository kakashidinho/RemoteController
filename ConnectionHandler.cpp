#include "ConnectionHandler.h"
#include "Timer.h"

#include <assert.h>
#include <fstream>
#include <sstream>
#include <limits>

#define SIMULATED_MAX_UDP_PACKET_SIZE 0
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
#	define CONN_INPROGRESS WSAEWOULDBLOCK

typedef int socklen_t;

#else//#ifdef WIN32

#	define SOCKET_ERROR -1
#	define INVALID_SOCKET -1
#	define SD_BOTH SHUT_RDWR
#	define closesocket close
#	define _INADDR_ANY htonl(INADDR_ANY)
#	define MSGSIZE_ERROR EMSGSIZE 
#	define CONN_INPROGRESS EINPROGRESS

#endif//#ifdef WIN32

namespace HQRemote {
	enum ReliableBufferState {
		READ_NEXT_MESSAGE_SIZE,
		READ_MESSAGE
	};
	
	enum MsgChunkType :uint32_t {
		MSG_HEADER,
		FRAGMENT_HEADER,
		PING_MSG_CHUNK,
		PING_REPLY_MSG_CHUNK
	};

	union MsgChunkHeader 
	{
		struct {
			uint64_t id;
			MsgChunkType type;
			uint32_t reserved;

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

	struct IConnectionHandler::MsgChunk {
		MsgChunk() {
			assert(offsetHeaderToPayload() == 0);
		}

		MsgChunkHeader header;
		unsigned char payload[MAX_FRAGMEMT_SIZE];

	private:
		size_t offsetHeaderToPayload() const {
			return (unsigned char*)&payload - (unsigned char*)this - sizeof(header);
		}
	};

	/*--------------- IConnectionHandler -----------*/
	IConnectionHandler::IConnectionHandler()
	: m_running(false), m_recvRate(0)
	{
	}
	
	IConnectionHandler::~IConnectionHandler() {
	}
	
	void IConnectionHandler::setInternalError(const char* msg)
	{
		if (msg == NULL)
			m_internalError = nullptr;
		else
		{
#ifdef DEBUG
			HQRemote::Log("setInternalError('%s')\n", msg);
#endif
			m_internalError = std::make_shared<CString>(msg);
		}
	}
	
	bool IConnectionHandler::start() {
		stop();
		
		if (!startImpl())
			return false;
		
		m_running = true;
		
		m_internalError = nullptr;
		
		invalidateUnusedReliableData();
		
		m_numLastestDataReceived = 0;
		m_recvRate = 0;
		
		getTimeCheckPoint(m_startTime);

		return true;
	}
	
	void IConnectionHandler::stop() {
		m_running = false;
		
		stopImpl();
		
		//wake any user thread blocked when trying to retrieve data
		{
			m_dataLock.lock();
			m_dataCv.notify_all();
			m_dataLock.unlock();
		}
		
#ifdef DEBUG
		Log("IConnectionHandler::stop() finished\n");
#endif
	}
	
	bool IConnectionHandler::running() const {
		return m_running.load(std::memory_order_relaxed);
	}
	
	double IConnectionHandler::timeSinceStart() const {
		if (!m_running.load(std::memory_order_relaxed))
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
	
	inline void IConnectionHandler::sendRawDataAtomic(const void* data, size_t size)
	{
		_ssize_t re;
		
		size_t offset = 0;
		do {
			re = sendRawDataImpl((const char*)data + offset, size - offset);
			offset += re;
		} while (re > 0 && offset < size);
	}
	
	void IConnectionHandler::sendData(const void* data, size_t size)
	{
		assert(size <= 0xffffffff);
		
		//send size of message first
		uint32_t sizeToSend = (uint32_t)size;
		sendRawDataAtomic(&sizeToSend, sizeof(sizeToSend));
		
		
		//send message itself
		sendRawDataAtomic(data, size);
		
		flushRawDataImpl();
	}
	
	void IConnectionHandler::sendDataUnreliable(const void* data, size_t size)
	{
		_ssize_t re = 0;
		assert(size <= 0xffffffff);
		
		//TODO: assume all sides use the same byte order for now
		uint32_t headerSize = sizeof(MsgChunkHeader);
		
		MsgChunkHeader header;
		header.type = MSG_HEADER;
		header.id = generateIDFromTime();
		header.wholeMsgInfo.msg_size = (uint32_t)size;//whole message size
		
		MsgChunk chunk;
		chunk.header = header;
		
		//send header
		re = sendRawDataUnreliableImpl(&chunk, headerSize);
		if (re < headerSize)
			return;//failed
		
		//send data's fragments
		uint32_t maxFragmentSize = sizeof(chunk.payload);
		//TODO: assume all sides use the same byte order for now
		chunk.header.type = FRAGMENT_HEADER;
		chunk.header.fragmentInfo.offset = 0;
		
		uint32_t chunkPayloadSize;
		
		do {
			auto remainSize = (uint32_t)size - chunk.header.fragmentInfo.offset;
			chunkPayloadSize = min(maxFragmentSize, remainSize);
			auto sizeToSend = headerSize + chunkPayloadSize;
			
			//fill chunk's data
			memcpy(chunk.payload, (const char*)data + chunk.header.fragmentInfo.offset, chunkPayloadSize);
			
			//send chunk
			re = sendRawDataUnreliableImpl(&chunk, sizeToSend);
			
			if (re > 0) {
				if (re < headerSize)
					re = -1;//not even able to send the header data. Treat as error
				else {
					uint32_t sentPayloadSize = (uint32_t)re - headerSize;
					if (sentPayloadSize < chunkPayloadSize)//partially sent, reduce max payload size for next fragment
						maxFragmentSize = sentPayloadSize;
					chunk.header.fragmentInfo.offset += sentPayloadSize;//next fragment
				}
			}//if (re > 0)
			
		} while (re > 0 && chunk.header.fragmentInfo.offset < size);
	}
	
	inline void IConnectionHandler::fillReliableBuffer(const void* &data, size_t& size)
	{
		assert(m_reliableBuffer.data != nullptr && m_reliableBuffer.filledSize <= m_reliableBuffer.data->size());
		
		auto remainSizeToFill = m_reliableBuffer.data->size() - m_reliableBuffer.filledSize;
		auto sizeToFill = min(remainSizeToFill, size);
		memcpy(m_reliableBuffer.data->data() + m_reliableBuffer.filledSize, data, sizeToFill);
		m_reliableBuffer.filledSize += sizeToFill;
		
		//the remaining data will be used later
		size -= sizeToFill;
		data = (const unsigned char*)data + sizeToFill;
	}
	
	void IConnectionHandler::onReceiveReliableData(const void* data, size_t size)
	{
		while (size > 0)
		{
			switch (m_reliableBufferState)
			{
				case READ_NEXT_MESSAGE_SIZE:
				{
					//we are expecting message size
					if (m_reliableBuffer.data == nullptr)
					{
						m_reliableBuffer.data = std::make_shared<CData>(sizeof(uint32_t));
						m_reliableBuffer.filledSize = 0;
					}
					
					fillReliableBuffer(data, size);
					
					if (m_reliableBuffer.data->size() == m_reliableBuffer.filledSize)//full
					{
						uint32_t messageSize;
						memcpy(&messageSize, m_reliableBuffer.data->data(), sizeof(messageSize));
						
						//initialize placeholder for message data
						m_reliableBuffer.data = nullptr;
						
						try {
							m_reliableBuffer.data = std::make_shared<CData>(messageSize);
							m_reliableBuffer.filledSize = 0;
							
							m_reliableBufferState = READ_MESSAGE;
						} catch (...)
						{
							//memory failed
						}
					}//if (m_reliableBuffer.data->size() == m_reliableBuffer.filledSize)
				}
					break;
				case READ_MESSAGE:
				{
					//we are expecting message data
					fillReliableBuffer(data, size);
					
					if (m_reliableBuffer.data->size() == m_reliableBuffer.filledSize)//full
					{
						//copy message's data to queue for user to read
						pushDataToQueue(m_reliableBuffer.data, true, false);
						
						m_reliableBufferState = READ_NEXT_MESSAGE_SIZE;//waiting for next message
						m_reliableBuffer.data = nullptr;
					}
				}
					break;
			}
		}//while (size > 0)
	}
	
	void IConnectionHandler::invalidateUnusedReliableData()
	{
		m_reliableBufferState = READ_NEXT_MESSAGE_SIZE;
		m_reliableBuffer.data = nullptr;
		m_reliableBuffer.filledSize = 0;
	}
	
	void IConnectionHandler::onReceivedUnreliableDataFragment(const void* data, size_t size)
	{
		if (size < sizeof(MsgChunkHeader))
			return;
		const MsgChunk& chunk  = *(const MsgChunk*)data;
		
		try {
			switch (chunk.header.type) {
				case MSG_HEADER:
				{
					auto data = std::make_shared<CData>(chunk.header.wholeMsgInfo.msg_size);
					//initialize a placeholder for upcoming message
					if (m_unreliableBuffers.size() == MAX_PENDING_UNRELIABLE_BUF)//discard oldest pending message
					{
						auto first = m_unreliableBuffers.begin();
						m_unreliableBuffers.erase(first);
						
	#if defined DEBUG || defined _DEBUG
						fprintf(stderr, "discarded a message\n");
	#endif
					}
					
					MsgBuf newBuf;
					newBuf.data = data;
					newBuf.filledSize = 0;
					
					m_unreliableBuffers.insert(std::pair<uint64_t, MsgBuf>(chunk.header.id, newBuf));
				}
					break;
				case FRAGMENT_HEADER:
				{
					//fill the pending message's buffer
					auto pendingBufIte = m_unreliableBuffers.find(chunk.header.id);
					if (pendingBufIte != m_unreliableBuffers.end()) {
						auto& buffer = pendingBufIte->second;
						
						auto payloadSize = size - sizeof(chunk.header);
						
						memcpy(buffer.data->data() + chunk.header.fragmentInfo.offset, chunk.payload, payloadSize);
						buffer.filledSize += payloadSize;
						
						//message is complete, push to data queue for comsuming
						if (buffer.filledSize >= buffer.data->size()) {
							pushDataToQueue(buffer.data, false, true);
							
							//remove from pending list
							m_unreliableBuffers.erase(pendingBufIte);
						}
					}
	#if defined DEBUG || defined _DEBUG
					else {
						fprintf(stderr, "discarded a fragment\n");
					}
	#endif
				}
					break;
			}//switch (chunk.header.type)
		} catch (...)
		{
			//TODO
		}
	}
	

	void IConnectionHandler::onConnected()
	{
		//reset internal buffer
		invalidateUnusedReliableData();
		
		//reset data rate counter
		getTimeCheckPoint(m_lastRecvTime);
		m_numLastestDataReceived = 0;
		m_recvRate = 0;

		//invoke callback> TODO: don't allow unregisterConnectedCallback() to be called inside callback
		for (auto& callback : m_delegates) {
			callback->onConnected();
		}
	}
	
	void IConnectionHandler::registerDelegate(Delegate* d) {
		if (d)
			m_delegates.insert(d);
	}

	void IConnectionHandler::unregisterDelegate(Delegate* d) {
		auto ite = m_delegates.find(d);
		if (ite != m_delegates.end())
			m_delegates.erase(ite);
	}

	//return data to user
	DataRef IConnectionHandler::receiveData(bool &isReliable)
	{
		DataRef data = nullptr;
		
		if (m_dataLock.try_lock()){
			if (m_dataQueue.size() > 0)
			{
				auto &dataEntry = m_dataQueue.front();
				
				data = dataEntry.data;
				isReliable = dataEntry.isReliable;

				m_dataQueue.pop_front();
			}
			
			m_dataLock.unlock();
		}
		
		return data;
	}
	
	DataRef IConnectionHandler::receiveDataBlock(bool &isReliable) {
		std::unique_lock<std::mutex> lk(m_dataLock);
		
		m_dataCv.wait(lk, [this] { return !m_running || m_dataQueue.size() > 0; });
		
		if (m_dataQueue.size())
		{
			auto &dataEntry = m_dataQueue.front();

			auto re = dataEntry.data;
			isReliable = dataEntry.isReliable;

			m_dataQueue.pop_front();
			return re;
		}
		return nullptr;
	}
	
	void IConnectionHandler::pushDataToQueue(DataRef data, bool reliable, bool discardIfFull) {
		std::lock_guard<std::mutex> lg(m_dataLock);
		
		//calculate data rate
		time_checkpoint_t curTime;
		getTimeCheckPoint(curTime);
		
		m_numLastestDataReceived += data->size();
		
		auto elapsedTime = getElapsedTime(m_lastRecvTime, curTime);
		if (elapsedTime >= RCV_RATE_UPDATE_INTERVAL)
		{
			auto oldRcvRate = m_recvRate.load(std::memory_order_relaxed);
			auto newRcvRate = 0.8f * oldRcvRate + 0.2f * m_numLastestDataReceived / (float)elapsedTime;
			m_recvRate.store(newRcvRate, std::memory_order_relaxed);
			
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
		
		m_dataQueue.push_back(ReceivedData(data, reliable));
		
		m_dataCv.notify_all();
	}

	/*----------------SocketConnectionHandler ----------------*/
	SocketConnectionHandler::SocketConnectionHandler()
		:m_connLessSocket(INVALID_SOCKET), m_connSocket(INVALID_SOCKET), m_enableReconnect(true)
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
		return m_connSocket.load(std::memory_order_relaxed) != INVALID_SOCKET || 
			(m_connLessSocket.load(std::memory_order_relaxed) != INVALID_SOCKET && m_connLessSocketDestAddr != nullptr);
	}

	_ssize_t SocketConnectionHandler::sendRawDataImpl(const void* data, size_t size)
	{
		_ssize_t re = 0;
		
		m_socketLock.lock();
		if (m_connSocket == INVALID_SOCKET)//fallback to unreliable socket
		{
			m_socketLock.unlock();
			sendDataUnreliable(data, size);
			
			re = size;
		}
		else
		{
			re = sendRawDataNoLock(m_connSocket, data, size);
			
			m_socketLock.unlock();
		}
		
		return re;
	}
	
	void SocketConnectionHandler::flushRawDataImpl() {
		//DO NOTHING: we don't use buffering
		
	}

	_ssize_t SocketConnectionHandler::sendRawDataUnreliableImpl(const void* data, size_t size)
	{
		_ssize_t re = 0;
		
		m_socketLock.lock();

		if (m_connLessSocket == INVALID_SOCKET || m_connLessSocketDestAddr == nullptr)//fallback to reliable socket
		{
			if (m_connSocket != INVALID_SOCKET)
				re = sendRawDataNoLock(m_connSocket, data, size);
		}
		else
			re = sendRawDataUnreliableNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), data, size);
		
		m_socketLock.unlock();
		
		return re;
	}

	_ssize_t SocketConnectionHandler::sendRawDataNoLock(socket_t socket, const void* data, size_t size) {
		return send(socket, (const char*)data, size, 0);
	}
	
	_ssize_t SocketConnectionHandler::sendRawDataUnreliableNoLock(socket_t socket, const sockaddr_in* pDstAddr, const void* data, size_t size) {
		
#if SIMULATED_MAX_UDP_PACKET_SIZE
		auto maxSentSize = min(SIMULATED_MAX_UDP_PACKET_SIZE, size);
		auto re = sendto(socket, data, maxSentSize, 0, (const sockaddr*)pDstAddr, sizeof(sockaddr_in));
		return re;
		
#else//SIMULATED_MAX_UDP_PACKET_SIZE
		_ssize_t re = 0;
		bool restart = false;
			
		//send data's fragment

		do {
			re = sendto(socket, (const char*)data, size, 0, (const sockaddr*)pDstAddr, sizeof(sockaddr_in));
					
			if (re == SOCKET_ERROR)
			{
				//exceed max message size, try to reduce it
				if (platformGetLastSocketErr() == MSGSIZE_ERROR && size > 1) {
					size >>= 1;

					restart = true;//restart in next iteration
				}
			}
			else {
				restart = false;
			}
		} while (restart);
		
		return re;
#endif//SIMULATED_MAX_UDP_PACKET_SIZE
	}
	
	_ssize_t SocketConnectionHandler::sendChunkUnreliableNoLock(socket_t socket, const sockaddr_in* pDstAddr, const MsgChunk& chunk, size_t size) {
		assert(size >= sizeof(chunk.header));
		
		if (pDstAddr == NULL)//we must have info of remote size's address
			return 0;
		return sendto(socket, (char*)&chunk, size, 0, (const sockaddr*)pDstAddr, sizeof(sockaddr_in));
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

	_ssize_t SocketConnectionHandler::recvRawDataNoLock(socket_t socket) {
		char buffer[1024];
		_ssize_t re;

		re = recv(socket, buffer, sizeof(buffer), 0);
		if (re > 0)
		{
			onReceiveReliableData(buffer, re);
		}
		
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
		case PING_MSG_CHUNK:
		{
			//reply
			chunk.header.type = PING_REPLY_MSG_CHUNK;
			sendChunkUnreliableNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), chunk, re);
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
		default:
			//call parent's handler
			onReceivedUnreliableDataFragment(&chunk, re);
			break;
		}//switch (chunk.header.type)

		return re;
	}
	
	_ssize_t SocketConnectionHandler::pingUnreliableNoLock(time_checkpoint_t sendTime) {
		//ping remote host
		MsgChunk pingChunk;
		pingChunk.header.type = PING_MSG_CHUNK;
		pingChunk.header.id = generateIDFromTime(sendTime);
		
		pingChunk.header.pingInfo.sendTime = convertToTimeCheckPoint64(sendTime);
		
		return sendChunkUnreliableNoLock(m_connLessSocket, m_connLessSocketDestAddr.get(), pingChunk, sizeof(pingChunk.header));
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
					connectedBefore = true;
				}

				//wait for at most 1ms
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
						re = recvRawDataNoLock(l_connSocket);

						if (re == SOCKET_ERROR || re == 0) {
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
				
				if (connected())
					onConnected();
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
					LogErr("Failed to bind connectionless socket, error = %d\n", platformGetLastSocketErr());

					closesocket(m_connLessSocket);
					m_connLessSocket = INVALID_SOCKET;
				}
			}//if (m_connLessSocket != INVALID_SOCKET)
			else
				LogErr("Failed to create connectionless socket, error = %d\n", platformGetLastSocketErr());
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
		return (m_connLessPort == 0 || m_connLessSocket.load(std::memory_order_relaxed) != INVALID_SOCKET)
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

				int true_val = 1;
				setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&true_val, sizeof true_val);
#ifdef SO_REUSEPORT
				setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEPORT, (const char*)&true_val, sizeof true_val);
#endif

				re = ::bind(m_serverSocket, (sockaddr*)&sa, sizeof(sa));
				if (re == SOCKET_ERROR) {
					//failed
					LogErr("Failed to bind server socket, error = %d\n", platformGetLastSocketErr());

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
			else
				LogErr("Failed to create server socket, error = %d\n", platformGetLastSocketErr());

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
		return m_connLessRemoteReachable.load(std::memory_order_relaxed);
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
				&& m_connSocket.load(std::memory_order_relaxed) != INVALID_SOCKET;
	}
	
	void SocketClientHandler::initConnectionImpl() {
		_ssize_t re, re2;
		int err;
		fd_set sset, eset;
		timeval tv;
		
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
				
				platformSetSocketBlockingMode(m_connSocket, false);//disable blocking mode

				socket_t l_connSocket = m_connSocket;
				lk.unlock();
				
				re = ::connect(l_connSocket, (sockaddr*)&sa, sizeof(sa));

				if (re == SOCKET_ERROR) {
					err = platformGetLastSocketErr();
					while (re == SOCKET_ERROR && err == CONN_INPROGRESS && m_running)
					{
						FD_ZERO(&sset);
						FD_ZERO(&eset);
						FD_SET(l_connSocket, &sset);
						FD_SET(l_connSocket, &eset);
						tv.tv_sec = 1;             /* 1 second timeout */
						tv.tv_usec = 0;

						if ((re2 = select(l_connSocket + 1, NULL, &sset, &eset, &tv)) > 0)
						{
							socklen_t errlen = sizeof(err);

							if (FD_ISSET(l_connSocket, &eset))//error
							{
								getsockopt(l_connSocket, SOL_SOCKET, SO_ERROR, (char*)(&err), &errlen);
							}
							else if (FD_ISSET(l_connSocket, &sset))//socket available for write
							{
								//verify that there is no error
								getsockopt(l_connSocket, SOL_SOCKET, SO_ERROR, (char*)(&err), &errlen);
								if (err == 0)
									re = 0;//succeeded
							}
						}
						else if (re2 == SOCKET_ERROR)
							err = platformGetLastSocketErr();
					}//while (re == SOCKET_ERROR && err == CONN_INPROGRESS && m_running)

				}//if (re == SOCKET_ERROR)

				if (re != SOCKET_ERROR)
				{
					//enable blocking mode
					if ((re = platformSetSocketBlockingMode(l_connSocket, true)) == SOCKET_ERROR)
						err = platformGetLastSocketErr();
				}

				if (re == SOCKET_ERROR) {
					//failed
					LogErr("Failed to connect socket, error = %d\n", err);
					
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