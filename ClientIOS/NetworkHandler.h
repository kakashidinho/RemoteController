//
//  NetworkHandler.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import <Foundation/Foundation.h>

@protocol NetworkHandlerDelegate <NSObject>

//the returned value can be used as new delegate
- (id<NetworkHandlerDelegate>) onNetworkOpened;
- (id<NetworkHandlerDelegate>) onNetworkClosedWithError: (NSString*) err;
- (id<NetworkHandlerDelegate>) onReceivedData: (NSData*) data;

@end

@interface NetworkHandler : NSObject<NSStreamDelegate>

@property (nullable, weak, atomic) id<NetworkHandlerDelegate> delegate;

- (id) initWithIp: (NSString*) ip
			 port: (int) port
		 delegate: (id<NetworkHandlerDelegate>) delegate;

- (void) close;

- (void) sendData: (NSData*) data;

@end
