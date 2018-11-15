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

#include "ConnectionHandler.h"
#include "Timer.h"

#include <assert.h>
#include <fstream>
#include <sstream>
#include <limits>

#define SIMULATED_MAX_UDP_PACKET_SIZE 0
#define MAX_FRAGMEMT_SIZE (16 * 1024)
#define MAX_PENDING_UNRELIABLE_BUF 100
#define UNRELIABLE_PING_TIMEOUT 3
#define UNRELIABLE_PING_RETRIES 10
#define UNRELIABLE_PING_INTERVAL 10

#define NUM_PENDING_MSGS_TO_START_DISCARD 60

#define DATA_RATE_UPDATE_INTERVAL 1.0

#ifndef min
#	define min(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifdef WIN32
#	include <windows.h>
#	include <Ws2tcpip.h>

#	define accept(sock, addrp, lenp) WSAAccept(sock, addrp, lenp, NULL, NULL)
#	define _INADDR_ANY INADDR_ANY
#	define MSGSIZE_ERROR WSAEMSGSIZE 
#	define CONN_INPROGRESS WSAEWOULDBLOCK
#	define _EWOULDBLOCK WSAEWOULDBLOCK
#	define _EAGAIN WSAEWOULDBLOCK

typedef int socklen_t;

#else//#ifdef WIN32

#	define SOCKET_ERROR -1
#	define INVALID_SOCKET -1
#	define SD_BOTH SHUT_RDWR
#	define closesocket close
#	define _INADDR_ANY htonl(INADDR_ANY)
#	define MSGSIZE_ERROR EMSGSIZE 
#	define CONN_INPROGRESS EINPROGRESS
#	define _EWOULDBLOCK EWOULDBLOCK
#	define _EAGAIN EAGAIN

#endif//#ifdef WIN32

namespace HQRemote {
	static const char DEFAULT_MULTICAST_ADDRESS[] = "226.1.1.2";
	static const unsigned int MULTICAST_MAX_MSG_SIZE = 512;
	static const char MULTICAST_MAGIC_STRING[] = "fd8acc40-d758-4d2c-a6a9-91387845e1ca";

	enum ReliableBufferState {
		READ_NEXT_MESSAGE_SIZE,
		READ_MESSAGE
	};
	
	enum MsgChunkType :uint32_t {
		MSG_HEADER, // deprecated message chunk, for compatible with older client
		FRAGMENT_HEADER, // deprecated message chunk, for compatible with older client
		PING_MSG_CHUNK,
		PING_REPLY_MSG_CHUNK,
		FRAGMENT_HEADER_EX,
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
				} wholeMsgInfo; // deprecated message chunk, for compatible with older client

				struct {
					uint32_t offset;
					uint32_t total_msg_size;
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
			assert((void*)this == (void*)&header);
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
	: m_running(false), m_recvRate(0), m_tag(0), m_sentRate(0),
		m_compatibleMode(true)
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

		// assume all data successfully sent. It doesn't need to be accurate anyway
		updateDataSentRate(size + sizeof(sizeToSend));
	}
	
