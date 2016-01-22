//
//  BaseViewControllerWithNetwork.h
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import <UIKit/UIKit.h>

#import "NetworkHandler.h"

@interface BaseViewControllerWithNetwork : UIViewController<NetworkHandlerDelegate>

@property (strong, atomic) NetworkHandler* networkHandler;

- (void) transferNetworkHandlerFrom: (BaseViewControllerWithNetwork*) viewController;

//stream delegate (can be overriden)
- (id<NetworkHandlerDelegate>) onNetworkOpened;
- (id<NetworkHandlerDelegate>) onNetworkClosedWithError: (NSString*) err;
- (id<NetworkHandlerDelegate>) onReceivedData: (NSData*) data;

@end
