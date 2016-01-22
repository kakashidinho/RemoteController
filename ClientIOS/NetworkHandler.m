//
//  NetworkHandler.m
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "NetworkHandler.h"

#include <stdint.h>
#include <assert.h>

#ifndef min
#define min(a, b) ((a) > (b) ? (b) : (a))
#endif

typedef enum DataReaderState {
	READ_MSG_SIZE,
	READ_MSG_DATA,
} DataReaderState;

@interface NetworkHandler ()

@property (strong, atomic) NSInputStream *networkInputStream;
@property (strong, atomic) NSOutputStream *networkOutputStream;
@property (atomic) uint64_t networkBytesToReceive;
@property (strong, atomic) NSMutableData* networkBytesBuf;
@property (atomic) DataReaderState dataReaderState;
@property (atomic, strong) NSObject* lock;
@end

@implementation NetworkHandler

@synthesize delegate = _delegate;

- (id) initWithIp: (NSString*) ip
			 port: (int) port
		 delegate: (id<NetworkHandlerDelegate>) delegate{
	if (self = [super init]) {
		_lock = [[NSObject alloc] init];
		_delegate = delegate;
		
		//open connection
		CFReadStreamRef readStream;
		CFWriteStreamRef writeStream;
		CFStreamCreatePairWithSocketToHost(NULL, (__bridge CFStringRef)(ip), port, &readStream, &writeStream);
		self.networkInputStream = (NSInputStream *)CFBridgingRelease(readStream);
		self.networkOutputStream = (NSOutputStream *)CFBridgingRelease(writeStream);
		
		[self.networkInputStream setDelegate:self];
		[self.networkOutputStream setDelegate:self];
		
		[self.networkInputStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[self.networkOutputStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		
		[self.networkInputStream open];
		[self.networkOutputStream open];
		
		self.networkBytesToReceive = 0;
		self.dataReaderState = READ_MSG_SIZE;
	}
	
	return self;
}

- (void) close {
	[self closeWithError:nil];
}

- (void) sendData: (NSData*) data{
	if (self.networkOutputStream != nil &&
		(self.networkOutputStream.streamStatus == NSStreamStatusOpen ||
		self.networkOutputStream.streamStatus == NSStreamStatusWriting)) {
		
			[self.networkOutputStream write:data.bytes maxLength:data.length];
	}
}

- (id<NetworkHandlerDelegate>) delegate {
	return _delegate;
}

- (void) setDelegate:(id<NetworkHandlerDelegate>)delegate {
	@synchronized(_lock) {
		_delegate = delegate;
	}
}

- (void) closeWithError:(NSString *)errorMsg {
	if (self.networkInputStream)
		[self.networkInputStream close];
	if (self.networkOutputStream)
		[self.networkOutputStream close];
	self.networkInputStream = nil;
	self.networkOutputStream = nil;
	
	@synchronized(_lock) {
		if (_delegate)
		{
			_delegate = [_delegate onNetworkClosedWithError:errorMsg];
		}
	}
}

//stream delegate
- (void)stream:(NSStream *)stream handleEvent:(NSStreamEvent)event {
	switch(event) {
		case NSStreamEventOpenCompleted:
			@synchronized(_lock) {
				if (_delegate)
				{
				_delegate = [_delegate onNetworkOpened];
				}
			}
			break;
		case NSStreamEventHasBytesAvailable: {
			if(stream == self.networkInputStream) {
				if (self.dataReaderState == READ_MSG_SIZE && self.networkBytesToReceive == 0)
				{
					self.networkBytesToReceive = sizeof(uint64_t);
				}
				
				size_t intermediateBufSize = min(self.networkBytesToReceive, 1024);
				
				unsigned char *buf = malloc(intermediateBufSize);
				//TODO: allocation failed?
				NSInteger re = [self.networkInputStream read:buf maxLength:intermediateBufSize];
				if (re > 0) {
					if (self.networkBytesBuf == nil)
					{
						self.networkBytesBuf = [NSMutableData dataWithCapacity:self.networkBytesToReceive];
					}
					[self.networkBytesBuf appendBytes:buf length:re];
					
					self.networkBytesToReceive -= re;
					
					//we received enough data
					if (self.networkBytesToReceive == 0)
					{
						switch (self.dataReaderState) {
							case READ_MSG_SIZE:
							{
								//TODO: assume remote side and we use the same byte order for now
								uint64_t msgSize;
								assert(self.networkBytesBuf.length == sizeof msgSize);
								memcpy(&msgSize, self.networkBytesBuf.bytes, self.networkBytesBuf.length);
								
								//now we will read msg content
								self.networkBytesToReceive = msgSize;
								self.dataReaderState = READ_MSG_DATA;
							}
								break;
								
							case READ_MSG_DATA:
							{
								@synchronized(_lock) {
									if (_delegate)
									{
										_delegate = [_delegate onReceivedData:self.networkBytesBuf];
									}
								}
								//now we are waiting for next message
								self.networkBytesToReceive = 0;
								self.dataReaderState = READ_MSG_SIZE;
							}
								break;
								
							default:
								break;
						}
						
						//invalidate buffer
						self.networkBytesBuf = nil;
					}//if (self.networkBytesToReceive == 0)
				}//if (re > 0)
				
				free(buf);
			}
			break;
		}
		case NSStreamEventErrorOccurred:
			[self closeWithError:stream.streamError.localizedDescription];
			break;
		case NSStreamEventEndEncountered:
			[self closeWithError:@"Remote host disconnected"];
			break;
	}
}

- (void) dealloc {
	
}

@end