	void IConnectionHandler::sendDataUnreliable(const void* data, size_t size) {
		_ssize_t re = 0;
		assert(size <= 0xffffffff);
		
		//TODO: assume all sides use the same byte order for now
		uint32_t headerSize = sizeof(MsgChunkHeader);
		
		MsgChunk chunk;
		chunk.header.id = generateIDFromTime();

		uint32_t maxFragmentSize = sizeof(chunk.payload);

		if (m_compatibleMode) {
			chunk.header.type = MSG_HEADER;
			chunk.header.wholeMsgInfo.msg_size = (uint32_t)size;//whole message size

			//send header containing info about the data first
			re = sendRawDataUnreliableImpl(&chunk, headerSize);
			if (re < (_ssize_t)headerSize)
				return;//failed
			updateDataSentRate(headerSize);

			//send data's fragments next
			//TODO: assume all sides use the same byte order for now
			chunk.header.type = FRAGMENT_HEADER;
			chunk.header.fragmentInfo.offset = 0;
		}
		else {
			// newer version: each fragment now contains info about its data's total size

			// we will send data's fragments one by one
			chunk.header.type = FRAGMENT_HEADER_EX;
			chunk.header.fragmentInfo.total_msg_size = (uint32_t)size;//whole message size
			chunk.header.fragmentInfo.offset = 0;
		}
		
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

				updateDataSentRate(re);

				if ((uint32_t)re < headerSize)
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
	
	IConnectionHandler::UnreliableBuffers::iterator IConnectionHandler::getOrCreateUnreliableBuffer(uint64_t id, size_t size) {
		// find existing entry
		auto ite = m_unreliableBuffers.find(id);
		if (ite != m_unreliableBuffers.end())
			return ite;

		auto data = std::make_shared<CData>(size);
		//initialize a placeholder for upcoming message
		if (m_unreliableBuffers.size() == MAX_PENDING_UNRELIABLE_BUF)//discard oldest pending message
		{
			auto first = m_unreliableBuffers.begin();
			m_unreliableBuffers.erase(first);

#if defined DEBUG || defined _DEBUG
			HQRemote::LogErr("discarded an unreliable message\n");
#endif
		}

		MsgBuf newBuf;
		newBuf.data = data;
		newBuf.filledSize = 0;

		auto re = m_unreliableBuffers.insert(std::pair<uint64_t, MsgBuf>(id, newBuf));

		return re.first;
	}

	void IConnectionHandler::onReceivedUnreliableDataFragment(const void* recv_data, size_t recv_size)
	{
		if (recv_size < sizeof(MsgChunkHeader))
			return;
		MsgChunkHeader chunkHeader;
		memcpy(&chunkHeader, recv_data, sizeof(chunkHeader));
		
		try {
			switch (chunkHeader.type) {
				case MSG_HEADER: // deprecated
				{
					getOrCreateUnreliableBuffer(chunkHeader.id, chunkHeader.wholeMsgInfo.msg_size);
				}
					break;
				case FRAGMENT_HEADER: // deprecated
				case FRAGMENT_HEADER_EX:
				{
					// fill the pending message's buffer
					UnreliableBuffers::iterator pendingBufIte;

					if (chunkHeader.type == FRAGMENT_HEADER_EX)
					{
						// new version: create message's buffer if it doesn't exist
						pendingBufIte = getOrCreateUnreliableBuffer(chunkHeader.id, chunkHeader.fragmentInfo.total_msg_size);
					}
					else
					{
						// older version: fragment info cannot create a new buffer
						pendingBufIte = m_unreliableBuffers.find(chunkHeader.id);
					}

					if (pendingBufIte != m_unreliableBuffers.end()) {
						auto& buffer = pendingBufIte->second;

						auto payload = (unsigned char*)recv_data + sizeof(chunkHeader);
						auto payloadSize = recv_size - sizeof(chunkHeader);

						if (chunkHeader.fragmentInfo.offset + payloadSize > buffer.data->size()) // overflow
						{
#if defined DEBUG || defined _DEBUG
							HQRemote::LogErr("discarded a fragment due to oveflow segment (%u sz=%u)\n", chunkHeader.fragmentInfo.offset, payloadSize);
#endif
							break;
						}
					
						memcpy(buffer.data->data() + chunkHeader.fragmentInfo.offset, payload, payloadSize);
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
						HQRemote::LogErr("discarded a fragment\n");
					}
	#endif
				}
					break;
			}//switch (chunkHeader.type)
		} catch (...)
		{
			//TODO
		}
	}
	

	void IConnectionHandler::onConnected(bool reconnected)
	{
		HQRemote::Log("IConnectionHandler::onConnected()\n");

		//reset internal buffer
		invalidateUnusedReliableData();

		// reset to compatible mode
		m_compatibleMode = true;
		
		if (!reconnected)
		{
			//we only do these if this is a fresh connection (not reconnection)
			
			//reset data rate counter
			getTimeCheckPoint(m_lastRecvTime);
			m_numLastestDataReceived = 0;
			m_recvRate = 0;

			getTimeCheckPoint(m_lastSendTime);
			m_numLastestDataSent = 0;
			m_sentRate = 0;

			//clear all pending unhandled data
			m_dataLock.lock();
			m_dataQueue.clear();
			m_dataLock.unlock();

			//invoke callback> TODO: don't allow unregisterConnectedCallback() to be called inside callback
			for (auto& callback : m_delegates) {
				callback->onConnected();
			}
		}
	}

	void IConnectionHandler::onDisconnected() {
		HQRemote::Log("IConnectionHandler::onDisconnected()\n");

		for (auto& callback : m_delegates) {
			callback->onDisconnected();
		}
	}

	void IConnectionHandler::setDesc(const char* desc) {
		if (desc == nullptr)
			m_name = nullptr;
		else
			m_name = std::make_shared<CString>(desc);
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
		if (elapsedTime >= DATA_RATE_UPDATE_INTERVAL)
		{
			auto oldRcvRate = m_recvRate.load(std::memory_order_relaxed);
			auto newRcvRate = 0.8f * oldRcvRate + 0.2f * m_numLastestDataReceived / (float)elapsedTime;
			m_recvRate.store(newRcvRate, std::memory_order_relaxed);
			
			m_lastRecvTime = curTime;
			m_numLastestDataReceived = 0;

#if 0 && (defined DEBUG || defined _DEBUG)
			Log("pushDataToQueue() rcv Bps=%.3f\n", m_recvRate.load(std::memory_order_relaxed));
#endif
		}
		
		//discard data if no more room
		if (discardIfFull && m_dataQueue.size() > NUM_PENDING_MSGS_TO_START_DISCARD)
		{
#if defined DEBUG || defined _DEBUG
			HQRemote::LogErr("discarded a message due to too many in queue\n");
#endif
			return;//ignore
		}
		
		m_dataQueue.push_back(ReceivedData(data, reliable));
		
		m_dataCv.notify_all();
	}

	void IConnectionHandler::updateDataSentRate(size_t sentSize) {
		m_numLastestDataSent += sentSize;
		time_checkpoint_t curTime;
		getTimeCheckPoint(curTime);

		auto elapsedTime = getElapsedTime(m_lastSendTime, curTime);
		if (elapsedTime >= DATA_RATE_UPDATE_INTERVAL)
		{
			auto oldSndRate = m_sentRate.load(std::memory_order_relaxed);
			auto newSndRate = 0.8f * oldSndRate + 0.2f * m_numLastestDataSent / (float)elapsedTime;
			m_sentRate.store(newSndRate, std::memory_order_relaxed);

			m_lastSendTime = curTime;
			m_numLastestDataSent = 0;

#if 0 && (defined DEBUG || defined _DEBUG)
			Log("updateDataSentRate() Bps=%.3f\n", m_sentRate.load(std::memory_order_relaxed));
#endif
		}
	}

	/*----------------SocketConnectionHandler ----------------*/
	const int SocketConnectionHandler::RANDOM_PORT = 0xffff;

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
		auto re = sendto(socket, (const char*)data, maxSentSize, 0, (const sockaddr*)pDstAddr, sizeof(sockaddr_in));
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

	_ssize_t SocketConnectionHandler::recvChunkUnreliableNoLock(socket_t socket, MsgChunk& chunk, sockaddr_in& srcAddr) {
		socklen_t len = sizeof(sockaddr_in);

		auto re = recvfrom(socket, (char*)&chunk, sizeof(chunk), 0, (sockaddr*)&srcAddr, &len);

		return re;
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
		sockaddr_in srcAddr;
		_ssize_t re;

		//read chunk data
		re = recvChunkUnreliableNoLock(socket, chunk, srcAddr);
		if (re == SOCKET_ERROR)
			return re;

		//first unreliable data
		if (m_connLessSocketDestAddr == nullptr)
		{
			//obtain remote size's address and use it as primary destination for sendUnreliable*() functions
			m_connLessSocketDestAddr = std::unique_ptr<sockaddr_in>(new sockaddr_in());
			memcpy(m_connLessSocketDestAddr.get(), &srcAddr, sizeof(sockaddr_in));
		}

		if (srcAddr.sin_addr.s_addr != m_connLessSocketDestAddr->sin_addr.s_addr ||
			srcAddr.sin_port != m_connLessSocketDestAddr->sin_port)
		{
			//we have received an data from unexpected source address
			return handleUnwantedDataFromImpl(srcAddr, &chunk, re);
		}

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
		bool connectedAtleastOnce = false;
		while (m_running) {
			m_socketLock.lock();
			auto l_connected = connected();
			socket_t l_connSocket = m_connSocket;
			socket_t l_connLessSocket = m_connLessSocket;
			m_socketLock.unlock();
			//we have an existing connection
			if (l_connected) {
				if (!connectedAtleastOnce)
				{
					connectedAtleastOnce = true;
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

							if (!connected()) // if this results in disconnected state
								onDisconnected();
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

							if (!connected()) // if this results in disconnected state
								onDisconnected();
						}
					}//if (select(l_connSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(l_connSocket, &sset))
				} //if (l_connSocket != INVALID_SOCKET)

				addtionalRcvThreadHandlerImpl();

			}//if (l_connected)
			else if (connectedAtleastOnce && !m_enableReconnect) {
				//stop
				m_running = false;
			}
			else {
				//initialize connection
				m_connLessSocketDestAddr = nullptr;
				
				initConnectionImpl();
				
				if (connected()) {
					onConnected();
				}
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
		: BaseUnreliableSocketHandler("", connLessListeningPort)
	{
	}

	BaseUnreliableSocketHandler::BaseUnreliableSocketHandler(const char* bindAddress, int connLessListeningPort)
		: SocketConnectionHandler(), m_connLessPort(connLessListeningPort), m_bindAddress(bindAddress != nullptr? bindAddress : "")
	{
	}

	BaseUnreliableSocketHandler::~BaseUnreliableSocketHandler() {

	}

	socket_t BaseUnreliableSocketHandler::createUnreliableSocket(const CString& bindAddr, int port, bool reuseAddr) {
		socket_t new_socket;
		_ssize_t re;

		sockaddr_in sa;
		memset(&sa, 0, sizeof sa);

		sa.sin_family = AF_INET;

		if (bindAddr.size() == 0)
			sa.sin_addr.s_addr = _INADDR_ANY;
		else
			sa.sin_addr = platformIpv4StringToAddr(bindAddr.c_str());

		new_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (new_socket != INVALID_SOCKET) {
			if (reuseAddr)
			{
				int true_val = 1;
				setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&true_val, sizeof true_val);
#ifdef SO_REUSEPORT
				setsockopt(new_socket, SOL_SOCKET, SO_REUSEPORT, (const char*)&true_val, sizeof true_val);
#endif
			}//if (reuseAddr)

			sa.sin_port = port >= RANDOM_PORT? 0 : htons(port);

			Log("Binding connectionless socket to port=%d addr=%s\n", port, bindAddr.c_str());

			re = ::bind(new_socket, (sockaddr*)&sa, sizeof(sa));
			if (re == SOCKET_ERROR) {
				//failed
				LogErr("Failed to bind connectionless socket (port=%d), error = %d\n", port, platformGetLastSocketErr());

				closesocket(new_socket);
				new_socket = INVALID_SOCKET;
			}
		}//if (m_connLessSocket != INVALID_SOCKET)
		else
			LogErr("Failed to create connectionless socket, error = %d\n", platformGetLastSocketErr());

		return new_socket;
	}

	bool BaseUnreliableSocketHandler::socketInitImpl() {
		std::lock_guard<std::mutex> lg(m_socketLock);
		//create connection less socket
		if (m_connLessSocket == INVALID_SOCKET && m_connLessPort != 0) {
			m_connLessSocketDestAddr = nullptr;

			m_connLessSocket = createUnreliableSocket(m_bindAddress, m_connLessPort);

			//get true port number 
			sockaddr_in sa;
			socklen_t addrlen = sizeof(sa);
			if (m_connLessPort >= RANDOM_PORT && getsockname(m_connLessSocket, (sockaddr*)&sa, &addrlen) != SOCKET_ERROR) {
				m_connLessPort = ntohs(sa.sin_port);
			}
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
	SocketServerHandler::SocketServerHandler(int listeningPort, int connLessListeningPort, const char* discovery_multicast_group, int discovery_multicast_port)
	: SocketServerHandler("", listeningPort, connLessListeningPort, discovery_multicast_group, discovery_multicast_port)
	{
	}

	SocketServerHandler::SocketServerHandler(const char* listeningAddr, int listeningPort, int connLessListeningPort, const char* discovery_multicast_group, int discovery_multicast_port)
	: BaseUnreliableSocketHandler(listeningAddr, connLessListeningPort),  m_serverSocket(INVALID_SOCKET), m_port(listeningPort), m_multicastSocket(INVALID_SOCKET),
		m_multicast_address(discovery_multicast_group ? discovery_multicast_group : DEFAULT_MULTICAST_ADDRESS),
		m_multicast_port(discovery_multicast_port)
	{}

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

		if (m_bindAddress.size() == 0)
			sa.sin_addr.s_addr = _INADDR_ANY;
		else {
			sa.sin_addr = platformIpv4StringToAddr(m_bindAddress.c_str());
		}

		std::lock_guard<std::mutex> lg(m_socketLock);
		//create server socket
		if (m_serverSocket == INVALID_SOCKET && m_port != 0) {
			m_serverSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (m_serverSocket != INVALID_SOCKET) {
				sa.sin_port = m_port >= RANDOM_PORT ? 0 : htons(m_port);

				int true_val = 1;
				setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&true_val, sizeof true_val);
#ifdef SO_REUSEPORT
				setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEPORT, (const char*)&true_val, sizeof true_val);
#endif

				Log("Binding server socket to port=%d addr=%s\n", m_port, m_bindAddress.c_str());

				re = ::bind(m_serverSocket, (sockaddr*)&sa, sizeof(sa));
				if (re == SOCKET_ERROR) {
					//failed
					LogErr("Failed to bind server socket, error = %d\n", platformGetLastSocketErr());

					closesocket(m_serverSocket);
					m_serverSocket = INVALID_SOCKET;
				}
				else {
					platformSetSocketBlockingMode(m_serverSocket, false);

					//get true port number 
					socklen_t addrlen = sizeof(sa);
					if (m_port >= RANDOM_PORT && getsockname(m_serverSocket, (sockaddr*)&sa, &addrlen) != SOCKET_ERROR) {
						m_port = ntohs(sa.sin_port);
					}

					//start accepting incoming connections
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

		//create multicast socket
		if (m_multicastSocket == INVALID_SOCKET) {
			if (m_multicast_port != m_connLessPort)//TODO: if user picks our multicast port, we cannot do much besides disabling the multicast socket
			{
				m_multicastSocket = createUnreliableSocket(m_bindAddress, m_multicast_port);

				if (m_multicastSocket != INVALID_SOCKET)
				{
					//enable broadcasting
					int _true = 1;
					setsockopt(m_multicastSocket, SOL_SOCKET, SO_BROADCAST, (char*)&_true, sizeof(_true));

					//join multicast group
					struct ip_mreq mreq;
					mreq.imr_multiaddr = platformIpv4StringToAddr(m_multicast_address.c_str());

					//join multicast group with all available network interfaces
					std::vector<struct in_addr> interface_addresses;
					platformGetLocalAddressesForMulticast(interface_addresses);

					for (auto &_interface : interface_addresses) {
						mreq.imr_interface = _interface;

						re = setsockopt(m_multicastSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq));

						char addr_buffer[20];
						if (platformIpv4AddrToString(&_interface, addr_buffer, sizeof(addr_buffer)) != NULL)
							HQRemote::Log("Multicast setsockopt(IP_ADD_MEMBERSHIP, %s) returned %d\n", addr_buffer, re);
						else
							HQRemote::Log("Multicast setsockopt(IP_ADD_MEMBERSHIP) returned %d\n", re);
					}

					//disable loopback
#if 0
					char loopback = 0;
					re = setsockopt(m_multicastSocket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char*)&loopback, sizeof(loopback));
					HQRemote::Log("Multicast setsockopt(IP_MULTICAST_LOOP) returned %d\n", re);
#endif
				}
			}
		}//if (m_multicastSocket == INVALID_SOCKET)

		return (m_serverSocket != INVALID_SOCKET);
	}

	void SocketServerHandler::initConnectionImpl() {
		if (m_serverSocket != INVALID_SOCKET) {
			//accepting incoming remote connection
			socket_t connSocket;
			int err;

			do {
				{
					std::lock_guard<std::mutex> lg(m_socketLock);
					//receive data from multicast
					pollingMulticastData();

					//check if there is any incoming connection
					connSocket = accept(m_serverSocket, NULL, NULL);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			} while (connSocket == INVALID_SOCKET && ((err = platformGetLastSocketErr()) == _EWOULDBLOCK || err == _EAGAIN));

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

	void SocketServerHandler::pollingMulticastData() {
		if (m_multicastSocket == INVALID_SOCKET)
			return;

		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;

		fd_set sset;

		FD_ZERO(&sset);
		FD_SET(m_multicastSocket, &sset);

		if (select(m_multicastSocket + 1, &sset, NULL, NULL, &timeout) == 1 && FD_ISSET(m_multicastSocket, &sset))
		{
			unsigned char msg[MULTICAST_MAX_MSG_SIZE];

			socklen_t from_addr_len = sizeof(sockaddr_in);
			sockaddr_in from_addr;

			int re = (int)recvfrom(m_multicastSocket, (char*)msg, sizeof(msg), 0, (sockaddr*)&from_addr, &from_addr_len);
			if (re > 0)
			{
				//debug
				int src_port = ntohs(from_addr.sin_port);
				char src_addr_buffer[20];
				if (platformIpv4AddrToString(&from_addr.sin_addr, src_addr_buffer, sizeof(src_addr_buffer)) != NULL) {
					HQRemote::Log("Received multicast data from %s:%d\n", src_addr_buffer, src_port);
				}

				//
				switch (msg[0])
				{
				case PING_MSG_CHUNK:
					//multicast message = PING_MSG_CHUNK | magic string | request_id
					if (re >= sizeof(MULTICAST_MAGIC_STRING) + sizeof(uint64_t) &&
						memcmp(msg + 1, MULTICAST_MAGIC_STRING, sizeof(MULTICAST_MAGIC_STRING) - 1) == 0)
					{
						//reply with our sockets' ports:
						//reply message = | PING_REPLY_MSG_CHUNK | magic string | request_id | reliable port | unreliable port | description length | description |
						unsigned char* msg_ptr = msg;
						size_t msg_size;

						int32_t reliable_port = m_port;
						int32_t unreliable_port = m_connLessPort;

						*msg_ptr = (unsigned char)PING_REPLY_MSG_CHUNK;
						msg_ptr += sizeof(MULTICAST_MAGIC_STRING) + sizeof(uint64_t);

						//TODO: assume all platforms use the same endianess
						memcpy(msg_ptr, &reliable_port, sizeof(reliable_port));
						memcpy(msg_ptr + sizeof(reliable_port), &unreliable_port, sizeof(unreliable_port));
						msg_ptr += sizeof(reliable_port) + sizeof(unreliable_port);

						msg_size = msg_ptr - msg;
						assert(msg_size <= MULTICAST_MAX_MSG_SIZE);

						uint32_t remainSizeForDesc = (uint32_t)(MULTICAST_MAX_MSG_SIZE - msg_size);
						uint32_t  descSize;

						auto descRef = getDesc();

						// no description yet?
						if (descRef == nullptr) {
							// get local address
							struct sockaddr_in sin;
							socklen_t len = sizeof(sin);
							if (getsockname(m_multicastSocket, (struct sockaddr *)&sin, &len) != SOCKET_ERROR) {
								char local_addr_buffer[20];
								if (sin.sin_addr.s_addr != INADDR_ANY && platformIpv4AddrToString(&sin.sin_addr, local_addr_buffer, sizeof(local_addr_buffer)) != NULL) {
									descRef = std::make_shared<CString>(local_addr_buffer);
								}
							}

							if (descRef == nullptr) // last resort
								descRef = std::make_shared<CString>("Unknown");

							HQRemote::LogErr("setDesc() hasn't been called. Using default desc=%s\n", descRef->c_str());
						}

						if (descRef->size() > remainSizeForDesc - sizeof(descSize)) {
							//desc is too large, truncate it
							descSize = remainSizeForDesc - sizeof(descSize);

							memcpy(msg_ptr, &descSize, sizeof(descSize));
							memcpy(msg_ptr + sizeof(descSize), descRef->c_str(), descSize - 3);
							memcpy(msg_ptr + sizeof(descSize) + descSize - 3, "...", 3);
						}
						else {
							descSize = (uint32_t)descRef->size();

							memcpy(msg_ptr, &descSize, sizeof(descSize));
							memcpy(msg_ptr + sizeof(descSize), descRef->c_str(), descSize);
						}

						msg_size += sizeof(descSize) + descSize;

						//send 3 reply messages to avoid packet loss
						sendRawDataUnreliableNoLock(m_connLessSocket, &from_addr, msg, msg_size);
						sendRawDataUnreliableNoLock(m_connLessSocket, &from_addr, msg, msg_size);
						sendRawDataUnreliableNoLock(m_connLessSocket, &from_addr, msg, msg_size);
					}
					break;
				}
			}//if (re > 0)
		}//if (select ...)
	}

	_ssize_t SocketServerHandler::handleUnwantedDataFromImpl(const sockaddr_in& srcAddr, const void* data, size_t size) {
		return 0;
	}

	void SocketServerHandler::addtionalRcvThreadHandlerImpl() {
		//TODO
	}

	void SocketServerHandler::addtionalRcvThreadCleanupImpl() {
		m_socketLock.lock();
		if (m_multicastSocket != INVALID_SOCKET)
		{
			closesocket(m_multicastSocket);
			m_multicastSocket = INVALID_SOCKET;
		}

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
		if (m_multicastSocket != INVALID_SOCKET)
		{
			shutdown(m_multicastSocket, SD_BOTH);
		}

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
			sa.sin_addr = platformIpv4StringToAddr(m_connLessRemoteEndpoint.address.c_str());
			sa.sin_port = htons(m_connLessRemoteEndpoint.port);

			Log("Testing UDP connection to remote host: %s:%d\n", m_connLessRemoteEndpoint.address.c_str(), m_connLessRemoteEndpoint.port);

			//try to ping the destination
			m_connLessRemoteReachable = testUnreliableRemoteEndpointNoLock();
			
			//close socket if destination unreachable
			if (!m_connLessRemoteReachable) {
				if (m_connLessSocket != INVALID_SOCKET)
				{
					closesocket(m_connLessSocket);
					m_connLessSocket = INVALID_SOCKET;
				}
			} else {
				Log("Connected UDP to remote host\n");
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
		int err = 0;
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
				sa.sin_addr = platformIpv4StringToAddr(m_remoteEndpoint.address.c_str());
				sa.sin_port = htons(m_remoteEndpoint.port);
				
				platformSetSocketBlockingMode(m_connSocket, false);//disable blocking mode

				socket_t l_connSocket = m_connSocket;
				lk.unlock();

				Log("Connecting TCP to remote host: %s:%d\n", m_remoteEndpoint.address.c_str(), m_remoteEndpoint.port);

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
				} else {
					Log("Connected TCP to remote host\n");
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

	/*--------- SocketServerDiscoverClientHandler ----------*/
	SocketServerDiscoverClientHandler::SocketServerDiscoverClientHandler(DiscoveryDelegate* delegate, const char* discovery_multicast_group, int discovery_multicast_port)
		:BaseUnreliableSocketHandler(RANDOM_PORT), m_discoveryDelegate(delegate),
		 m_multicast_address(discovery_multicast_group != nullptr ? discovery_multicast_group : DEFAULT_MULTICAST_ADDRESS),
		 m_multicast_port(discovery_multicast_port)
	{
	}
	SocketServerDiscoverClientHandler::~SocketServerDiscoverClientHandler() {
	}

	void SocketServerDiscoverClientHandler::setDiscoveryDelegate(DiscoveryDelegate* delegate)
	{ 
		std::lock_guard<std::mutex> lg(m_discoveryDelegateLock);

		m_discoveryDelegate = delegate; 
	}

	bool SocketServerDiscoverClientHandler::socketInitImpl()
	{
		if (!BaseUnreliableSocketHandler::socketInitImpl())
			return false;
		int re;

		//enable broadcasting
		int _true = 1;
		setsockopt(m_connLessSocket, SOL_SOCKET, SO_BROADCAST, (char*)&_true, sizeof(_true));

		//increase TTL
		//set max TTL to reach more than one subnets
		int ttl = 3;
		re = setsockopt(m_connLessSocket, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));
		HQRemote::Log("SocketServerDiscoverClientHandler: setsockopt(IP_TTL) returned %d\n", re);

		//destination address is the multicast group
		m_connLessSocketDestAddr = std::unique_ptr<sockaddr_in>(new sockaddr_in());

		memset(m_connLessSocketDestAddr.get(), 0, sizeof(sockaddr_in));

		m_connLessSocketDestAddr->sin_family = AF_INET;
		m_connLessSocketDestAddr->sin_addr = platformIpv4StringToAddr(m_multicast_address.c_str());
		m_connLessSocketDestAddr->sin_port = htons(m_multicast_port);
		
		return true;
	}

	void SocketServerDiscoverClientHandler::findOtherServers(uint64_t request_id) {
		//msg = | PING_MSG_CHUNK | magic string | request_id |
		unsigned char ping_msg[sizeof(MULTICAST_MAGIC_STRING) + sizeof(uint64_t)];

		unsigned char type = PING_MSG_CHUNK;
		memcpy(ping_msg, &type, sizeof(type));
		memcpy(ping_msg + 1, MULTICAST_MAGIC_STRING, sizeof(MULTICAST_MAGIC_STRING) - 1);
		memcpy(ping_msg + sizeof(MULTICAST_MAGIC_STRING), &request_id, sizeof(request_id));//TODO: assume all platforms use the same endianess

		//send to multicast group 3 times to avoid packet drop
		sendRawDataUnreliableImpl(ping_msg, sizeof(ping_msg));
		sendRawDataUnreliableImpl(ping_msg, sizeof(ping_msg));
		sendRawDataUnreliableImpl(ping_msg, sizeof(ping_msg));

		//also attempt to broadcast the ping message in case multicast doesn't work
		struct sockaddr_in broadcast_addr = {0};
		broadcast_addr.sin_family = AF_INET;
		broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		broadcast_addr.sin_port = htons(m_multicast_port);

		m_socketLock.lock();
		sendRawDataUnreliableNoLock(m_connLessSocket, &broadcast_addr, ping_msg, sizeof(ping_msg));
		m_socketLock.unlock();
	}
	
	_ssize_t SocketServerDiscoverClientHandler::handleUnwantedDataFromImpl(const sockaddr_in& srcAddr, const void* data, size_t size) {
		auto msg = (const unsigned char*)data;
		switch (msg[0])
		{
		case PING_REPLY_MSG_CHUNK:
			if (size >= sizeof(MULTICAST_MAGIC_STRING) + sizeof(uint64_t) + 2 * sizeof(int) &&
				memcmp(msg + 1, MULTICAST_MAGIC_STRING, sizeof(MULTICAST_MAGIC_STRING) - 1) == 0)
			{
				//msg = | PING_REPLY_MSG_CHUNK | magic string | request_id | reliable port | unreliable port | description length | description
				int unreliable_port = ntohs(srcAddr.sin_port);
				int32_t reliable_port_in_msg, unreliable_port_in_msg;
				uint32_t descLen;
				uint64_t request_id;

				auto msg_ptr = msg + sizeof(MULTICAST_MAGIC_STRING);

				//TODO: assume all platforms use the same endianess
				memcpy(&request_id, msg_ptr, sizeof(request_id));
				memcpy(&reliable_port_in_msg, msg_ptr + sizeof(request_id), sizeof(reliable_port_in_msg));
				memcpy(&unreliable_port_in_msg, msg_ptr + sizeof(request_id) + sizeof(int32_t), sizeof(unreliable_port_in_msg));
				memcpy(&descLen, msg_ptr + sizeof(request_id) + 2 * sizeof(int32_t), sizeof(descLen));

				msg_ptr += sizeof(request_id) + 2 * sizeof(int32_t) + sizeof(descLen);

				uint32_t descMaxLen = (uint32_t)(size + msg - msg_ptr);//just to verify <descLen> is not corrupted
				descLen = min(descLen, descMaxLen);
				char desc[MULTICAST_MAX_MSG_SIZE];
				memcpy(desc, msg_ptr, descLen);
				desc[descLen] = '\0';

				std::lock_guard<std::mutex> lg(m_discoveryDelegateLock);
				char addr_buffer[20];
				if (platformIpv4AddrToString(&srcAddr.sin_addr, addr_buffer, sizeof(addr_buffer)) != NULL &&
					unreliable_port_in_msg == unreliable_port &&
					m_discoveryDelegate != NULL) {

					//HQRemote::Log("Get ping reply from %s:%d\n", addr_buffer, unreliable_port);

					//invoke delegate
					m_discoveryDelegate->onNewServerDiscovered(this, request_id, addr_buffer, reliable_port_in_msg, unreliable_port_in_msg, desc);
				}
			}
			break;
		}//switch (msg[0])

		return size;
	}
}
