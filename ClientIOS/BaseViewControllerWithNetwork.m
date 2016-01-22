//
//  BaseViewControllerWithNetwork.m
//  RemoteController
//
//  Created by Le Hoang Quyen on 11/1/16.
//  Copyright © 2016 Le Hoang Quyen. All rights reserved.
//

#import "BaseViewControllerWithNetwork.h"

@interface BaseViewControllerWithNetwork ()

@end

@implementation BaseViewControllerWithNetwork

@synthesize networkHandler = _networkHandler;

- (void)viewDidLoad {
    [super viewDidLoad];

    // Do any additional setup after loading the view.
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

/*
#pragma mark - Navigation

// In a storyboard-based application, you will often want to do a little preparation before navigation
- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    // Get the new view controller using [segue destinationViewController].
    // Pass the selected object to the new view controller.
}
*/

- (NetworkHandler*) networkHandler {
	return _networkHandler;
}

- (void) setNetworkHandler: (NetworkHandler*) handler {
	
	if (_networkHandler)
	{
		_networkHandler.delegate = nil;
		_networkHandler = nil;
	}
	
	_networkHandler = handler;
	if (_networkHandler)
		_networkHandler.delegate = self;
	
}

- (void) transferNetworkHandlerFrom: (BaseViewControllerWithNetwork*) oldViewController {
	NetworkHandler* networkHandler = oldViewController.networkHandler;
	
	oldViewController.networkHandler = nil;
	
	[self setNetworkHandler:networkHandler];
}

//stream delegate
- (id<NetworkHandlerDelegate>) onNetworkOpened {
	//DO NOTHING
	return self;
}

- (id<NetworkHandlerDelegate>) onNetworkClosedWithError: (NSString*) errorMsg {
	self.networkHandler.delegate = nil;
	self.networkHandler = nil;
	
	
	//display popup message
	if (errorMsg != nil) {
		UIAlertController *alert =
		[UIAlertController alertControllerWithTitle:@"Error"
											message:errorMsg
									 preferredStyle:UIAlertControllerStyleAlert];
		
		
		
		UIAlertAction *action = [UIAlertAction actionWithTitle:@"OK"
														 style:UIAlertActionStyleDefault
													   handler:^(UIAlertAction *action) {
													   }];
		[alert addAction:action];
		
		[self presentViewController:alert animated:YES completion:nil];
	}
	
	return self;
}

- (id<NetworkHandlerDelegate>) onReceivedData: (NSData*) data {
	//DO NOTHING
	return self;
}

@end
